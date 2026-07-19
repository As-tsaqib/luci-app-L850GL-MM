/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ubus_glib.h"

#include "identity.h"
#include "network_binding.h"
#include "radio_policy.h"
#include "sms_policy.h"

#include <glib-unix.h>
#include <libubox/blobmsg.h>
#include <libubus.h>
#include <math.h>
#include <string.h>
#include <time.h>

#define RECONNECT_INITIAL_MS 250U
#define RECONNECT_MAX_MS 30000U
#define SAFE_TEXT_MAX 160U
#define SAFE_PORT_MAX 64U
#define SMS_DEFAULT_LIMIT 50U
#define SMS_MAX_LIMIT 100U
#define SMS_INBOUND_CHARS_MAX 4096U
#define SMS_INBOUND_BYTES_MAX 16384U
#define SMS_OPERATION_TIMEOUT_SECONDS 320U
#define SMS_DEDUPE_SECONDS 300U
#define SMS_DEDUPE_MAX 64U
#define ADVANCED_OPERATION_TIMEOUT_SECONDS 130U
#define ADVANCED_PROXY_TIMEOUT_MS 120000
#define ADVANCED_SHORT_COOLDOWN_SECONDS 10U
#define ADVANCED_REPROBE_COOLDOWN_SECONDS 60U
#define MAX_RADIO_BANDS 256U
#define MAX_RADIO_MODES 32U
#define MAX_SIM_SLOTS 16U

G_STATIC_ASSERT(FIBOCOM_SMS_REQUEST_DIGEST_LEN ==
		FIBOCOM_SMS_SHA256_DIGEST_LEN);

struct _FibocomUbus {
	gint refs;
	FibocomBridge *bridge;
	gchar *socket_path;
	struct ubus_context context;
	struct ubus_object object;
	gboolean context_initialized;
	gboolean connected;
	gboolean stopping;
	guint fd_source;
	guint reconnect_source;
	guint reconnect_delay_ms;
	GHashTable *sms_operations;
	GHashTable *advanced_operations;
	gint64 reprobe_cooldown_until;
};

typedef enum {
	SMS_OPERATION_SEND,
	SMS_OPERATION_DELETE,
} SmsOperationType;

typedef struct {
	SmsOperationType type;
	FibocomUbus *ubus;
	FibocomModem *modem;
	MMModemMessaging *messaging;
	MMSmsProperties *properties;
	MMSms *sms;
	FibocomSms *sms_entry;
	GCancellable *cancellable;
	struct ubus_request_data request;
	guint32 generation;
	guint32 messaging_generation;
	guint timeout_source;
	gchar *client_token;
	gchar *sms_id;
	guint8 request_digest[FIBOCOM_SMS_REQUEST_DIGEST_LEN];
	gboolean has_request_digest;
	gboolean timed_out;
	gboolean send_dispatched;
	gboolean transport_lost;
	gboolean deferred;
} SmsOperation;

typedef enum {
	ADVANCED_OPERATION_BANDS,
	ADVANCED_OPERATION_RADIO_ENABLE,
	ADVANCED_OPERATION_RADIO_DISABLE,
	ADVANCED_OPERATION_RESET,
	ADVANCED_OPERATION_SIM_SLOT,
} AdvancedOperationType;

typedef struct {
	AdvancedOperationType type;
	FibocomUbus *ubus;
	FibocomModem *modem;
	GCancellable *cancellable;
	struct ubus_request_data request;
	guint32 generation;
	guint timeout_source;
	guint cooldown_seconds;
	guint slot;
	MMModemBand *bands;
	guint n_bands;
	gboolean timed_out;
	gboolean transport_lost;
	gboolean dispatched;
	gboolean deferred;
} AdvancedOperation;

static FibocomUbus *fibocom_ubus_ref_internal(FibocomUbus *ubus);
static void fibocom_ubus_unref_internal(FibocomUbus *ubus);

static void
cancel_sms_operations(FibocomUbus *ubus, gboolean transport_lost)
{
	GList *operations;
	GList *cursor;

	if (ubus == NULL || ubus->sms_operations == NULL)
		return;
	operations = g_hash_table_get_keys(ubus->sms_operations);
	for (cursor = operations; cursor != NULL; cursor = cursor->next) {
		SmsOperation *operation = cursor->data;

		operation->deferred = FALSE;
		operation->transport_lost = transport_lost;
		g_cancellable_cancel(operation->cancellable);
	}
	g_list_free(operations);
}

static void
cancel_advanced_operations(FibocomUbus *ubus, gboolean transport_lost)
{
	GList *operations;
	GList *cursor;

	if (ubus == NULL || ubus->advanced_operations == NULL)
		return;
	operations = g_hash_table_get_keys(ubus->advanced_operations);
	for (cursor = operations; cursor != NULL; cursor = cursor->next) {
		AdvancedOperation *operation = cursor->data;

		operation->deferred = FALSE;
		operation->transport_lost = transport_lost;
		g_cancellable_cancel(operation->cancellable);
	}
	g_list_free(operations);
}

enum {
	MODEM_ID,
	__MODEM_MAX,
};

static const struct blobmsg_policy modem_policy[__MODEM_MAX] = {
	[MODEM_ID] = { .name = "modem_id", .type = BLOBMSG_TYPE_STRING },
};

enum {
	LIST_SMS_MODEM_ID,
	LIST_SMS_FOLDER,
	LIST_SMS_LIMIT,
	LIST_SMS_CURSOR,
	__LIST_SMS_MAX,
};

static const struct blobmsg_policy list_sms_policy[__LIST_SMS_MAX] = {
	[LIST_SMS_MODEM_ID] = { .name = "modem_id", .type = BLOBMSG_TYPE_STRING },
	[LIST_SMS_FOLDER] = { .name = "folder", .type = BLOBMSG_TYPE_STRING },
	[LIST_SMS_LIMIT] = { .name = "limit", .type = BLOBMSG_TYPE_INT32 },
	[LIST_SMS_CURSOR] = { .name = "cursor", .type = BLOBMSG_TYPE_STRING },
};

enum {
	SEND_SMS_MODEM_ID,
	SEND_SMS_GENERATION,
	SEND_SMS_MESSAGING_GENERATION,
	SEND_SMS_RECIPIENT,
	SEND_SMS_TEXT,
	SEND_SMS_CLIENT_TOKEN,
	__SEND_SMS_MAX,
};

static const struct blobmsg_policy send_sms_policy[__SEND_SMS_MAX] = {
	[SEND_SMS_MODEM_ID] = { .name = "modem_id", .type = BLOBMSG_TYPE_STRING },
	[SEND_SMS_GENERATION] = { .name = "generation", .type = BLOBMSG_TYPE_INT32 },
	[SEND_SMS_MESSAGING_GENERATION] = {
		.name = "messaging_generation", .type = BLOBMSG_TYPE_INT32 },
	[SEND_SMS_RECIPIENT] = { .name = "recipient", .type = BLOBMSG_TYPE_STRING },
	[SEND_SMS_TEXT] = { .name = "text", .type = BLOBMSG_TYPE_STRING },
	[SEND_SMS_CLIENT_TOKEN] = {
		.name = "client_token", .type = BLOBMSG_TYPE_STRING },
};

enum {
	DELETE_SMS_MODEM_ID,
	DELETE_SMS_GENERATION,
	DELETE_SMS_MESSAGING_GENERATION,
	DELETE_SMS_SMS_ID,
	DELETE_SMS_CONFIRM,
	__DELETE_SMS_MAX,
};

static const struct blobmsg_policy delete_sms_policy[__DELETE_SMS_MAX] = {
	[DELETE_SMS_MODEM_ID] = { .name = "modem_id", .type = BLOBMSG_TYPE_STRING },
	[DELETE_SMS_GENERATION] = {
		.name = "generation", .type = BLOBMSG_TYPE_INT32 },
	[DELETE_SMS_MESSAGING_GENERATION] = {
		.name = "messaging_generation", .type = BLOBMSG_TYPE_INT32 },
	[DELETE_SMS_SMS_ID] = { .name = "sms_id", .type = BLOBMSG_TYPE_STRING },
	[DELETE_SMS_CONFIRM] = { .name = "confirm", .type = BLOBMSG_TYPE_BOOL },
};

enum {
	SET_BANDS_MODEM_ID,
	SET_BANDS_GENERATION,
	SET_BANDS_BANDS,
	SET_BANDS_CONFIRM,
	__SET_BANDS_MAX,
};

static const struct blobmsg_policy set_bands_policy[__SET_BANDS_MAX] = {
	[SET_BANDS_MODEM_ID] = { .name = "modem_id", .type = BLOBMSG_TYPE_STRING },
	[SET_BANDS_GENERATION] = {
		.name = "generation", .type = BLOBMSG_TYPE_INT32 },
	[SET_BANDS_BANDS] = { .name = "bands", .type = BLOBMSG_TYPE_ARRAY },
	[SET_BANDS_CONFIRM] = { .name = "confirm", .type = BLOBMSG_TYPE_BOOL },
};

enum {
	SET_RADIO_MODEM_ID,
	SET_RADIO_GENERATION,
	SET_RADIO_ENABLED,
	SET_RADIO_CONFIRM,
	__SET_RADIO_MAX,
};

static const struct blobmsg_policy set_radio_policy[__SET_RADIO_MAX] = {
	[SET_RADIO_MODEM_ID] = { .name = "modem_id", .type = BLOBMSG_TYPE_STRING },
	[SET_RADIO_GENERATION] = {
		.name = "generation", .type = BLOBMSG_TYPE_INT32 },
	[SET_RADIO_ENABLED] = { .name = "enabled", .type = BLOBMSG_TYPE_BOOL },
	[SET_RADIO_CONFIRM] = { .name = "confirm", .type = BLOBMSG_TYPE_BOOL },
};

enum {
	RESET_MODEM_ID,
	RESET_GENERATION,
	RESET_CONFIRM,
	__RESET_MAX,
};

static const struct blobmsg_policy reset_policy[__RESET_MAX] = {
	[RESET_MODEM_ID] = { .name = "modem_id", .type = BLOBMSG_TYPE_STRING },
	[RESET_GENERATION] = {
		.name = "generation", .type = BLOBMSG_TYPE_INT32 },
	[RESET_CONFIRM] = { .name = "confirm", .type = BLOBMSG_TYPE_BOOL },
};

enum {
	SET_SIM_SLOT_MODEM_ID,
	SET_SIM_SLOT_GENERATION,
	SET_SIM_SLOT_SLOT,
	SET_SIM_SLOT_CONFIRM,
	__SET_SIM_SLOT_MAX,
};

static const struct blobmsg_policy set_sim_slot_policy[__SET_SIM_SLOT_MAX] = {
	[SET_SIM_SLOT_MODEM_ID] = {
		.name = "modem_id", .type = BLOBMSG_TYPE_STRING },
	[SET_SIM_SLOT_GENERATION] = {
		.name = "generation", .type = BLOBMSG_TYPE_INT32 },
	[SET_SIM_SLOT_SLOT] = { .name = "slot", .type = BLOBMSG_TYPE_INT32 },
	[SET_SIM_SLOT_CONFIRM] = {
		.name = "confirm", .type = BLOBMSG_TYPE_BOOL },
};

static int method_list_modems(struct ubus_context *context,
			      struct ubus_object *object,
			      struct ubus_request_data *request,
			      const char *method, struct blob_attr *message);
static int method_get_overview(struct ubus_context *context,
			       struct ubus_object *object,
			       struct ubus_request_data *request,
			       const char *method, struct blob_attr *message);
static int method_get_status(struct ubus_context *context,
			     struct ubus_object *object,
			     struct ubus_request_data *request,
			     const char *method, struct blob_attr *message);
static int method_get_capabilities(struct ubus_context *context,
				   struct ubus_object *object,
				   struct ubus_request_data *request,
				   const char *method,
				   struct blob_attr *message);
static int method_list_sms(struct ubus_context *context,
			   struct ubus_object *object,
			   struct ubus_request_data *request,
			   const char *method, struct blob_attr *message);
static int method_send_sms(struct ubus_context *context,
			   struct ubus_object *object,
			   struct ubus_request_data *request,
			   const char *method, struct blob_attr *message);
static int method_delete_sms(struct ubus_context *context,
			     struct ubus_object *object,
			     struct ubus_request_data *request,
			     const char *method, struct blob_attr *message);
static int method_set_bands(struct ubus_context *context,
			    struct ubus_object *object,
			    struct ubus_request_data *request,
			    const char *method, struct blob_attr *message);
static int method_set_radio(struct ubus_context *context,
			    struct ubus_object *object,
			    struct ubus_request_data *request,
			    const char *method, struct blob_attr *message);
static int method_reset(struct ubus_context *context,
			struct ubus_object *object,
			struct ubus_request_data *request,
			const char *method, struct blob_attr *message);
static int method_set_primary_sim_slot(struct ubus_context *context,
			       struct ubus_object *object,
			       struct ubus_request_data *request,
			       const char *method,
			       struct blob_attr *message);

static const struct ubus_method fibocom_methods[] = {
	UBUS_METHOD_NOARG("list_modems", method_list_modems),
	UBUS_METHOD("get_overview", method_get_overview, modem_policy),
	UBUS_METHOD("get_status", method_get_status, modem_policy),
	UBUS_METHOD("get_capabilities", method_get_capabilities, modem_policy),
	UBUS_METHOD("list_sms", method_list_sms, list_sms_policy),
	UBUS_METHOD("send_sms", method_send_sms, send_sms_policy),
	UBUS_METHOD("delete_sms", method_delete_sms, delete_sms_policy),
	UBUS_METHOD("set_bands", method_set_bands, set_bands_policy),
	UBUS_METHOD("set_radio", method_set_radio, set_radio_policy),
	UBUS_METHOD("reset", method_reset, reset_policy),
	UBUS_METHOD("set_primary_sim_slot", method_set_primary_sim_slot,
		set_sim_slot_policy),
};

static struct ubus_object_type fibocom_object_type =
	UBUS_OBJECT_TYPE("fibocom.mm", fibocom_methods);

static FibocomUbus *
from_object(struct ubus_object *object)
{
	return (FibocomUbus *)((gchar *)object -
		G_STRUCT_OFFSET(FibocomUbus, object));
}

static gchar *
safe_text(const gchar *value, gsize limit)
{
	const gchar *cursor;
	gsize characters;
	gchar *result;

	if (value == NULL || !g_utf8_validate(value, -1, NULL))
		return g_strdup("");
	for (cursor = value; *cursor != '\0'; cursor = g_utf8_next_char(cursor)) {
		gunichar character = g_utf8_get_char(cursor);

		if (g_unichar_iscntrl(character))
			return g_strdup("");
	}
	characters = (gsize)g_utf8_strlen(value, -1);
	result = characters > limit ? g_utf8_substring(value, 0, limit) :
		g_strdup(value);
	return result;
}

static void
add_safe_string(struct blob_buf *buffer, const gchar *name,
		const gchar *value, gsize limit)
{
	g_autofree gchar *sanitized = safe_text(value, limit);

	blobmsg_add_string(buffer, name, sanitized);
}

static gchar *
masked_identifier(const gchar *value)
{
	gsize length;
	gsize i;

	if (value == NULL || !g_utf8_validate(value, -1, NULL))
		return g_strdup("");
	length = strlen(value);
	if (length < 4U)
		return g_strdup(value[0] == '\0' ? "" : "****");
	for (i = length - 4U; i < length; i++) {
		if (!g_ascii_isalnum(value[i]))
			return g_strdup("****");
	}
	return g_strdup_printf("****%s", value + length - 4U);
}

static void
add_masked_string(struct blob_buf *buffer, const gchar *name,
		  const gchar *value)
{
	g_autofree gchar *masked = masked_identifier(value);

	blobmsg_add_string(buffer, name, masked);
}

static const gchar *
state_name(MMModemState state)
{
	switch (state) {
	case MM_MODEM_STATE_FAILED: return "failed";
	case MM_MODEM_STATE_INITIALIZING: return "initializing";
	case MM_MODEM_STATE_LOCKED: return "locked";
	case MM_MODEM_STATE_DISABLED: return "disabled";
	case MM_MODEM_STATE_DISABLING: return "disabling";
	case MM_MODEM_STATE_ENABLING: return "enabling";
	case MM_MODEM_STATE_ENABLED: return "enabled";
	case MM_MODEM_STATE_SEARCHING: return "searching";
	case MM_MODEM_STATE_REGISTERED: return "registered";
	case MM_MODEM_STATE_DISCONNECTING: return "disconnecting";
	case MM_MODEM_STATE_CONNECTING: return "connecting";
	case MM_MODEM_STATE_CONNECTED: return "connected";
	case MM_MODEM_STATE_UNKNOWN:
	default: return "unknown";
	}
}

static const gchar *
failed_reason_name(MMModemStateFailedReason reason)
{
	switch (reason) {
	case MM_MODEM_STATE_FAILED_REASON_NONE: return "none";
	case MM_MODEM_STATE_FAILED_REASON_SIM_MISSING: return "sim-missing";
	case MM_MODEM_STATE_FAILED_REASON_SIM_ERROR: return "sim-error";
	case MM_MODEM_STATE_FAILED_REASON_UNKNOWN_CAPABILITIES:
		return "unknown-capabilities";
	case MM_MODEM_STATE_FAILED_REASON_ESIM_WITHOUT_PROFILES:
		return "esim-without-profiles";
	case MM_MODEM_STATE_FAILED_REASON_UNKNOWN:
	default: return "unknown";
	}
}

static const gchar *
power_state_name(MMModemPowerState state)
{
	switch (state) {
	case MM_MODEM_POWER_STATE_OFF: return "off";
	case MM_MODEM_POWER_STATE_LOW: return "low";
	case MM_MODEM_POWER_STATE_ON: return "on";
	case MM_MODEM_POWER_STATE_UNKNOWN:
	default: return "unknown";
	}
}

static const gchar *
lock_name(MMModemLock lock)
{
	switch (lock) {
	case MM_MODEM_LOCK_NONE: return "none";
	case MM_MODEM_LOCK_SIM_PIN: return "sim-pin";
	case MM_MODEM_LOCK_SIM_PIN2: return "sim-pin2";
	case MM_MODEM_LOCK_SIM_PUK: return "sim-puk";
	case MM_MODEM_LOCK_SIM_PUK2: return "sim-puk2";
	case MM_MODEM_LOCK_PH_SP_PIN: return "service-provider-pin";
	case MM_MODEM_LOCK_PH_SP_PUK: return "service-provider-puk";
	case MM_MODEM_LOCK_PH_NET_PIN: return "network-pin";
	case MM_MODEM_LOCK_PH_NET_PUK: return "network-puk";
	case MM_MODEM_LOCK_PH_SIM_PIN: return "phone-sim-pin";
	case MM_MODEM_LOCK_PH_CORP_PIN: return "corporate-pin";
	case MM_MODEM_LOCK_PH_CORP_PUK: return "corporate-puk";
	case MM_MODEM_LOCK_PH_FSIM_PIN: return "fixed-sim-pin";
	case MM_MODEM_LOCK_PH_FSIM_PUK: return "fixed-sim-puk";
	case MM_MODEM_LOCK_PH_NETSUB_PIN: return "network-subset-pin";
	case MM_MODEM_LOCK_PH_NETSUB_PUK: return "network-subset-puk";
	case MM_MODEM_LOCK_UNKNOWN:
	default: return "unknown";
	}
}

static const gchar *
registration_name(MMModem3gppRegistrationState state)
{
	switch (state) {
	case MM_MODEM_3GPP_REGISTRATION_STATE_IDLE: return "idle";
	case MM_MODEM_3GPP_REGISTRATION_STATE_HOME: return "home";
	case MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING: return "searching";
	case MM_MODEM_3GPP_REGISTRATION_STATE_DENIED: return "denied";
	case MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING: return "roaming";
	case MM_MODEM_3GPP_REGISTRATION_STATE_HOME_SMS_ONLY:
		return "home-sms-only";
	case MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING_SMS_ONLY:
		return "roaming-sms-only";
	case MM_MODEM_3GPP_REGISTRATION_STATE_EMERGENCY_ONLY:
		return "emergency-only";
	case MM_MODEM_3GPP_REGISTRATION_STATE_HOME_CSFB_NOT_PREFERRED:
		return "home-csfb-not-preferred";
	case MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING_CSFB_NOT_PREFERRED:
		return "roaming-csfb-not-preferred";
	case MM_MODEM_3GPP_REGISTRATION_STATE_ATTACHED_RLOS:
		return "attached-rlos";
	case MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN:
	default: return "unknown";
	}
}

static gboolean
registration_is_roaming(MMModem3gppRegistrationState state)
{
	return state == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING ||
		state == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING_SMS_ONLY ||
		state == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING_CSFB_NOT_PREFERRED;
}

static const gchar *
packet_service_name(MMModem3gppPacketServiceState state)
{
	switch (state) {
	case MM_MODEM_3GPP_PACKET_SERVICE_STATE_DETACHED: return "detached";
	case MM_MODEM_3GPP_PACKET_SERVICE_STATE_ATTACHED: return "attached";
	case MM_MODEM_3GPP_PACKET_SERVICE_STATE_UNKNOWN:
	default: return "unknown";
	}
}

static const gchar *
port_type_name(MMModemPortType type)
{
	switch (type) {
	case MM_MODEM_PORT_TYPE_NET: return "net";
	case MM_MODEM_PORT_TYPE_AT: return "at";
	case MM_MODEM_PORT_TYPE_QCDM: return "qcdm";
	case MM_MODEM_PORT_TYPE_GPS: return "gps";
	case MM_MODEM_PORT_TYPE_QMI: return "qmi";
	case MM_MODEM_PORT_TYPE_MBIM: return "mbim";
	case MM_MODEM_PORT_TYPE_AUDIO: return "audio";
	case MM_MODEM_PORT_TYPE_IGNORED: return "ignored";
	case MM_MODEM_PORT_TYPE_XMMRPC: return "xmmrpc";
	case MM_MODEM_PORT_TYPE_UNKNOWN:
	default: return "unknown";
	}
}

static const gchar *
ip_method_name(MMBearerIpMethod method)
{
	switch (method) {
	case MM_BEARER_IP_METHOD_PPP: return "ppp";
	case MM_BEARER_IP_METHOD_STATIC: return "static";
	case MM_BEARER_IP_METHOD_DHCP: return "dhcp";
	case MM_BEARER_IP_METHOD_UNKNOWN:
	default: return "unknown";
	}
}

static void
add_common(struct blob_buf *buffer, gboolean ok)
{
	blobmsg_add_u32(buffer, "schema", FIBOCOM_MM_API_SCHEMA);
	blobmsg_add_u64(buffer, "generated_at", (guint64)time(NULL));
	blobmsg_add_u8(buffer, "ok", ok);
}

static int
send_buffer(struct ubus_context *context, struct ubus_request_data *request,
	    struct blob_buf *buffer)
{
	int status = ubus_send_reply(context, request, buffer->head);

	blob_buf_free(buffer);
	return status;
}

static int
send_error(struct ubus_context *context, struct ubus_request_data *request,
	   const gchar *code, const gchar *message, gboolean retryable)
{
	struct blob_buf buffer = {};
	void *error;

	blob_buf_init(&buffer, 0);
	add_common(&buffer, FALSE);
	error = blobmsg_open_table(&buffer, "error");
	blobmsg_add_string(&buffer, "code", code);
	blobmsg_add_string(&buffer, "message", message);
	blobmsg_add_u8(&buffer, "retryable", retryable);
	blobmsg_close_table(&buffer, error);
	return send_buffer(context, request, &buffer);
}

static gboolean
message_is_empty(struct blob_attr *message)
{
	struct blob_attr *attribute;
	unsigned int remaining;

	if (message == NULL)
		return TRUE;
	blobmsg_for_each_attr(attribute, message, remaining)
		return FALSE;
	return TRUE;
}

static gboolean
parse_modem_id(struct blob_attr *message, struct blob_attr **parsed)
{
	struct blob_attr *attribute;
	unsigned int remaining;
	gboolean seen = FALSE;

	if (message == NULL)
		return FALSE;
	blobmsg_for_each_attr(attribute, message, remaining) {
		if (!g_str_equal(blobmsg_name(attribute), "modem_id") || seen ||
		    blobmsg_type(attribute) != BLOBMSG_TYPE_STRING)
			return FALSE;
		seen = TRUE;
	}
	if (!seen)
		return FALSE;
	blobmsg_parse(modem_policy, __MODEM_MAX, parsed, blob_data(message),
		      blob_len(message));
	return parsed[MODEM_ID] != NULL &&
		fibocom_identity_is_valid(blobmsg_get_string(parsed[MODEM_ID]));
}

static FibocomModem *
requested_modem(FibocomUbus *ubus, struct blob_attr *message,
		struct blob_attr **parsed, const gchar **error_code)
{
	FibocomModem *modem;

	if (!parse_modem_id(message, parsed)) {
		*error_code = "invalid_argument";
		return NULL;
	}
	modem = fibocom_bridge_find_modem(ubus->bridge,
		blobmsg_get_string(parsed[MODEM_ID]));
	if (modem == NULL) {
		*error_code = fibocom_bridge_manager_available(ubus->bridge) ?
			"not_found" : "dependency_unavailable";
		return NULL;
	}
	*error_code = NULL;
	return modem;
}

static int
send_requested_error(struct ubus_context *context,
		     struct ubus_request_data *request,
		     const gchar *error_code)
{
	if (g_str_equal(error_code, "invalid_argument"))
		return send_error(context, request, error_code,
			"exactly one valid modem_id is required", FALSE);
	if (g_str_equal(error_code, "dependency_unavailable"))
		return send_error(context, request, error_code,
			"ModemManager is not available", TRUE);
	return send_error(context, request, "not_found",
			  "modem_id is not present in the live inventory", FALSE);
}

static gboolean
blob_string_is_canonical(struct blob_attr *attribute)
{
	const gchar *value;
	gsize length;

	if (attribute == NULL ||
	    blobmsg_type(attribute) != BLOBMSG_TYPE_STRING)
		return FALSE;
	value = blobmsg_data(attribute);
	length = blobmsg_data_len(attribute);
	return length > 0U && value[length - 1U] == '\0' &&
		memchr(value, '\0', length - 1U) == NULL;
}

static gboolean
parse_exact_fields(struct blob_attr *message,
		   const struct blobmsg_policy *policy, guint policy_length,
		   guint64 required, struct blob_attr **parsed)
{
	struct blob_attr *attribute;
	unsigned int remaining;
	guint64 seen = 0;

	if (message == NULL || policy_length == 0U || policy_length > 63U)
		return FALSE;
	blobmsg_for_each_attr(attribute, message, remaining) {
		guint index;

		for (index = 0; index < policy_length; index++) {
			if (g_str_equal(blobmsg_name(attribute), policy[index].name))
				break;
		}
		if (index == policy_length || (seen & (G_GUINT64_CONSTANT(1) << index)) ||
		    blobmsg_type(attribute) != (int)policy[index].type)
			return FALSE;
		if (policy[index].type == BLOBMSG_TYPE_STRING &&
		    !blob_string_is_canonical(attribute))
			return FALSE;
		seen |= G_GUINT64_CONSTANT(1) << index;
		parsed[index] = attribute;
	}
	return (seen & required) == required;
}

static gchar *
safe_sms_text(const gchar *value, gboolean *truncated)
{
	const gchar *cursor;
	GString *result;
	gsize characters = 0;

	*truncated = FALSE;
	if (value == NULL)
		return g_strdup("");
	if (!g_utf8_validate(value, -1, NULL)) {
		*truncated = TRUE;
		return g_strdup("");
	}
	result = g_string_sized_new(MIN(strlen(value), SMS_INBOUND_BYTES_MAX));
	for (cursor = value; *cursor != '\0'; cursor = g_utf8_next_char(cursor)) {
		gunichar character = g_utf8_get_char(cursor);
		gchar encoded[6];
		gint encoded_length;

		if (characters >= SMS_INBOUND_CHARS_MAX) {
			*truncated = TRUE;
			break;
		}
		if (g_unichar_iscntrl(character) && character != '\n' &&
		    character != '\r' && character != '\t') {
			character = 0xfffd;
			*truncated = TRUE;
		}
		encoded_length = g_unichar_to_utf8(character, encoded);
		if (result->len + (gsize)encoded_length > SMS_INBOUND_BYTES_MAX) {
			*truncated = TRUE;
			break;
		}
		g_string_append_len(result, encoded, encoded_length);
		characters++;
	}
	return g_string_free(result, FALSE);
}

static const gchar *
sms_state_name(MMSmsState state)
{
	switch (state) {
	case MM_SMS_STATE_STORED: return "stored";
	case MM_SMS_STATE_RECEIVING: return "receiving";
	case MM_SMS_STATE_RECEIVED: return "received";
	case MM_SMS_STATE_SENDING: return "sending";
	case MM_SMS_STATE_SENT: return "sent";
	case MM_SMS_STATE_UNKNOWN:
	default: return "unknown";
	}
}

static const gchar *
sms_pdu_type_name(MMSmsPduType type)
{
	switch (type) {
	case MM_SMS_PDU_TYPE_DELIVER: return "deliver";
	case MM_SMS_PDU_TYPE_SUBMIT: return "submit";
	case MM_SMS_PDU_TYPE_STATUS_REPORT: return "status-report";
	case MM_SMS_PDU_TYPE_CDMA_DELIVER: return "cdma-deliver";
	case MM_SMS_PDU_TYPE_CDMA_SUBMIT: return "cdma-submit";
	case MM_SMS_PDU_TYPE_CDMA_CANCELLATION: return "cdma-cancellation";
	case MM_SMS_PDU_TYPE_CDMA_DELIVERY_ACKNOWLEDGEMENT:
		return "cdma-delivery-acknowledgement";
	case MM_SMS_PDU_TYPE_CDMA_USER_ACKNOWLEDGEMENT:
		return "cdma-user-acknowledgement";
	case MM_SMS_PDU_TYPE_CDMA_READ_ACKNOWLEDGEMENT:
		return "cdma-read-acknowledgement";
	case MM_SMS_PDU_TYPE_UNKNOWN:
	default: return "unknown";
	}
}

static const gchar *
sms_storage_name(MMSmsStorage storage)
{
	switch (storage) {
	case MM_SMS_STORAGE_SM: return "sm";
	case MM_SMS_STORAGE_ME: return "me";
	case MM_SMS_STORAGE_MT: return "mt";
	case MM_SMS_STORAGE_SR: return "sr";
	case MM_SMS_STORAGE_BM: return "bm";
	case MM_SMS_STORAGE_TA: return "ta";
	case MM_SMS_STORAGE_UNKNOWN:
	default: return "unknown";
	}
}

static const gchar *
sms_direction(MMSms *sms)
{
	switch (mm_sms_get_pdu_type(sms)) {
	case MM_SMS_PDU_TYPE_DELIVER:
	case MM_SMS_PDU_TYPE_CDMA_DELIVER:
		return "inbound";
	case MM_SMS_PDU_TYPE_SUBMIT:
	case MM_SMS_PDU_TYPE_CDMA_SUBMIT:
	case MM_SMS_PDU_TYPE_CDMA_CANCELLATION:
		return "outbound";
	case MM_SMS_PDU_TYPE_STATUS_REPORT:
	case MM_SMS_PDU_TYPE_CDMA_DELIVERY_ACKNOWLEDGEMENT:
	case MM_SMS_PDU_TYPE_CDMA_USER_ACKNOWLEDGEMENT:
	case MM_SMS_PDU_TYPE_CDMA_READ_ACKNOWLEDGEMENT:
		return "report";
	default:
		return "unknown";
	}
}

static const gchar *
sms_folder(MMSms *sms)
{
	MMSmsState state = mm_sms_get_state(sms);
	const gchar *direction = sms_direction(sms);

	if (g_str_equal(direction, "report"))
		return "unknown";
	if (state == MM_SMS_STATE_STORED)
		return "draft";
	if (g_str_equal(direction, "inbound") &&
	    (state == MM_SMS_STATE_RECEIVING || state == MM_SMS_STATE_RECEIVED))
		return "inbox";
	if (g_str_equal(direction, "outbound") &&
	    (state == MM_SMS_STATE_SENDING || state == MM_SMS_STATE_SENT))
		return "outbox";
	return "unknown";
}

static gboolean
sms_folder_is_valid(const gchar *folder)
{
	return g_str_equal(folder, "all") || g_str_equal(folder, "inbox") ||
		g_str_equal(folder, "outbox") || g_str_equal(folder, "draft") ||
		g_str_equal(folder, "unknown");
}

static gint
sms_compare(gconstpointer left, gconstpointer right)
{
	const FibocomSms *left_entry = *(FibocomSms *const *)left;
	const FibocomSms *right_entry = *(FibocomSms *const *)right;
	const gchar *left_timestamp = mm_sms_get_timestamp(left_entry->sms);
	const gchar *right_timestamp = mm_sms_get_timestamp(right_entry->sms);
	gint compared = g_strcmp0(right_timestamp, left_timestamp);

	if (compared != 0)
		return compared;
	return g_strcmp0(left_entry->sms_id, right_entry->sms_id);
}

static void
add_sms_entry(struct blob_buf *buffer, FibocomSms *entry)
{
	MMSms *sms = entry->sms;
	g_autofree gchar *text = NULL;
	gboolean text_truncated;
	gsize data_length = 0;
	void *table = blobmsg_open_table(buffer, NULL);

	text = safe_sms_text(mm_sms_get_text(sms), &text_truncated);
	blobmsg_add_string(buffer, "sms_id", entry->sms_id);
	blobmsg_add_string(buffer, "folder", sms_folder(sms));
	blobmsg_add_string(buffer, "direction", sms_direction(sms));
	blobmsg_add_string(buffer, "state", sms_state_name(mm_sms_get_state(sms)));
	add_safe_string(buffer, "number", mm_sms_get_number(sms), 32U);
	blobmsg_add_string(buffer, "text", text);
	blobmsg_add_u8(buffer, "text_truncated", text_truncated);
	add_safe_string(buffer, "timestamp", mm_sms_get_timestamp(sms), 64U);
	add_safe_string(buffer, "discharge_timestamp",
		mm_sms_get_discharge_timestamp(sms), 64U);
	blobmsg_add_string(buffer, "pdu_type",
		sms_pdu_type_name(mm_sms_get_pdu_type(sms)));
	blobmsg_add_u32(buffer, "delivery_state", mm_sms_get_delivery_state(sms));
	blobmsg_add_u32(buffer, "message_reference",
		mm_sms_get_message_reference(sms));
	blobmsg_add_string(buffer, "storage",
		sms_storage_name(mm_sms_get_storage(sms)));
	(void)mm_sms_get_data(sms, &data_length);
	blobmsg_add_u8(buffer, "has_binary_data", data_length > 0U);
	blobmsg_close_table(buffer, table);
}

typedef struct {
	const gchar *code;
	const gchar *message;
	gboolean retryable;
} SmsNormalizedError;

static const gchar *
sms_error_message(const gchar *code)
{
	if (g_str_equal(code, "device_gone"))
		return "The modem was removed during the operation";
	if (g_str_equal(code, "stale_identity"))
		return "The modem identity is no longer live";
	if (g_str_equal(code, "stale_generation"))
		return "The modem generation changed; refresh and retry";
	if (g_str_equal(code, "stale_messaging_generation"))
		return "The active SIM or Messaging view changed; refresh and retry";
	if (g_str_equal(code, "stale_cursor"))
		return "The SMS inventory changed; restart pagination";
	if (g_str_equal(code, "unsupported"))
		return "This SMS operation is not supported by the modem";
	if (g_str_equal(code, "not_ready"))
		return "The modem is not ready for this SMS operation";
	if (g_str_equal(code, "busy"))
		return "Another modem mutation is already in progress";
	if (g_str_equal(code, "timeout"))
		return "The SMS operation timed out";
	if (g_str_equal(code, "outcome_unknown"))
		return "The SMS may have been sent; refresh before any retry";
	if (g_str_equal(code, "storage_full"))
		return "The modem or SIM SMS storage is full";
	if (g_str_equal(code, "permission_denied"))
		return "ModemManager denied this SMS operation";
	if (g_str_equal(code, "dependency_unavailable"))
		return "ModemManager is not available";
	if (g_str_equal(code, "not_found"))
		return "The SMS is no longer present in the live inventory";
	if (g_str_equal(code, "invalid_argument"))
		return "The SMS operation arguments are invalid";
	if (g_str_equal(code, "internal_error"))
		return "The SMS operation could not be tracked safely";
	return "The SMS operation failed";
}

static gboolean
remote_error_has_suffix(const gchar *remote, const gchar *suffix)
{
	return remote != NULL && g_str_has_suffix(remote, suffix);
}

static gboolean
sms_error_is_timeout_or_transport(GError *error, const gchar *remote)
{
	if (error == NULL)
		return FALSE;
	return g_error_matches(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT) ||
		g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED) ||
		g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CLOSED) ||
		g_error_matches(error, G_IO_ERROR, G_IO_ERROR_BROKEN_PIPE) ||
		g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_NO_REPLY) ||
		g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_TIMEOUT) ||
		g_error_matches(error, MM_CORE_ERROR, MM_CORE_ERROR_TIMEOUT) ||
		g_error_matches(error, MM_MOBILE_EQUIPMENT_ERROR,
			MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT) ||
		g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN) ||
		g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_NAME_HAS_NO_OWNER) ||
		g_error_matches(error, MM_CORE_ERROR, MM_CORE_ERROR_TIMEOUT) ||
		g_error_matches(error, MM_MESSAGE_ERROR,
			MM_MESSAGE_ERROR_NETWORK_TIMEOUT) ||
		g_error_matches(error, MM_MOBILE_EQUIPMENT_ERROR,
			MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT) ||
		remote_error_has_suffix(remote, ".Core.Timeout") ||
		remote_error_has_suffix(remote,
			".MobileEquipment.NetworkTimeout") ||
		remote_error_has_suffix(remote, ".Message.NetworkTimeout");
}

static SmsNormalizedError
normalize_sms_error(GError *error, SmsOperation *operation)
{
	g_autofree gchar *remote = NULL;
	FibocomModem *modem = operation->modem;

	if (error != NULL && g_dbus_error_is_remote_error(error))
		remote = g_dbus_error_get_remote_error(error);
	if (operation->type == SMS_OPERATION_SEND &&
	    operation->send_dispatched &&
	    (operation->timed_out || operation->transport_lost ||
	     sms_error_is_timeout_or_transport(error, remote)))
		return (SmsNormalizedError){ "outcome_unknown",
			sms_error_message("outcome_unknown"), FALSE };
	if (modem == NULL || !modem->live)
		return (SmsNormalizedError){ "device_gone",
			sms_error_message("device_gone"), FALSE };
	if (modem->generation != operation->generation)
		return (SmsNormalizedError){ "stale_generation",
			sms_error_message("stale_generation"), FALSE };
	if (modem->messaging_generation != operation->messaging_generation ||
	    modem->messaging != operation->messaging)
		return (SmsNormalizedError){ "stale_messaging_generation",
			sms_error_message("stale_messaging_generation"), FALSE };
	if (operation->timed_out)
		return (SmsNormalizedError){ "timeout",
			sms_error_message("timeout"), TRUE };
	if (operation->transport_lost)
		return (SmsNormalizedError){ "dependency_unavailable",
			sms_error_message("dependency_unavailable"), TRUE };
	if (error == NULL)
		return (SmsNormalizedError){ "operation_failed",
			sms_error_message("operation_failed"), FALSE };
	if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT))
		return (SmsNormalizedError){ "timeout",
			sms_error_message("timeout"), TRUE };
	if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED))
		return (SmsNormalizedError){ "permission_denied",
			sms_error_message("permission_denied"), FALSE };
	if (g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN) ||
	    g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_NAME_HAS_NO_OWNER))
		return (SmsNormalizedError){ "dependency_unavailable",
			sms_error_message("dependency_unavailable"), TRUE };
	if (g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED) ||
	    g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_AUTH_FAILED))
		return (SmsNormalizedError){ "permission_denied",
			sms_error_message("permission_denied"), FALSE };
	if (g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_NO_REPLY) ||
	    g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_TIMEOUT))
		return (SmsNormalizedError){ "timeout",
			sms_error_message("timeout"), TRUE };
	if (g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD) ||
	    g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED) ||
	    g_error_matches(error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED) ||
	    g_error_matches(error, MM_MOBILE_EQUIPMENT_ERROR,
		MM_MOBILE_EQUIPMENT_ERROR_NOT_SUPPORTED) ||
	    g_error_matches(error, MM_MESSAGE_ERROR,
		MM_MESSAGE_ERROR_NOT_SUPPORTED))
		return (SmsNormalizedError){ "unsupported",
			sms_error_message("unsupported"), FALSE };
	if (remote_error_has_suffix(remote, ".Core.Unsupported") ||
	    remote_error_has_suffix(remote, ".MobileEquipment.NotSupported") ||
	    remote_error_has_suffix(remote, ".Message.NotSupported"))
		return (SmsNormalizedError){ "unsupported",
			sms_error_message("unsupported"), FALSE };
	if (g_error_matches(error, MM_CORE_ERROR,
		MM_CORE_ERROR_UNAUTHORIZED) ||
	    g_error_matches(error, MM_MOBILE_EQUIPMENT_ERROR,
		MM_MOBILE_EQUIPMENT_ERROR_NOT_ALLOWED) ||
	    g_error_matches(error, MM_MESSAGE_ERROR,
		MM_MESSAGE_ERROR_NOT_ALLOWED) ||
	    remote_error_has_suffix(remote, ".Core.Unauthorized") ||
	    remote_error_has_suffix(remote, ".Message.NotAllowed"))
		return (SmsNormalizedError){ "permission_denied",
			sms_error_message("permission_denied"), FALSE };
	if (g_error_matches(error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS) ||
	    g_error_matches(error, MM_MOBILE_EQUIPMENT_ERROR,
		MM_MOBILE_EQUIPMENT_ERROR_INVALID_CHARS) ||
	    g_error_matches(error, MM_MOBILE_EQUIPMENT_ERROR,
		MM_MOBILE_EQUIPMENT_ERROR_TEXT_TOO_LONG) ||
	    g_error_matches(error, MM_MESSAGE_ERROR,
		MM_MESSAGE_ERROR_INVALID_PDU_PARAMETER) ||
	    g_error_matches(error, MM_MESSAGE_ERROR,
		MM_MESSAGE_ERROR_INVALID_TEXT_PARAMETER) ||
	    remote_error_has_suffix(remote, ".Core.InvalidArgs") ||
	    remote_error_has_suffix(remote, ".MobileEquipment.InvalidChars") ||
	    remote_error_has_suffix(remote, ".MobileEquipment.TextTooLong") ||
	    remote_error_has_suffix(remote, ".Message.InvalidPduParameter") ||
	    remote_error_has_suffix(remote, ".Message.InvalidTextParameter"))
		return (SmsNormalizedError){ "invalid_argument",
			sms_error_message("invalid_argument"), FALSE };
	if (g_error_matches(error, MM_MESSAGE_ERROR,
		MM_MESSAGE_ERROR_MEMORY_FULL) ||
	    g_error_matches(error, MM_MOBILE_EQUIPMENT_ERROR,
		MM_MOBILE_EQUIPMENT_ERROR_MEMORY_FULL) ||
	    remote_error_has_suffix(remote, ".Message.MemoryFull") ||
	    remote_error_has_suffix(remote, ".MobileEquipment.MemoryFull"))
		return (SmsNormalizedError){ "storage_full",
			sms_error_message("storage_full"), FALSE };
	if (g_error_matches(error, MM_CORE_ERROR, MM_CORE_ERROR_IN_PROGRESS) ||
	    g_error_matches(error, MM_CORE_ERROR, MM_CORE_ERROR_THROTTLED) ||
	    g_error_matches(error, MM_MOBILE_EQUIPMENT_ERROR,
		MM_MOBILE_EQUIPMENT_ERROR_SIM_BUSY) ||
	    g_error_matches(error, MM_MESSAGE_ERROR,
		MM_MESSAGE_ERROR_SIM_BUSY) ||
	    remote_error_has_suffix(remote, ".Core.InProgress") ||
	    remote_error_has_suffix(remote, ".Core.Throttled") ||
	    remote_error_has_suffix(remote, ".MobileEquipment.SimBusy") ||
	    remote_error_has_suffix(remote, ".Message.SimBusy"))
		return (SmsNormalizedError){ "busy",
			sms_error_message("busy"), TRUE };
	if (g_error_matches(error, MM_CORE_ERROR, MM_CORE_ERROR_NOT_FOUND) ||
	    g_error_matches(error, MM_MOBILE_EQUIPMENT_ERROR,
		MM_MOBILE_EQUIPMENT_ERROR_NOT_FOUND) ||
	    g_error_matches(error, MM_MOBILE_EQUIPMENT_ERROR,
		MM_MOBILE_EQUIPMENT_ERROR_INVALID_INDEX) ||
	    g_error_matches(error, MM_MESSAGE_ERROR,
		MM_MESSAGE_ERROR_INVALID_INDEX) ||
	    remote_error_has_suffix(remote, ".Core.NotFound") ||
	    remote_error_has_suffix(remote, ".MobileEquipment.NotFound") ||
	    remote_error_has_suffix(remote, ".MobileEquipment.InvalidIndex") ||
	    remote_error_has_suffix(remote, ".Message.InvalidIndex"))
		return (SmsNormalizedError){ "not_found",
			sms_error_message("not_found"), FALSE };
	if (g_error_matches(error, MM_CORE_ERROR, MM_CORE_ERROR_TIMEOUT) ||
	    g_error_matches(error, MM_MESSAGE_ERROR,
		MM_MESSAGE_ERROR_NETWORK_TIMEOUT) ||
	    g_error_matches(error, MM_MOBILE_EQUIPMENT_ERROR,
		MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT) ||
	    remote_error_has_suffix(remote, ".Core.Timeout") ||
	    remote_error_has_suffix(remote, ".MobileEquipment.NetworkTimeout") ||
	    remote_error_has_suffix(remote, ".Message.NetworkTimeout"))
		return (SmsNormalizedError){ "timeout",
			sms_error_message("timeout"), TRUE };
	if (g_error_matches(error, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE) ||
	    g_error_matches(error, MM_CORE_ERROR,
		MM_CORE_ERROR_WRONG_SIM_STATE) ||
	    g_error_matches(error, MM_CORE_ERROR, MM_CORE_ERROR_RETRY) ||
	    g_error_matches(error, MM_CORE_ERROR,
		MM_CORE_ERROR_RESET_AND_RETRY) ||
	    g_error_matches(error, MM_MOBILE_EQUIPMENT_ERROR,
		MM_MOBILE_EQUIPMENT_ERROR_NO_NETWORK) ||
	    g_error_matches(error, MM_MOBILE_EQUIPMENT_ERROR,
		MM_MOBILE_EQUIPMENT_ERROR_SIM_NOT_INSERTED) ||
	    g_error_matches(error, MM_MESSAGE_ERROR,
		MM_MESSAGE_ERROR_NO_NETWORK) ||
	    g_error_matches(error, MM_MESSAGE_ERROR,
		MM_MESSAGE_ERROR_SIM_NOT_INSERTED) ||
	    remote_error_has_suffix(remote, ".Core.WrongState") ||
	    remote_error_has_suffix(remote, ".Core.WrongSimState") ||
	    remote_error_has_suffix(remote, ".Core.Retry") ||
	    remote_error_has_suffix(remote, ".Core.ResetAndRetry") ||
	    remote_error_has_suffix(remote, ".MobileEquipment.NoNetwork") ||
	    remote_error_has_suffix(remote, ".Message.NoNetwork"))
		return (SmsNormalizedError){ "not_ready",
			sms_error_message("not_ready"), TRUE };
	return (SmsNormalizedError){ "operation_failed",
		sms_error_message("operation_failed"), FALSE };
}

static void
sms_dedupe_prune(FibocomModem *modem)
{
	gint64 now = g_get_monotonic_time();
	GList *cursor;
	GList *next;

	for (cursor = modem->sms_dedupe->head; cursor != NULL; cursor = next) {
		FibocomSmsDedupe *entry = cursor->data;

		next = cursor->next;
		if (entry->expires_at <= now) {
			g_queue_delete_link(modem->sms_dedupe, cursor);
			g_free(entry->client_token);
			g_free(entry->sms_id);
			g_free(entry->state);
			g_free(entry->error_code);
			g_free(entry);
		}
	}
}

static gboolean
sms_dedupe_digest_matches(const FibocomSmsDedupe *entry,
			  const guint8 digest[FIBOCOM_SMS_REQUEST_DIGEST_LEN])
{
	return entry != NULL && entry->has_request_digest && digest != NULL &&
		fibocom_sms_digest_equal(entry->request_digest, digest);
}

static FibocomSmsDedupe *
sms_dedupe_find(FibocomModem *modem, const gchar *client_token)
{
	GList *cursor;

	sms_dedupe_prune(modem);
	for (cursor = modem->sms_dedupe->head; cursor != NULL;
	     cursor = cursor->next) {
		FibocomSmsDedupe *entry = cursor->data;

		if (g_str_equal(entry->client_token, client_token))
			return entry;
	}
	return NULL;
}

static void
sms_dedupe_remove(FibocomModem *modem, const gchar *client_token)
{
	FibocomSmsDedupe *entry = sms_dedupe_find(modem, client_token);

	if (entry == NULL || !g_queue_remove(modem->sms_dedupe, entry))
		return;
	g_free(entry->client_token);
	g_free(entry->sms_id);
	g_free(entry->state);
	g_free(entry->error_code);
	g_free(entry);
}

static void
sms_dedupe_store(FibocomModem *modem, const gchar *client_token,
		 const guint8 request_digest[FIBOCOM_SMS_REQUEST_DIGEST_LEN],
		 gboolean ok, const gchar *sms_id, const gchar *state,
		 const gchar *error_code, gboolean retryable)
{
	FibocomSmsDedupe *entry;

	if (client_token == NULL || modem->sms_dedupe == NULL)
		return;
	sms_dedupe_prune(modem);
	entry = sms_dedupe_find(modem, client_token);
	if (entry == NULL) {
		while (g_queue_get_length(modem->sms_dedupe) >= SMS_DEDUPE_MAX) {
			entry = g_queue_pop_head(modem->sms_dedupe);
			g_free(entry->client_token);
			g_free(entry->sms_id);
			g_free(entry->state);
			g_free(entry->error_code);
			g_free(entry);
		}
		entry = g_new0(FibocomSmsDedupe, 1);
		entry->client_token = g_strdup(client_token);
		g_queue_push_tail(modem->sms_dedupe, entry);
	}
	g_free(entry->sms_id);
	g_free(entry->state);
	g_free(entry->error_code);
	entry->ok = ok;
	entry->sms_id = g_strdup(sms_id);
	entry->state = g_strdup(state);
	entry->error_code = g_strdup(error_code);
	entry->retryable = retryable;
	entry->has_request_digest = request_digest != NULL;
	if (request_digest != NULL)
		memcpy(entry->request_digest, request_digest,
			FIBOCOM_SMS_REQUEST_DIGEST_LEN);
	entry->expires_at = g_get_monotonic_time() +
		((gint64)SMS_DEDUPE_SECONDS * G_USEC_PER_SEC);
}

static void
add_access_technologies(struct blob_buf *buffer, MMModemAccessTechnology value)
{
	void *array = blobmsg_open_array(buffer, "access");
	static const struct {
		MMModemAccessTechnology flag;
		const gchar *name;
	} technologies[] = {
		{ MM_MODEM_ACCESS_TECHNOLOGY_POTS, "pots" },
		{ MM_MODEM_ACCESS_TECHNOLOGY_GSM, "gsm" },
		{ MM_MODEM_ACCESS_TECHNOLOGY_GSM_COMPACT, "gsm-compact" },
		{ MM_MODEM_ACCESS_TECHNOLOGY_GPRS, "gprs" },
		{ MM_MODEM_ACCESS_TECHNOLOGY_EDGE, "edge" },
		{ MM_MODEM_ACCESS_TECHNOLOGY_UMTS, "umts" },
		{ MM_MODEM_ACCESS_TECHNOLOGY_HSDPA, "hsdpa" },
		{ MM_MODEM_ACCESS_TECHNOLOGY_HSUPA, "hsupa" },
		{ MM_MODEM_ACCESS_TECHNOLOGY_HSPA, "hspa" },
		{ MM_MODEM_ACCESS_TECHNOLOGY_HSPA_PLUS, "hspa-plus" },
		{ MM_MODEM_ACCESS_TECHNOLOGY_1XRTT, "1xrtt" },
		{ MM_MODEM_ACCESS_TECHNOLOGY_EVDO0, "evdo0" },
		{ MM_MODEM_ACCESS_TECHNOLOGY_EVDOA, "evdoa" },
		{ MM_MODEM_ACCESS_TECHNOLOGY_EVDOB, "evdob" },
		{ MM_MODEM_ACCESS_TECHNOLOGY_LTE, "lte" },
		{ MM_MODEM_ACCESS_TECHNOLOGY_5GNR, "5gnr" },
		{ MM_MODEM_ACCESS_TECHNOLOGY_LTE_CAT_M, "lte-cat-m" },
		{ MM_MODEM_ACCESS_TECHNOLOGY_LTE_NB_IOT, "lte-nb-iot" },
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS(technologies); i++) {
		if ((value & technologies[i].flag) != 0)
			blobmsg_add_string(buffer, NULL, technologies[i].name);
	}
	blobmsg_close_array(buffer, array);
}

static MMBearer *
preferred_bearer(FibocomModem *modem)
{
	GList *cursor;

	for (cursor = modem->bearers; cursor != NULL; cursor = cursor->next) {
		if (mm_bearer_get_connected(MM_BEARER(cursor->data)))
			return MM_BEARER(cursor->data);
	}
	return modem->bearers != NULL ? MM_BEARER(modem->bearers->data) : NULL;
}

static void
add_ip_families(struct blob_buf *buffer, MMBearer *bearer)
{
	void *array = blobmsg_open_array(buffer, "ip_families");
	MMBearerIpConfig *ipv4;
	MMBearerIpConfig *ipv6;

	if (bearer != NULL) {
		ipv4 = mm_bearer_peek_ipv4_config(bearer);
		ipv6 = mm_bearer_peek_ipv6_config(bearer);
		if (ipv4 != NULL && mm_bearer_ip_config_get_method(ipv4) !=
		    MM_BEARER_IP_METHOD_UNKNOWN)
			blobmsg_add_string(buffer, NULL, "ipv4");
		if (ipv6 != NULL && mm_bearer_ip_config_get_method(ipv6) !=
		    MM_BEARER_IP_METHOD_UNKNOWN)
			blobmsg_add_string(buffer, NULL, "ipv6");
	}
	blobmsg_close_array(buffer, array);
}

static void
add_modem_identity(struct blob_buf *buffer, FibocomModem *modem)
{
	blobmsg_add_string(buffer, "modem_id", modem->modem_id);
	blobmsg_add_u32(buffer, "generation", modem->generation);
}

static void
add_network(struct blob_buf *buffer, FibocomModem *modem)
{
	MMModem3gpp *gpp = mm_object_peek_modem_3gpp(modem->object);
	MMModem3gppRegistrationState registration =
		MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
	void *network = blobmsg_open_table(buffer, "network");

	if (gpp != NULL)
		registration = mm_modem_3gpp_get_registration_state(gpp);
	blobmsg_add_string(buffer, "registration", registration_name(registration));
	add_safe_string(buffer, "operator",
		gpp != NULL ? mm_modem_3gpp_get_operator_name(gpp) : "",
		SAFE_TEXT_MAX);
	add_safe_string(buffer, "operator_code",
		gpp != NULL ? mm_modem_3gpp_get_operator_code(gpp) : "", 8U);
	blobmsg_add_u8(buffer, "roaming", registration_is_roaming(registration));
	blobmsg_add_string(buffer, "packet_service",
		gpp != NULL ? packet_service_name(
			mm_modem_3gpp_get_packet_service_state(gpp)) : "unknown");
	add_access_technologies(buffer,
		mm_modem_get_access_technologies(modem->modem));
	blobmsg_close_table(buffer, network);
}

static void
add_signal_summary(struct blob_buf *buffer, FibocomModem *modem)
{
	gboolean recent = FALSE;
	guint quality = mm_modem_get_signal_quality(modem->modem, &recent);
	void *signal = blobmsg_open_table(buffer, "signal");

	blobmsg_add_u32(buffer, "quality", MIN(quality, 100U));
	blobmsg_add_u8(buffer, "recent", recent);
	blobmsg_close_table(buffer, signal);
}

static void
add_sim_summary(struct blob_buf *buffer, FibocomModem *modem)
{
	const gchar *sim_path = mm_modem_get_sim_path(modem->modem);
	void *sim = blobmsg_open_table(buffer, "sim");

	blobmsg_add_u8(buffer, "present",
		sim_path != NULL && !g_str_equal(sim_path, "/"));
	blobmsg_add_u32(buffer, "slot", mm_modem_get_primary_sim_slot(modem->modem));
	blobmsg_add_string(buffer, "lock",
		lock_name(mm_modem_get_unlock_required(modem->modem)));
	blobmsg_add_string(buffer, "cache_state", modem->sim_cache_state);
	blobmsg_close_table(buffer, sim);
}

static void
add_bearer_summary(struct blob_buf *buffer, FibocomModem *modem)
{
	MMBearer *bearer = preferred_bearer(modem);
	void *entry = blobmsg_open_table(buffer, "bearer");

	blobmsg_add_u8(buffer, "connected",
		bearer != NULL && mm_bearer_get_connected(bearer));
	add_safe_string(buffer, "interface",
		bearer != NULL ? mm_bearer_get_interface(bearer) : "",
		SAFE_PORT_MAX);
	add_ip_families(buffer, bearer);
	blobmsg_add_string(buffer, "cache_state", modem->bearer_cache_state);
	blobmsg_close_table(buffer, entry);
}

static void
add_openwrt_unavailable(struct blob_buf *buffer)
{
	void *openwrt = blobmsg_open_table(buffer, "openwrt");

	blobmsg_add_string(buffer, "state", "unavailable");
	blobmsg_add_string(buffer, "reason",
		"netifd-runtime-correlation-not-implemented");
	blobmsg_add_string(buffer, "network", "");
	blobmsg_add_u8(buffer, "up", FALSE);
	blobmsg_close_table(buffer, openwrt);
}

static int
method_list_modems(struct ubus_context *context, struct ubus_object *object,
		   struct ubus_request_data *request, const char *method,
		   struct blob_attr *message)
{
	FibocomUbus *ubus = from_object(object);
	g_autoptr(GPtrArray) modems = NULL;
	struct blob_buf buffer = {};
	void *array;
	void *dependencies;
	gboolean mbim_seen = FALSE;
	guint i;

	(void)method;
	if (!message_is_empty(message))
		return send_error(context, request, "invalid_argument",
			"list_modems does not accept arguments", FALSE);
	modems = fibocom_bridge_snapshot_modems(ubus->bridge);
	blob_buf_init(&buffer, 0);
	add_common(&buffer, TRUE);
	array = blobmsg_open_array(&buffer, "modems");
	for (i = 0; i < modems->len; i++) {
		FibocomModem *modem = g_ptr_array_index(modems, i);
		const gchar *composition = fibocom_modem_composition(modem);
		void *entry = blobmsg_open_table(&buffer, NULL);

		add_modem_identity(&buffer, modem);
		add_safe_string(&buffer, "manufacturer",
			mm_modem_get_manufacturer(modem->modem), SAFE_TEXT_MAX);
		add_safe_string(&buffer, "model", mm_modem_get_model(modem->modem),
			SAFE_TEXT_MAX);
		add_safe_string(&buffer, "plugin", mm_modem_get_plugin(modem->modem),
			SAFE_TEXT_MAX);
		blobmsg_add_string(&buffer, "composition", composition);
		blobmsg_add_string(&buffer, "state",
			state_name(mm_modem_get_state(modem->modem)));
		blobmsg_add_u8(&buffer, "supported", fibocom_modem_is_supported(modem));
		blobmsg_add_string(&buffer, "support_reason",
			fibocom_modem_support_reason(modem));
		blobmsg_add_u64(&buffer, "last_changed_at", modem->last_changed_at);
		blobmsg_close_table(&buffer, entry);
		if (g_str_equal(composition, "mbim"))
			mbim_seen = TRUE;
	}
	blobmsg_close_array(&buffer, array);
	dependencies = blobmsg_open_table(&buffer, "dependencies");
	blobmsg_add_string(&buffer, "modemmanager",
		fibocom_bridge_manager_available(ubus->bridge) ? "available" :
		"unavailable");
	blobmsg_add_string(&buffer, "netifd_proto",
		g_file_test("/lib/netifd/proto/modemmanager.sh", G_FILE_TEST_EXISTS) ?
		"available" : "unknown");
	blobmsg_add_string(&buffer, "fibocom_plugin",
		modems->len > 0 ? "available" : "unknown");
	blobmsg_add_string(&buffer, "mbim", mbim_seen ? "available" : "unknown");
	blobmsg_close_table(&buffer, dependencies);
	return send_buffer(context, request, &buffer);
}

static int
method_get_overview(struct ubus_context *context, struct ubus_object *object,
		    struct ubus_request_data *request, const char *method,
		    struct blob_attr *message)
{
	FibocomUbus *ubus = from_object(object);
	struct blob_attr *parsed[__MODEM_MAX] = {};
	const gchar *error_code;
	FibocomModem *modem;
	struct blob_buf buffer = {};
	void *general;
	void *warnings;

	(void)method;
	modem = requested_modem(ubus, message, parsed, &error_code);
	if (modem == NULL)
		return send_requested_error(context, request, error_code);
	blob_buf_init(&buffer, 0);
	add_common(&buffer, TRUE);
	add_modem_identity(&buffer, modem);
	blobmsg_add_string(&buffer, "freshness", modem->live ? "fresh" : "stale");
	general = blobmsg_open_table(&buffer, "modem");
	add_safe_string(&buffer, "model", mm_modem_get_model(modem->modem),
		SAFE_TEXT_MAX);
	add_safe_string(&buffer, "revision", mm_modem_get_revision(modem->modem),
		SAFE_TEXT_MAX);
	blobmsg_add_string(&buffer, "state",
		state_name(mm_modem_get_state(modem->modem)));
	blobmsg_close_table(&buffer, general);
	add_sim_summary(&buffer, modem);
	add_network(&buffer, modem);
	add_signal_summary(&buffer, modem);
	add_bearer_summary(&buffer, modem);
	add_openwrt_unavailable(&buffer);
	warnings = blobmsg_open_array(&buffer, "warnings");
	if (!g_str_equal(modem->sim_cache_state, "ready") &&
	    !g_str_equal(modem->sim_cache_state, "absent"))
		blobmsg_add_string(&buffer, NULL, "sim-snapshot-incomplete");
	if (!g_str_equal(modem->bearer_cache_state, "ready"))
		blobmsg_add_string(&buffer, NULL, "bearer-snapshot-incomplete");
	blobmsg_add_string(&buffer, NULL,
		"openwrt-runtime-state-unavailable");
	blobmsg_close_array(&buffer, warnings);
	return send_buffer(context, request, &buffer);
}

static void
add_general_status(struct blob_buf *buffer, FibocomModem *modem)
{
	const gchar *const *drivers = mm_modem_get_drivers(modem->modem);
	void *general = blobmsg_open_table(buffer, "general");
	void *array;
	guint i;

	add_safe_string(buffer, "manufacturer", mm_modem_get_manufacturer(modem->modem),
		SAFE_TEXT_MAX);
	add_safe_string(buffer, "model", mm_modem_get_model(modem->modem), SAFE_TEXT_MAX);
	add_safe_string(buffer, "revision", mm_modem_get_revision(modem->modem),
		SAFE_TEXT_MAX);
	add_safe_string(buffer, "plugin", mm_modem_get_plugin(modem->modem), SAFE_TEXT_MAX);
	array = blobmsg_open_array(buffer, "drivers");
	if (drivers != NULL) {
		for (i = 0; drivers[i] != NULL && i < 16U; i++)
			add_safe_string(buffer, NULL, drivers[i], SAFE_TEXT_MAX);
	}
	blobmsg_close_array(buffer, array);
	add_masked_string(buffer, "equipment_identifier",
		mm_modem_get_equipment_identifier(modem->modem));
	blobmsg_add_string(buffer, "state", state_name(mm_modem_get_state(modem->modem)));
	blobmsg_add_string(buffer, "failure_reason",
		failed_reason_name(mm_modem_get_state_failed_reason(modem->modem)));
	blobmsg_add_string(buffer, "power_state",
		power_state_name(mm_modem_get_power_state(modem->modem)));
	blobmsg_close_table(buffer, general);
}

static void
add_ports_status(struct blob_buf *buffer, FibocomModem *modem)
{
	const MMModemPortInfo *ports = NULL;
	const gchar *primary = mm_modem_get_primary_port(modem->modem);
	guint n_ports = 0;
	guint i;
	void *array = blobmsg_open_array(buffer, "ports");

	if (mm_modem_peek_ports(modem->modem, &ports, &n_ports)) {
		for (i = 0; i < n_ports && i < 32U; i++) {
			void *entry = blobmsg_open_table(buffer, NULL);

			add_safe_string(buffer, "name", ports[i].name, SAFE_PORT_MAX);
			blobmsg_add_string(buffer, "type", port_type_name(ports[i].type));
			blobmsg_add_string(buffer, "role",
				primary != NULL && g_strcmp0(primary, ports[i].name) == 0 ?
				"primary" : "secondary");
			blobmsg_add_u8(buffer, "primary",
				primary != NULL && g_strcmp0(primary, ports[i].name) == 0);
			blobmsg_close_table(buffer, entry);
		}
	}
	blobmsg_close_array(buffer, array);
}

static guint
sim_slot_count(FibocomModem *modem)
{
	const gchar *const *paths = mm_modem_get_sim_slot_paths(modem->modem);
	guint count = 0;

	if (paths != NULL) {
		while (paths[count] != NULL && count < MAX_SIM_SLOTS)
			count++;
	}
	return count;
}

static void
add_sim_status(struct blob_buf *buffer, FibocomModem *modem)
{
	const gchar *sim_path = mm_modem_get_sim_path(modem->modem);
	const gchar *const *slot_paths =
		mm_modem_get_sim_slot_paths(modem->modem);
	guint primary_slot = mm_modem_get_primary_sim_slot(modem->modem);
	guint slots = sim_slot_count(modem);
	void *sim = blobmsg_open_table(buffer, "sim");
	void *slot_array;
	guint index;

	blobmsg_add_u8(buffer, "present",
		sim_path != NULL && !g_str_equal(sim_path, "/"));
	blobmsg_add_string(buffer, "cache_state", modem->sim_cache_state);
	blobmsg_add_u32(buffer, "primary_slot", primary_slot);
	blobmsg_add_string(buffer, "lock",
		lock_name(mm_modem_get_unlock_required(modem->modem)));
	if (modem->sim != NULL) {
		blobmsg_add_u8(buffer, "active", mm_sim_get_active(modem->sim));
		add_masked_string(buffer, "iccid", mm_sim_get_identifier(modem->sim));
		add_masked_string(buffer, "imsi", mm_sim_get_imsi(modem->sim));
		add_safe_string(buffer, "operator_code",
			mm_sim_get_operator_identifier(modem->sim), 8U);
		add_safe_string(buffer, "operator",
			mm_sim_get_operator_name(modem->sim), SAFE_TEXT_MAX);
	}
	slot_array = blobmsg_open_array(buffer, "slots");
	for (index = 0; index < slots; index++) {
		const gchar *path = slot_paths[index];
		gboolean primary = primary_slot == index + 1U;
		void *slot = blobmsg_open_table(buffer, NULL);

		blobmsg_add_u32(buffer, "slot", index + 1U);
		blobmsg_add_u8(buffer, "present",
			path != NULL && !g_str_equal(path, "/"));
		blobmsg_add_u8(buffer, "primary", primary);
		blobmsg_add_u8(buffer, "active", primary);
		blobmsg_close_table(buffer, slot);
	}
	blobmsg_close_array(buffer, slot_array);
	blobmsg_close_table(buffer, sim);
}

static guint
mode_family_mask(MMModemMode modes)
{
	guint families = FIBOCOM_RADIO_FAMILY_NONE;

	if ((modes & MM_MODEM_MODE_2G) != 0)
		families |= FIBOCOM_RADIO_FAMILY_2G;
	if ((modes & MM_MODEM_MODE_3G) != 0)
		families |= FIBOCOM_RADIO_FAMILY_3G;
	if ((modes & MM_MODEM_MODE_4G) != 0)
		families |= FIBOCOM_RADIO_FAMILY_4G;
	if ((modes & MM_MODEM_MODE_5G) != 0)
		families |= FIBOCOM_RADIO_FAMILY_5G;
	return families;
}

static void
add_mode_array(struct blob_buf *buffer, const gchar *name,
	       MMModemMode modes)
{
	void *array = blobmsg_open_array(buffer, name);

	if ((modes & MM_MODEM_MODE_2G) != 0)
		blobmsg_add_string(buffer, NULL, "2g");
	if ((modes & MM_MODEM_MODE_3G) != 0)
		blobmsg_add_string(buffer, NULL, "3g");
	if ((modes & MM_MODEM_MODE_4G) != 0)
		blobmsg_add_string(buffer, NULL, "4g");
	if ((modes & MM_MODEM_MODE_5G) != 0)
		blobmsg_add_string(buffer, NULL, "5g");
	blobmsg_close_array(buffer, array);
}

static const gchar *
preferred_mode_name(MMModemMode mode)
{
	switch (mode) {
	case MM_MODEM_MODE_NONE: return "none";
	case MM_MODEM_MODE_2G: return "2g";
	case MM_MODEM_MODE_3G: return "3g";
	case MM_MODEM_MODE_4G: return "4g";
	case MM_MODEM_MODE_5G: return "5g";
	case MM_MODEM_MODE_ANY: return "any";
	case MM_MODEM_MODE_CS:
	default: return "unknown";
	}
}

static guint
add_band_array(struct blob_buf *buffer, const gchar *name,
	       const MMModemBand *bands, guint n_bands)
{
	void *array = blobmsg_open_array(buffer, name);
	guint added = 0;
	guint index;

	for (index = 0; bands != NULL && index < n_bands &&
	     index < MAX_RADIO_BANDS; index++) {
		const gchar *band_name;

		if (bands[index] == MM_MODEM_BAND_UNKNOWN)
			continue;
		band_name = mm_modem_band_get_string(bands[index]);
		if (!fibocom_radio_band_name_is_canonical(band_name))
			continue;
		blobmsg_add_string(buffer, NULL, band_name);
		added++;
	}
	blobmsg_close_array(buffer, array);
	return added;
}

static const gchar *
radio_state_name(FibocomModem *modem)
{
	MMModemState state = mm_modem_get_state(modem->modem);
	MMModemPowerState power = mm_modem_get_power_state(modem->modem);

	if (state == MM_MODEM_STATE_ENABLING)
		return "enabling";
	if (state == MM_MODEM_STATE_DISABLING)
		return "disabling";
	if (state == MM_MODEM_STATE_FAILED)
		return "failed";
	if (state == MM_MODEM_STATE_DISABLED ||
	    power == MM_MODEM_POWER_STATE_OFF ||
	    power == MM_MODEM_POWER_STATE_LOW)
		return "disabled";
	if (power == MM_MODEM_POWER_STATE_ON &&
	    state >= MM_MODEM_STATE_ENABLED)
		return "enabled";
	return "unknown";
}

static void
add_radio_status(struct blob_buf *buffer, FibocomModem *modem)
{
	const MMModemBand *supported_bands = NULL;
	const MMModemBand *current_bands = NULL;
	const MMModemModeCombination *supported_modes = NULL;
	MMModemMode allowed = MM_MODEM_MODE_NONE;
	MMModemMode preferred = MM_MODEM_MODE_NONE;
	guint n_supported_bands = 0;
	guint n_current_bands = 0;
	guint n_supported_modes = 0;
	gboolean current_modes_known;
	gboolean current_bands_known;
	void *radio = blobmsg_open_table(buffer, "radio");
	void *mode_array;
	void *current_mode;
	guint index;

	(void)mm_modem_peek_supported_bands(modem->modem,
		&supported_bands, &n_supported_bands);
	current_bands_known = mm_modem_peek_current_bands(modem->modem,
		&current_bands, &n_current_bands);
	(void)mm_modem_peek_supported_modes(modem->modem,
		&supported_modes, &n_supported_modes);
	current_modes_known = mm_modem_get_current_modes(modem->modem,
		&allowed, &preferred);
	blobmsg_add_string(buffer, "state", radio_state_name(modem));
	blobmsg_add_string(buffer, "power_state",
		power_state_name(mm_modem_get_power_state(modem->modem)));
	(void)add_band_array(buffer, "supported_bands", supported_bands,
		n_supported_bands);
	(void)add_band_array(buffer, "current_bands", current_bands,
		n_current_bands);
	if (!current_bands_known || n_current_bands == 0U)
		blobmsg_add_string(buffer, "band_selection", "unknown");
	else if (n_current_bands == 1U &&
		 current_bands[0] == MM_MODEM_BAND_ANY)
		blobmsg_add_string(buffer, "band_selection", "all-supported");
	else
		blobmsg_add_string(buffer, "band_selection", "explicit");
	mode_array = blobmsg_open_array(buffer, "supported_modes");
	for (index = 0; supported_modes != NULL &&
	     index < n_supported_modes && index < MAX_RADIO_MODES; index++) {
		void *entry = blobmsg_open_table(buffer, NULL);

		add_mode_array(buffer, "allowed", supported_modes[index].allowed);
		blobmsg_add_string(buffer, "preferred",
			preferred_mode_name(supported_modes[index].preferred));
		blobmsg_close_table(buffer, entry);
	}
	blobmsg_close_array(buffer, mode_array);
	current_mode = blobmsg_open_table(buffer, "current_modes");
	blobmsg_add_u8(buffer, "known", current_modes_known);
	add_mode_array(buffer, "allowed",
		current_modes_known ? allowed : MM_MODEM_MODE_NONE);
	blobmsg_add_string(buffer, "preferred", current_modes_known ?
		preferred_mode_name(preferred) : "unknown");
	blobmsg_close_table(buffer, current_mode);
	blobmsg_close_table(buffer, radio);
}

static enum FibocomNetworkBindingResult
lookup_network_binding(FibocomModem *modem,
		       struct FibocomNetworkBinding *binding)
{
	const gchar *device = mm_modem_get_device(modem->modem);

	if (device == NULL || device[0] == '\0')
		return fibocom_network_binding_lookup(NULL, binding);
	return fibocom_network_binding_lookup(device, binding);
}

static const gchar *
network_binding_state(enum FibocomNetworkBindingResult result)
{
	switch (result) {
	case FIBOCOM_NETWORK_BINDING_NONE: return "unbound";
	case FIBOCOM_NETWORK_BINDING_UNIQUE: return "bound";
	case FIBOCOM_NETWORK_BINDING_AMBIGUOUS: return "ambiguous";
	case FIBOCOM_NETWORK_BINDING_ERROR:
	default: return "unavailable";
	}
}

static void
add_network_binding_status(struct blob_buf *buffer, FibocomModem *modem)
{
	struct FibocomNetworkBinding binding;
	enum FibocomNetworkBindingResult result =
		lookup_network_binding(modem, &binding);
	void *table = blobmsg_open_table(buffer, "network_binding");

	blobmsg_add_string(buffer, "state", network_binding_state(result));
	blobmsg_add_u8(buffer, "bound",
		result == FIBOCOM_NETWORK_BINDING_UNIQUE);
	if (result == FIBOCOM_NETWORK_BINDING_UNIQUE) {
		blobmsg_add_string(buffer, "section", binding.section);
		blobmsg_add_string(buffer, "allowedmode",
			binding.has_allowedmode ? binding.allowedmode : "");
		blobmsg_add_string(buffer, "preferredmode",
			binding.has_preferredmode ? binding.preferredmode : "");
		blobmsg_add_u8(buffer, "disable_modem",
			binding.disable_modem);
		blobmsg_add_u8(buffer, "disable_modem_configured",
			binding.disable_modem_configured);
	}
	blobmsg_close_table(buffer, table);
}

static void
add_metric(struct blob_buf *buffer, const gchar *name, gdouble value)
{
	if (value != MM_SIGNAL_UNKNOWN && isfinite(value))
		blobmsg_add_double(buffer, name, value);
}

static void
add_signal_technology(struct blob_buf *buffer, const gchar *name,
		      MMSignal *signal)
{
	void *entry;

	if (signal == NULL)
		return;
	entry = blobmsg_open_table(buffer, name);
	add_metric(buffer, "rssi", mm_signal_get_rssi(signal));
	add_metric(buffer, "rscp", mm_signal_get_rscp(signal));
	add_metric(buffer, "ecio", mm_signal_get_ecio(signal));
	add_metric(buffer, "rsrp", mm_signal_get_rsrp(signal));
	add_metric(buffer, "rsrq", mm_signal_get_rsrq(signal));
	add_metric(buffer, "snr", mm_signal_get_snr(signal));
	add_metric(buffer, "sinr", mm_signal_get_sinr(signal));
	add_metric(buffer, "error_rate", mm_signal_get_error_rate(signal));
	blobmsg_close_table(buffer, entry);
}

static void
add_signal_status(struct blob_buf *buffer, FibocomModem *modem)
{
	MMModemSignal *signal_proxy = mm_object_peek_modem_signal(modem->object);
	gboolean recent = FALSE;
	guint quality = mm_modem_get_signal_quality(modem->modem, &recent);
	void *signal = blobmsg_open_table(buffer, "signal");

	blobmsg_add_u32(buffer, "quality", MIN(quality, 100U));
	blobmsg_add_u8(buffer, "recent", recent);
	if (signal_proxy != NULL) {
		add_signal_technology(buffer, "gsm",
			mm_modem_signal_peek_gsm(signal_proxy));
		add_signal_technology(buffer, "umts",
			mm_modem_signal_peek_umts(signal_proxy));
		add_signal_technology(buffer, "lte",
			mm_modem_signal_peek_lte(signal_proxy));
		add_signal_technology(buffer, "nr5g",
			mm_modem_signal_peek_nr5g(signal_proxy));
	}
	blobmsg_close_table(buffer, signal);
}

static void
add_ip_config(struct blob_buf *buffer, const gchar *name,
	      MMBearerIpConfig *config)
{
	const gchar **dns;
	void *entry;
	void *dns_array;
	guint i;

	if (config == NULL)
		return;
	entry = blobmsg_open_table(buffer, name);
	blobmsg_add_string(buffer, "method",
		ip_method_name(mm_bearer_ip_config_get_method(config)));
	add_safe_string(buffer, "address", mm_bearer_ip_config_get_address(config), 64U);
	blobmsg_add_u32(buffer, "prefix", mm_bearer_ip_config_get_prefix(config));
	add_safe_string(buffer, "gateway", mm_bearer_ip_config_get_gateway(config), 64U);
	blobmsg_add_u32(buffer, "mtu", mm_bearer_ip_config_get_mtu(config));
	dns_array = blobmsg_open_array(buffer, "dns");
	dns = mm_bearer_ip_config_get_dns(config);
	if (dns != NULL) {
		for (i = 0; dns[i] != NULL && i < 8U; i++)
			add_safe_string(buffer, NULL, dns[i], 64U);
	}
	blobmsg_close_array(buffer, dns_array);
	blobmsg_close_table(buffer, entry);
}

static void
add_bearers_status(struct blob_buf *buffer, FibocomModem *modem)
{
	GList *cursor;
	void *array;
	guint count = 0;

	blobmsg_add_string(buffer, "bearer_cache_state", modem->bearer_cache_state);
	array = blobmsg_open_array(buffer, "bearers");
	for (cursor = modem->bearers; cursor != NULL && count < 32U;
	     cursor = cursor->next, count++) {
		MMBearer *bearer = MM_BEARER(cursor->data);
		MMBearerStats *stats;
		void *entry = blobmsg_open_table(buffer, NULL);

		blobmsg_add_u8(buffer, "connected", mm_bearer_get_connected(bearer));
		blobmsg_add_u8(buffer, "suspended", mm_bearer_get_suspended(bearer));
		blobmsg_add_u8(buffer, "multiplexed", mm_bearer_get_multiplexed(bearer));
		add_safe_string(buffer, "interface", mm_bearer_get_interface(bearer),
			SAFE_PORT_MAX);
		add_ip_config(buffer, "ipv4", mm_bearer_peek_ipv4_config(bearer));
		add_ip_config(buffer, "ipv6", mm_bearer_peek_ipv6_config(bearer));
		stats = mm_bearer_peek_stats(bearer);
		if (stats != NULL) {
			void *stats_table = blobmsg_open_table(buffer, "stats");

			blobmsg_add_u32(buffer, "duration",
				mm_bearer_stats_get_duration(stats));
			blobmsg_add_u64(buffer, "rx_bytes",
				mm_bearer_stats_get_rx_bytes(stats));
			blobmsg_add_u64(buffer, "tx_bytes",
				mm_bearer_stats_get_tx_bytes(stats));
			blobmsg_close_table(buffer, stats_table);
		}
		blobmsg_close_table(buffer, entry);
	}
	blobmsg_close_array(buffer, array);
}

static int
method_get_status(struct ubus_context *context, struct ubus_object *object,
		  struct ubus_request_data *request, const char *method,
		  struct blob_attr *message)
{
	FibocomUbus *ubus = from_object(object);
	struct blob_attr *parsed[__MODEM_MAX] = {};
	const gchar *error_code;
	FibocomModem *modem;
	struct blob_buf buffer = {};
	void *cell;
	void *diagnostics;

	(void)method;
	modem = requested_modem(ubus, message, parsed, &error_code);
	if (modem == NULL)
		return send_requested_error(context, request, error_code);
	blob_buf_init(&buffer, 0);
	add_common(&buffer, TRUE);
	add_modem_identity(&buffer, modem);
	blobmsg_add_string(&buffer, "freshness", modem->live ? "fresh" : "stale");
	add_general_status(&buffer, modem);
	add_ports_status(&buffer, modem);
	add_sim_status(&buffer, modem);
	add_network(&buffer, modem);
	add_radio_status(&buffer, modem);
	add_signal_status(&buffer, modem);
	cell = blobmsg_open_table(&buffer, "cell");
	blobmsg_add_string(&buffer, "state", "unknown");
	blobmsg_add_string(&buffer, "reason",
		"standard-cell-info-not-queried");
	blobmsg_close_table(&buffer, cell);
	add_bearers_status(&buffer, modem);
	add_openwrt_unavailable(&buffer);
	add_network_binding_status(&buffer, modem);
	diagnostics = blobmsg_open_table(&buffer, "diagnostics");
	blobmsg_add_string(&buffer, "bridge_version", FIBOCOM_MM_BRIDGE_VERSION);
	add_safe_string(&buffer, "modemmanager_version",
		fibocom_bridge_manager_version(ubus->bridge), 32U);
	blobmsg_add_u8(&buffer, "read_only", FALSE);
	blobmsg_add_u8(&buffer, "raw_dbus_paths_exposed", FALSE);
	blobmsg_add_u8(&buffer, "at_commands_enabled", FALSE);
	blobmsg_close_table(&buffer, diagnostics);
	return send_buffer(context, request, &buffer);
}

static void
add_feature(struct blob_buf *buffer, const gchar *name, const gchar *state,
	    gboolean mutable, const gchar *reason)
{
	void *feature = blobmsg_open_table(buffer, name);

	blobmsg_add_string(buffer, "state", state);
	blobmsg_add_u8(buffer, "mutable", mutable);
	blobmsg_add_string(buffer, "reason", reason);
	blobmsg_close_table(buffer, feature);
}

static guint32
cooldown_remaining_ms(gint64 deadline)
{
	gint64 remaining;

	if (deadline <= 0)
		return 0U;
	remaining = deadline - g_get_monotonic_time();
	if (remaining <= 0)
		return 0U;
	remaining = (remaining + 999) / 1000;
	return remaining > G_MAXUINT32 ? G_MAXUINT32 : (guint32)remaining;
}

static guint32
advanced_retry_after_ms(FibocomUbus *ubus, FibocomModem *modem)
{
	return MAX(cooldown_remaining_ms(modem->advanced_cooldown_until),
		cooldown_remaining_ms(ubus->reprobe_cooldown_until));
}

static void
add_standard_feature(struct blob_buf *buffer, const gchar *name,
		     FibocomUbus *ubus, FibocomModem *modem,
		     gboolean attested, gboolean available,
		     const gchar *unavailable_state,
		     const gchar *available_reason,
		     const gchar *unavailable_reason,
		     gboolean ownership_allowed,
		     const gchar *ownership_reason)
{
	const gchar *state;
	const gchar *reason;
	gboolean mutable = FALSE;
	gboolean busy = FALSE;
	guint32 retry_after = advanced_retry_after_ms(ubus, modem);
	void *feature = blobmsg_open_table(buffer, name);

	if (!available) {
		state = unavailable_state;
		reason = unavailable_reason;
	} else if (!attested) {
		state = "unsupported";
		reason = "exact-l850-mbim-hardware-not-attested";
	} else if (!ownership_allowed) {
		state = "unavailable";
		reason = ownership_reason;
	} else if (modem->mutation_busy || retry_after > 0U) {
		state = "busy";
		reason = modem->mutation_busy ?
			"per-modem-mutation-in-progress" : "advanced-cooldown";
		busy = TRUE;
	} else {
		state = "available";
		reason = available_reason;
		mutable = TRUE;
	}
	blobmsg_add_string(buffer, "state", state);
	blobmsg_add_u8(buffer, "mutable", mutable);
	blobmsg_add_string(buffer, "reason", reason);
	blobmsg_add_u8(buffer, "busy", busy);
	if (retry_after > 0U)
		blobmsg_add_u32(buffer, "retry_after_ms", retry_after);
	blobmsg_close_table(buffer, feature);
}

static void
add_messaging_feature(struct blob_buf *buffer, FibocomModem *modem,
		      gboolean present, gboolean attested)
{
	const gchar *state;
	const gchar *reason;
	gboolean mutable = FALSE;
	gboolean busy = FALSE;
	void *feature = blobmsg_open_table(buffer, "messaging");

	if (!present) {
		state = "unavailable";
		reason = "dbus-interface-absent";
	} else if (!attested) {
		state = "unsupported";
		reason = "exact-l850-mbim-hardware-not-attested";
	} else if (modem->mutation_busy) {
		state = "busy";
		reason = "per-modem-mutation-in-progress";
		busy = TRUE;
	} else {
		state = "available";
		reason = "dbus-interface-present-native-api";
		mutable = TRUE;
	}
	blobmsg_add_string(buffer, "state", state);
	blobmsg_add_u8(buffer, "mutable", mutable);
	blobmsg_add_string(buffer, "reason", reason);
	blobmsg_add_u8(buffer, "busy", busy);
	blobmsg_close_table(buffer, feature);
}

static gboolean
snapshot_supported_radio_bands(FibocomModem *modem,
			       struct FibocomRadioBand *choices,
			       guint *choice_count,
			       guint *supported_families)
{
	const MMModemBand *bands = NULL;
	guint n_bands = 0;
	guint count = 0;
	guint families = FIBOCOM_RADIO_FAMILY_NONE;
	guint index;

	*choice_count = 0U;
	*supported_families = FIBOCOM_RADIO_FAMILY_NONE;
	if (!mm_modem_peek_supported_bands(modem->modem, &bands, &n_bands) ||
	    bands == NULL || n_bands == 0U || n_bands > MAX_RADIO_BANDS)
		return FALSE;
	for (index = 0; index < n_bands; index++) {
		const gchar *name;
		guint family;

		if (bands[index] == MM_MODEM_BAND_UNKNOWN)
			continue;
		name = mm_modem_band_get_string(bands[index]);
		if (!fibocom_radio_band_name_is_canonical(name))
			continue;
		family = fibocom_radio_band_family(name);
		choices[count].name = name;
		choices[count].value = (guint)bands[index];
		choices[count].family = family;
		families |= family;
		count++;
	}
	*choice_count = count;
	*supported_families = families;
	return count > 0U;
}

static gboolean
current_radio_families(FibocomModem *modem, guint supported_families,
		       guint *allowed_families)
{
	MMModemMode allowed = MM_MODEM_MODE_NONE;
	MMModemMode preferred = MM_MODEM_MODE_NONE;
	guint families;

	if (!mm_modem_get_current_modes(modem->modem, &allowed, &preferred))
		return FALSE;
	(void)preferred;
	families = allowed == MM_MODEM_MODE_ANY ? supported_families :
		mode_family_mask(allowed);
	if (families == FIBOCOM_RADIO_FAMILY_NONE)
		return FALSE;
	*allowed_families = families;
	return TRUE;
}

static int
method_get_capabilities(struct ubus_context *context,
			struct ubus_object *object,
			struct ubus_request_data *request, const char *method,
			struct blob_attr *message)
{
	FibocomUbus *ubus = from_object(object);
	struct blob_attr *parsed[__MODEM_MAX] = {};
	const gchar *error_code;
	FibocomModem *modem;
	MMModemMessaging *messaging;
	const MMModemModeCombination *modes = NULL;
	struct FibocomRadioBand band_choices[MAX_RADIO_BANDS];
	struct FibocomNetworkBinding binding;
	enum FibocomNetworkBindingResult binding_result;
	guint band_choice_count = 0;
	guint supported_families = FIBOCOM_RADIO_FAMILY_NONE;
	guint allowed_families = FIBOCOM_RADIO_FAMILY_NONE;
	guint n_modes = 0;
	guint slots;
	gboolean attested;
	gboolean bands_available;
	gboolean modes_known;
	gboolean radio_ownership_allowed;
	const gchar *radio_ownership_reason = "standard-method";
	struct blob_buf buffer = {};
	void *capabilities;

	(void)method;
	modem = requested_modem(ubus, message, parsed, &error_code);
	if (modem == NULL)
		return send_requested_error(context, request, error_code);
	messaging = mm_object_peek_modem_messaging(modem->object);
	attested = fibocom_modem_attest_mutation_target(modem);
	bands_available = snapshot_supported_radio_bands(modem, band_choices,
		&band_choice_count, &supported_families);
	modes_known = current_radio_families(modem, supported_families,
		&allowed_families);
	(void)allowed_families;
	(void)mm_modem_peek_supported_modes(modem->modem, &modes, &n_modes);
	(void)modes;
	slots = sim_slot_count(modem);
	binding_result = lookup_network_binding(modem, &binding);
	(void)binding;
	radio_ownership_allowed =
		binding_result == FIBOCOM_NETWORK_BINDING_NONE;
	if (binding_result == FIBOCOM_NETWORK_BINDING_UNIQUE)
		radio_ownership_reason = "network-interface-owns-radio";
	else if (binding_result == FIBOCOM_NETWORK_BINDING_AMBIGUOUS)
		radio_ownership_reason = "network-interface-binding-ambiguous";
	else if (binding_result == FIBOCOM_NETWORK_BINDING_ERROR)
		radio_ownership_reason = "network-interface-binding-unavailable";
	blob_buf_init(&buffer, 0);
	add_common(&buffer, TRUE);
	add_modem_identity(&buffer, modem);
	capabilities = blobmsg_open_table(&buffer, "capabilities");
	add_feature(&buffer, "mbim_data",
		g_str_equal(fibocom_modem_composition(modem), "mbim") ?
		"available" : "unavailable",
		FALSE, "observed-modemmanager-composition");
	add_feature(&buffer, "ncm_data", "unsupported",
		FALSE, "missing-l850-modemmanager-ncm-backend");
	add_messaging_feature(&buffer, modem, messaging != NULL, attested);
	add_standard_feature(&buffer, "bands", ubus, modem, attested,
		bands_available && modes_known, "unknown",
		"supported-bands-and-current-modes-present",
		bands_available ? "current-modes-not-advertised" :
		"supported-bands-not-advertised", TRUE, "");
	add_feature(&buffer, "modes",
		n_modes > 0U || modes_known ? "available" : "unknown",
		FALSE, n_modes > 0U || modes_known ? "network-uci-owned" :
		"supported-modes-not-advertised");
	add_standard_feature(&buffer, "radio", ubus, modem, attested, TRUE,
		"unavailable", "standard-method", "standard-method-unavailable",
		radio_ownership_allowed, radio_ownership_reason);
	add_standard_feature(&buffer, "reset", ubus, modem, attested, TRUE,
		"unavailable", "standard-method", "standard-method-unavailable",
		TRUE, "");
	add_standard_feature(&buffer, "sim_slot", ubus, modem, attested,
		slots > 1U, "unsupported", "multiple-slots-advertised",
		"single-slot", TRUE, "");
	add_feature(&buffer, "cell_info", "unknown", FALSE,
		"standard-cell-info-not-queried");
	add_feature(&buffer, "l850_cell_extension", "unavailable",
		FALSE, "expert-build-disabled");
	blobmsg_close_table(&buffer, capabilities);
	return send_buffer(context, request, &buffer);
}

static int
method_list_sms(struct ubus_context *context, struct ubus_object *object,
		struct ubus_request_data *request, const char *method,
		struct blob_attr *message)
{
	FibocomUbus *ubus = from_object(object);
	struct blob_attr *parsed[__LIST_SMS_MAX] = {};
	g_autoptr(GPtrArray) snapshot = NULL;
	g_autoptr(GPtrArray) filtered = NULL;
	FibocomModem *modem;
	const gchar *folder = "all";
	const gchar *cursor = "";
	guint limit = SMS_DEFAULT_LIMIT;
	guint start = 0;
	guint end;
	guint i;
	gboolean cursor_found = FALSE;
	gboolean has_more;
	struct blob_buf buffer = {};
	void *messages;

	(void)method;
	if (!parse_exact_fields(message, list_sms_policy, __LIST_SMS_MAX,
		G_GUINT64_CONSTANT(1) << LIST_SMS_MODEM_ID, parsed))
		return send_error(context, request, "invalid_argument",
			"list_sms accepts only modem_id, folder, limit, and cursor",
			FALSE);
	if (!fibocom_identity_is_valid(
		blobmsg_get_string(parsed[LIST_SMS_MODEM_ID])))
		return send_error(context, request, "invalid_argument",
			"modem_id is invalid", FALSE);
	if (parsed[LIST_SMS_FOLDER] != NULL)
		folder = blobmsg_get_string(parsed[LIST_SMS_FOLDER]);
	if (!sms_folder_is_valid(folder))
		return send_error(context, request, "invalid_argument",
			"folder must be all, inbox, outbox, draft, or unknown", FALSE);
	if (parsed[LIST_SMS_LIMIT] != NULL)
		limit = blobmsg_get_u32(parsed[LIST_SMS_LIMIT]);
	if (limit < 1U || limit > SMS_MAX_LIMIT)
		return send_error(context, request, "invalid_argument",
			"limit must be between 1 and 100", FALSE);
	if (parsed[LIST_SMS_CURSOR] != NULL)
		cursor = blobmsg_get_string(parsed[LIST_SMS_CURSOR]);
	if (cursor[0] != '\0' && !fibocom_sms_identity_is_valid(cursor))
		return send_error(context, request, "invalid_argument",
			"cursor is invalid", FALSE);
	modem = fibocom_bridge_find_modem(ubus->bridge,
		blobmsg_get_string(parsed[LIST_SMS_MODEM_ID]));
	if (modem == NULL)
		return send_error(context, request,
			fibocom_bridge_manager_available(ubus->bridge) ?
			"stale_identity" : "dependency_unavailable",
			fibocom_bridge_manager_available(ubus->bridge) ?
			"modem identity is no longer live" :
			"ModemManager is not available",
			!fibocom_bridge_manager_available(ubus->bridge));
	if (modem->messaging == NULL)
		return send_error(context, request, "unsupported",
			"Messaging is not available on this modem", FALSE);
	if (!g_str_equal(modem->sms_cache_state, "ready") &&
	    !g_str_equal(modem->sms_cache_state, "ready-truncated")) {
		fibocom_modem_refresh_sms(modem);
		return send_error(context, request, "not_ready",
			"SMS inventory is still loading", TRUE);
	}
	snapshot = fibocom_modem_snapshot_sms(modem);
	filtered = g_ptr_array_new_with_free_func(
		(GDestroyNotify)fibocom_sms_unref);
	for (i = 0; i < snapshot->len; i++) {
		FibocomSms *entry = g_ptr_array_index(snapshot, i);

		if (g_str_equal(folder, "all") ||
		    g_str_equal(folder, sms_folder(entry->sms)))
			g_ptr_array_add(filtered, fibocom_sms_ref(entry));
	}
	g_ptr_array_sort(filtered, sms_compare);
	if (cursor[0] != '\0') {
		for (i = 0; i < filtered->len; i++) {
			FibocomSms *entry = g_ptr_array_index(filtered, i);

			if (g_str_equal(entry->sms_id, cursor)) {
				start = i + 1U;
				cursor_found = TRUE;
				break;
			}
		}
		if (!cursor_found)
			return send_error(context, request, "stale_cursor",
				"cursor is no longer present in this SMS view", FALSE);
	}
	end = MIN(filtered->len, start + limit);
	has_more = end < filtered->len;
	blob_buf_init(&buffer, 0);
	add_common(&buffer, TRUE);
	add_modem_identity(&buffer, modem);
	blobmsg_add_u32(&buffer, "messaging_generation",
		modem->messaging_generation);
	blobmsg_add_u64(&buffer, "revision", modem->sms_revision);
	blobmsg_add_string(&buffer, "cache_state", modem->sms_cache_state);
	blobmsg_add_u8(&buffer, "cache_truncated", modem->sms_cache_truncated);
	blobmsg_add_string(&buffer, "folder", folder);
	blobmsg_add_u32(&buffer, "limit", limit);
	messages = blobmsg_open_array(&buffer, "messages");
	for (i = start; i < end; i++)
		add_sms_entry(&buffer, g_ptr_array_index(filtered, i));
	blobmsg_close_array(&buffer, messages);
	blobmsg_add_u8(&buffer, "has_more", has_more);
	blobmsg_add_string(&buffer, "next_cursor", has_more && end > start ?
		((FibocomSms *)g_ptr_array_index(filtered, end - 1U))->sms_id : "");
	return send_buffer(context, request, &buffer);
}

static const gchar *
sms_operation_stale_code(SmsOperation *operation)
{
	if (!operation->modem->live)
		return "device_gone";
	if (operation->modem->generation != operation->generation)
		return "stale_generation";
	if (operation->modem->messaging_generation !=
	    operation->messaging_generation ||
	    operation->modem->messaging != operation->messaging)
		return "stale_messaging_generation";
	return NULL;
}

static gboolean
sms_operation_timeout(gpointer user_data)
{
	SmsOperation *operation = user_data;

	operation->timeout_source = 0;
	operation->timed_out = TRUE;
	g_cancellable_cancel(operation->cancellable);
	return G_SOURCE_REMOVE;
}

static void
sms_operation_arm_timeout(SmsOperation *operation)
{
	if (operation->timeout_source != 0)
		g_source_remove(operation->timeout_source);
	operation->timed_out = FALSE;
	operation->timeout_source = g_timeout_add_seconds(
		SMS_OPERATION_TIMEOUT_SECONDS, sms_operation_timeout, operation);
}

static void
sms_operation_free(SmsOperation *operation)
{
	FibocomUbus *ubus = operation->ubus;

	if (operation->timeout_source != 0)
		g_source_remove(operation->timeout_source);
	fibocom_modem_release_sms_id(operation->modem, operation->sms_id);
	if (operation->modem->mutation_kind == FIBOCOM_MUTATION_SMS &&
	    operation->modem->mutation_cancellable ==
	    operation->cancellable) {
		g_clear_object(&operation->modem->mutation_cancellable);
		operation->modem->mutation_busy = FALSE;
		operation->modem->mutation_kind = FIBOCOM_MUTATION_NONE;
	}
	g_clear_object(&operation->properties);
	g_clear_object(&operation->sms);
	if (operation->sms_entry != NULL)
		fibocom_sms_unref(operation->sms_entry);
	g_clear_object(&operation->messaging);
	g_clear_object(&operation->cancellable);
	fibocom_modem_unref(operation->modem);
	g_free(operation->client_token);
	g_free(operation->sms_id);
	if (ubus != NULL && ubus->sms_operations != NULL)
		g_hash_table_remove(ubus->sms_operations, operation);
	g_free(operation);
	fibocom_ubus_unref_internal(ubus);
}

static void
sms_operation_complete_buffer(SmsOperation *operation,
			      struct blob_buf *buffer)
{
	if (operation->deferred && operation->ubus->connected &&
	    operation->ubus->context_initialized && !operation->ubus->stopping) {
		(void)ubus_send_reply(&operation->ubus->context,
			&operation->request, buffer->head);
		ubus_complete_deferred_request(&operation->ubus->context,
			&operation->request, UBUS_STATUS_OK);
	}
	operation->deferred = FALSE;
	blob_buf_free(buffer);
	sms_operation_free(operation);
}

static void
sms_operation_complete_error(SmsOperation *operation, const gchar *code,
			     const gchar *message, gboolean retryable)
{
	struct blob_buf buffer = {};
	void *error;

	if (operation->type == SMS_OPERATION_SEND) {
		if (sms_operation_stale_code(operation) == NULL &&
		    g_str_equal(code, "outcome_unknown"))
			sms_dedupe_store(operation->modem,
				operation->client_token, operation->request_digest,
				FALSE, operation->sms_id, NULL, code, FALSE);
		else
			sms_dedupe_remove(operation->modem,
				operation->client_token);
	}
	blob_buf_init(&buffer, 0);
	add_common(&buffer, FALSE);
	error = blobmsg_open_table(&buffer, "error");
	blobmsg_add_string(&buffer, "code", code);
	blobmsg_add_string(&buffer, "message", message);
	blobmsg_add_u8(&buffer, "retryable", retryable);
	blobmsg_close_table(&buffer, error);
	sms_operation_complete_buffer(operation, &buffer);
}

static void
sms_operation_complete_normalized_error(SmsOperation *operation,
					GError *error)
{
	SmsNormalizedError normalized = normalize_sms_error(error, operation);

	sms_operation_complete_error(operation, normalized.code,
		normalized.message, normalized.retryable);
}

static void
sms_operation_complete_send(SmsOperation *operation, gboolean store_dedupe)
{
	const gchar *state = sms_state_name(mm_sms_get_state(operation->sms));
	struct blob_buf buffer = {};

	if (store_dedupe)
		sms_dedupe_store(operation->modem, operation->client_token,
			operation->request_digest, TRUE, operation->sms_id, state,
			NULL, FALSE);
	blob_buf_init(&buffer, 0);
	add_common(&buffer, TRUE);
	add_modem_identity(&buffer, operation->modem);
	blobmsg_add_u32(&buffer, "messaging_generation",
		operation->messaging_generation);
	blobmsg_add_string(&buffer, "sms_id", operation->sms_id);
	blobmsg_add_string(&buffer, "state", state);
	blobmsg_add_u8(&buffer, "deduplicated", FALSE);
	sms_operation_complete_buffer(operation, &buffer);
}

static void
sms_operation_complete_delete(SmsOperation *operation)
{
	struct blob_buf buffer = {};

	blob_buf_init(&buffer, 0);
	add_common(&buffer, TRUE);
	add_modem_identity(&buffer, operation->modem);
	blobmsg_add_u32(&buffer, "messaging_generation",
		operation->messaging_generation);
	blobmsg_add_string(&buffer, "sms_id", operation->sms_id);
	blobmsg_add_u8(&buffer, "deleted", TRUE);
	sms_operation_complete_buffer(operation, &buffer);
}

static SmsOperation *
sms_operation_new(FibocomUbus *ubus, FibocomModem *modem,
		  SmsOperationType type, struct ubus_context *context,
		  struct ubus_request_data *request)
{
	SmsOperation *operation = g_new0(SmsOperation, 1);

	operation->type = type;
	operation->ubus = fibocom_ubus_ref_internal(ubus);
	operation->modem = fibocom_modem_ref(modem);
	operation->messaging = g_object_ref(modem->messaging);
	operation->cancellable = g_cancellable_new();
	operation->generation = modem->generation;
	operation->messaging_generation = modem->messaging_generation;
	ubus_defer_request(context, request, &operation->request);
	operation->deferred = TRUE;
	g_hash_table_add(ubus->sms_operations, operation);
	sms_operation_arm_timeout(operation);
	modem->mutation_busy = TRUE;
	modem->mutation_kind = FIBOCOM_MUTATION_SMS;
	modem->mutation_cancellable =
		g_object_ref(operation->cancellable);
	return operation;
}

static void
sms_send_ready(GObject *source, GAsyncResult *result, gpointer user_data)
{
	SmsOperation *operation = user_data;
	g_autoptr(GError) error = NULL;
	const gchar *stale;
	gboolean sent;

	sent = mm_sms_send_finish(MM_SMS(source), result, &error);
	if (!sent) {
		sms_operation_complete_normalized_error(operation, error);
		return;
	}
	stale = sms_operation_stale_code(operation);
	if (stale != NULL) {
		/*
		 * Send() already succeeded. Report that authoritative result for the
		 * captured request identity, but never place an old-epoch result in
		 * the current modem's dedupe queue.
		 */
		sms_operation_complete_send(operation, FALSE);
		return;
	}
	fibocom_modem_refresh_sms(operation->modem);
	sms_operation_complete_send(operation, TRUE);
}

static void
sms_create_ready(GObject *source, GAsyncResult *result, gpointer user_data)
{
	SmsOperation *operation = user_data;
	g_autoptr(GError) error = NULL;
	const gchar *stale;
	MMSms *sms;

	sms = mm_modem_messaging_create_finish(MM_MODEM_MESSAGING(source),
		result, &error);
	g_clear_object(&operation->properties);
	if (sms == NULL) {
		sms_operation_complete_normalized_error(operation, error);
		return;
	}
	operation->sms = sms;
	stale = sms_operation_stale_code(operation);
	if (stale != NULL) {
		sms_operation_complete_error(operation, stale,
			sms_error_message(stale), FALSE);
		return;
	}
	operation->sms_entry = fibocom_modem_admit_reserved_sms(
		operation->modem, operation->sms, operation->sms_id);
	if (operation->sms_entry == NULL) {
		sms_operation_complete_error(operation, "internal_error",
			sms_error_message("internal_error"), FALSE);
		return;
	}
	stale = sms_operation_stale_code(operation);
	if (stale != NULL) {
		sms_operation_complete_error(operation, stale,
			sms_error_message(stale), FALSE);
		return;
	}
	if (!fibocom_modem_attest_mutation_target(operation->modem)) {
		sms_operation_complete_error(operation, "unsupported",
			sms_error_message("unsupported"), FALSE);
		return;
	}
	operation->send_dispatched = TRUE;
	sms_operation_arm_timeout(operation);
	mm_sms_send(operation->sms, operation->cancellable, sms_send_ready,
		operation);
}

static void
sms_delete_ready(GObject *source, GAsyncResult *result, gpointer user_data)
{
	SmsOperation *operation = user_data;
	g_autoptr(GError) error = NULL;
	const gchar *stale;
	gboolean deleted;

	deleted = mm_modem_messaging_delete_finish(MM_MODEM_MESSAGING(source),
		result, &error);
	if (!deleted) {
		sms_operation_complete_normalized_error(operation, error);
		return;
	}
	stale = sms_operation_stale_code(operation);
	if (stale == NULL)
		fibocom_modem_refresh_sms(operation->modem);
	/* Delete() succeeded authoritatively even if its captured epoch is stale. */
	sms_operation_complete_delete(operation);
}

static FibocomModem *
sms_mutation_modem(FibocomUbus *ubus, const gchar *modem_id,
		   guint32 generation, guint32 messaging_generation,
		   const gchar **error_code)
{
	FibocomModem *modem;

	modem = fibocom_bridge_find_modem(ubus->bridge, modem_id);
	if (modem == NULL) {
		*error_code = fibocom_bridge_manager_available(ubus->bridge) ?
			"stale_identity" : "dependency_unavailable";
		return NULL;
	}
	if (modem->generation != generation) {
		*error_code = "stale_generation";
		return NULL;
	}
	if (modem->messaging_generation != messaging_generation) {
		*error_code = "stale_messaging_generation";
		return NULL;
	}
	if (modem->messaging == NULL) {
		*error_code = "unsupported";
		return NULL;
	}
	if (!fibocom_modem_attest_mutation_target(modem)) {
		*error_code = "unsupported";
		return NULL;
	}
	*error_code = NULL;
	return modem;
}

static int
send_sms_mutation_lookup_error(struct ubus_context *context,
			       struct ubus_request_data *request,
			       const gchar *error_code)
{
	gboolean retryable = g_str_equal(error_code, "dependency_unavailable") ||
		g_str_equal(error_code, "not_ready") ||
		g_str_equal(error_code, "busy");

	return send_error(context, request, error_code,
		sms_error_message(error_code), retryable);
}

static int
send_cached_sms_result(struct ubus_context *context,
		       struct ubus_request_data *request, FibocomModem *modem,
		       FibocomSmsDedupe *cached)
{
	struct blob_buf buffer = {};

	if (!cached->ok)
		return send_error(context, request, cached->error_code,
			sms_error_message(cached->error_code), cached->retryable);
	blob_buf_init(&buffer, 0);
	add_common(&buffer, TRUE);
	add_modem_identity(&buffer, modem);
	blobmsg_add_u32(&buffer, "messaging_generation",
		modem->messaging_generation);
	blobmsg_add_string(&buffer, "sms_id",
		cached->sms_id != NULL ? cached->sms_id : "");
	blobmsg_add_string(&buffer, "state",
		cached->state != NULL ? cached->state : "unknown");
	blobmsg_add_u8(&buffer, "deduplicated", TRUE);
	return send_buffer(context, request, &buffer);
}

static int
method_send_sms(struct ubus_context *context, struct ubus_object *object,
		struct ubus_request_data *request, const char *method,
		struct blob_attr *message)
{
	FibocomUbus *ubus = from_object(object);
	struct blob_attr *parsed[__SEND_SMS_MAX] = {};
	const gchar *modem_id;
	const gchar *recipient;
	const gchar *text;
	const gchar *client_token;
	const gchar *error_code;
	FibocomSmsDedupe *cached;
	FibocomModem *modem;
	SmsOperation *operation;
	MMSmsProperties *properties;
	g_autofree gchar *sms_id = NULL;
	guint8 request_digest[FIBOCOM_SMS_REQUEST_DIGEST_LEN];
	guint32 generation;
	guint32 messaging_generation;

	(void)method;
	if (!parse_exact_fields(message, send_sms_policy, __SEND_SMS_MAX,
		(G_GUINT64_CONSTANT(1) << __SEND_SMS_MAX) - 1U, parsed))
		return send_error(context, request, "invalid_argument",
			"send_sms requires exactly modem_id, generation, "
			"messaging_generation, recipient, text, and client_token",
			FALSE);
	modem_id = blobmsg_get_string(parsed[SEND_SMS_MODEM_ID]);
	recipient = blobmsg_get_string(parsed[SEND_SMS_RECIPIENT]);
	text = blobmsg_get_string(parsed[SEND_SMS_TEXT]);
	client_token = blobmsg_get_string(parsed[SEND_SMS_CLIENT_TOKEN]);
	if (!fibocom_identity_is_valid(modem_id) ||
	    !fibocom_sms_recipient_is_valid(recipient) ||
	    !fibocom_sms_outbound_text_is_valid(text) ||
	    !fibocom_sms_operation_token_is_valid(client_token))
		return send_error(context, request, "invalid_argument",
			"recipient, text, modem_id, or client_token is invalid",
			FALSE);
	if (!fibocom_sms_request_digest(recipient, text, request_digest))
		return send_error(context, request, "internal_error",
			sms_error_message("internal_error"), FALSE);
	generation = blobmsg_get_u32(parsed[SEND_SMS_GENERATION]);
	messaging_generation = blobmsg_get_u32(
		parsed[SEND_SMS_MESSAGING_GENERATION]);
	modem = sms_mutation_modem(ubus, modem_id, generation,
		messaging_generation, &error_code);
	if (modem == NULL)
		return send_sms_mutation_lookup_error(context, request, error_code);
	cached = sms_dedupe_find(modem, client_token);
	if (cached != NULL &&
	    !sms_dedupe_digest_matches(cached, request_digest))
		return send_error(context, request, "invalid_argument",
			"client_token was already used for different SMS content",
			FALSE);
	if (cached != NULL)
		return send_cached_sms_result(context, request, modem, cached);
	if (modem->mutation_busy)
		return send_sms_mutation_lookup_error(context, request, "busy");
	sms_id = fibocom_modem_reserve_sms_id(modem);
	if (sms_id == NULL)
		return send_sms_mutation_lookup_error(context, request,
			"internal_error");
	properties = mm_sms_properties_new();
	mm_sms_properties_set_number(properties, recipient);
	mm_sms_properties_set_text(properties, text);
	operation = sms_operation_new(ubus, modem, SMS_OPERATION_SEND,
		context, request);
	operation->properties = properties;
	operation->client_token = g_strdup(client_token);
	operation->sms_id = g_steal_pointer(&sms_id);
	memcpy(operation->request_digest, request_digest,
		FIBOCOM_SMS_REQUEST_DIGEST_LEN);
	operation->has_request_digest = TRUE;
	sms_dedupe_store(modem, client_token, operation->request_digest, FALSE,
		operation->sms_id, NULL, "busy", TRUE);
	if (!fibocom_modem_attest_mutation_target(modem)) {
		sms_operation_complete_error(operation, "unsupported",
			sms_error_message("unsupported"), FALSE);
		return UBUS_STATUS_OK;
	}
	mm_modem_messaging_create(operation->messaging, operation->properties,
		operation->cancellable, sms_create_ready, operation);
	return UBUS_STATUS_OK;
}

static int
method_delete_sms(struct ubus_context *context, struct ubus_object *object,
		  struct ubus_request_data *request, const char *method,
		  struct blob_attr *message)
{
	FibocomUbus *ubus = from_object(object);
	struct blob_attr *parsed[__DELETE_SMS_MAX] = {};
	const gchar *modem_id;
	const gchar *sms_id;
	const gchar *sms_path;
	const gchar *error_code;
	FibocomModem *modem;
	FibocomSms *entry;
	SmsOperation *operation;
	guint32 generation;
	guint32 messaging_generation;

	(void)method;
	if (!parse_exact_fields(message, delete_sms_policy, __DELETE_SMS_MAX,
		(G_GUINT64_CONSTANT(1) << __DELETE_SMS_MAX) - 1U, parsed))
		return send_error(context, request, "invalid_argument",
			"delete_sms requires exactly modem_id, generation, "
			"messaging_generation, sms_id, and confirm", FALSE);
	modem_id = blobmsg_get_string(parsed[DELETE_SMS_MODEM_ID]);
	sms_id = blobmsg_get_string(parsed[DELETE_SMS_SMS_ID]);
	if (!fibocom_identity_is_valid(modem_id) ||
	    !fibocom_sms_identity_is_valid(sms_id) ||
	    !blobmsg_get_bool(parsed[DELETE_SMS_CONFIRM]))
		return send_error(context, request, "invalid_argument",
			"valid modem_id, sms_id, and confirm=true are required",
			FALSE);
	generation = blobmsg_get_u32(parsed[DELETE_SMS_GENERATION]);
	messaging_generation = blobmsg_get_u32(
		parsed[DELETE_SMS_MESSAGING_GENERATION]);
	modem = sms_mutation_modem(ubus, modem_id, generation,
		messaging_generation, &error_code);
	if (modem == NULL)
		return send_sms_mutation_lookup_error(context, request, error_code);
	if (!g_str_equal(modem->sms_cache_state, "ready") &&
	    !g_str_equal(modem->sms_cache_state, "ready-truncated")) {
		fibocom_modem_refresh_sms(modem);
		return send_sms_mutation_lookup_error(context, request, "not_ready");
	}
	if (modem->mutation_busy)
		return send_sms_mutation_lookup_error(context, request, "busy");
	entry = fibocom_modem_find_sms(modem, sms_id);
	if (entry == NULL)
		return send_sms_mutation_lookup_error(context, request, "not_found");
	if (mm_sms_get_state(entry->sms) == MM_SMS_STATE_SENDING) {
		fibocom_sms_unref(entry);
		return send_sms_mutation_lookup_error(context, request, "busy");
	}
	sms_path = mm_sms_get_path(entry->sms);
	if (sms_path == NULL || !g_variant_is_object_path(sms_path)) {
		fibocom_sms_unref(entry);
		return send_sms_mutation_lookup_error(context, request,
			"internal_error");
	}
	operation = sms_operation_new(ubus, modem, SMS_OPERATION_DELETE,
		context, request);
	operation->sms_entry = entry;
	operation->sms_id = g_strdup(sms_id);
	if (!fibocom_modem_attest_mutation_target(modem)) {
		sms_operation_complete_error(operation, "unsupported",
			sms_error_message("unsupported"), FALSE);
		return UBUS_STATUS_OK;
	}
	mm_modem_messaging_delete(operation->messaging, sms_path,
		operation->cancellable, sms_delete_ready, operation);
	return UBUS_STATUS_OK;
}

typedef struct {
	const gchar *code;
	const gchar *message;
	gboolean retryable;
} AdvancedNormalizedError;

static const gchar *
advanced_error_message(const gchar *code)
{
	if (g_str_equal(code, "device_gone"))
		return "The modem was removed during the operation";
	if (g_str_equal(code, "stale_identity"))
		return "The modem identity is no longer live";
	if (g_str_equal(code, "stale_generation"))
		return "The modem generation changed; refresh before retrying";
	if (g_str_equal(code, "unsupported"))
		return "The live modem does not expose this reviewed operation";
	if (g_str_equal(code, "not_ready"))
		return "The modem is not ready for this operation";
	if (g_str_equal(code, "busy"))
		return "Another modem mutation or cooldown is in progress";
	if (g_str_equal(code, "outcome_unknown"))
		return "The operation outcome is unknown; refresh before any retry";
	if (g_str_equal(code, "permission_denied"))
		return "ModemManager denied this operation";
	if (g_str_equal(code, "dependency_unavailable"))
		return "A required ModemManager or network configuration dependency is unavailable";
	if (g_str_equal(code, "ambiguous_device"))
		return "More than one matching network interface owns this modem";
	if (g_str_equal(code, "managed_by_netifd"))
		return "The bound netifd interface owns radio state; change it in Network Interfaces";
	if (g_str_equal(code, "invalid_argument"))
		return "The operation arguments are invalid";
	return "The modem operation failed";
}

static int
send_advanced_error(struct ubus_context *context,
		    struct ubus_request_data *request, const gchar *code,
		    gboolean retryable, guint32 retry_after_ms)
{
	struct blob_buf buffer = {};
	void *error;

	blob_buf_init(&buffer, 0);
	add_common(&buffer, FALSE);
	error = blobmsg_open_table(&buffer, "error");
	blobmsg_add_string(&buffer, "code", code);
	blobmsg_add_string(&buffer, "message", advanced_error_message(code));
	blobmsg_add_u8(&buffer, "retryable", retryable);
	if (retry_after_ms > 0U)
		blobmsg_add_u32(&buffer, "retry_after_ms", retry_after_ms);
	blobmsg_close_table(&buffer, error);
	return send_buffer(context, request, &buffer);
}

static FibocomModem *
advanced_mutation_modem(FibocomUbus *ubus, const gchar *modem_id,
			guint32 generation, const gchar **error_code,
			guint32 *retry_after_ms)
{
	FibocomModem *modem;

	*retry_after_ms = 0U;
	modem = fibocom_bridge_find_modem(ubus->bridge, modem_id);
	if (modem == NULL) {
		*error_code = fibocom_bridge_manager_available(ubus->bridge) ?
			"stale_identity" : "dependency_unavailable";
		return NULL;
	}
	if (modem->generation != generation) {
		*error_code = "stale_generation";
		return NULL;
	}
	if (!fibocom_modem_attest_mutation_target(modem)) {
		*error_code = "unsupported";
		return NULL;
	}
	*retry_after_ms = advanced_retry_after_ms(ubus, modem);
	if (modem->mutation_busy || *retry_after_ms > 0U) {
		*error_code = "busy";
		return NULL;
	}
	*error_code = NULL;
	return modem;
}

static gboolean
parse_requested_bands(struct blob_attr *array, const gchar **requested,
		      guint *requested_count)
{
	struct blob_attr *item;
	unsigned int remaining;
	guint count = 0;

	if (array == NULL || blobmsg_type(array) != BLOBMSG_TYPE_ARRAY)
		return FALSE;
	blobmsg_for_each_attr(item, array, remaining) {
		const gchar *name = blobmsg_name(item);

		if (count >= FIBOCOM_RADIO_REQUEST_BANDS_MAX ||
		    blobmsg_type(item) != BLOBMSG_TYPE_STRING ||
		    !blob_string_is_canonical(item) ||
		    (name != NULL && name[0] != '\0'))
			return FALSE;
		requested[count] = blobmsg_get_string(item);
		if (!fibocom_radio_band_name_is_canonical(requested[count]))
			return FALSE;
		count++;
	}
	if (count == 0U)
		return FALSE;
	*requested_count = count;
	return TRUE;
}

static const gchar *
advanced_operation_name(AdvancedOperationType type)
{
	switch (type) {
	case ADVANCED_OPERATION_BANDS: return "set_bands";
	case ADVANCED_OPERATION_RADIO_ENABLE: return "enable_radio";
	case ADVANCED_OPERATION_RADIO_DISABLE: return "disable_radio";
	case ADVANCED_OPERATION_RESET: return "reset";
	case ADVANCED_OPERATION_SIM_SLOT: return "set_primary_sim_slot";
	default: return "unknown";
	}
}

static void
advanced_operation_apply_cooldown(AdvancedOperation *operation)
{
	gint64 deadline = g_get_monotonic_time() +
		((gint64)operation->cooldown_seconds * G_USEC_PER_SEC);

	if (operation->type == ADVANCED_OPERATION_RESET ||
	    operation->type == ADVANCED_OPERATION_SIM_SLOT) {
		operation->ubus->reprobe_cooldown_until = MAX(
			operation->ubus->reprobe_cooldown_until, deadline);
	} else {
		operation->modem->advanced_cooldown_until = MAX(
			operation->modem->advanced_cooldown_until, deadline);
	}
}

static const gchar *
advanced_operation_stale_code(AdvancedOperation *operation,
			      GObject *source)
{
	if (!operation->modem->live)
		return "device_gone";
	if (operation->modem->generation != operation->generation ||
	    source != G_OBJECT(operation->modem->modem))
		return "stale_generation";
	return NULL;
}

static gboolean
advanced_operation_timeout(gpointer user_data)
{
	AdvancedOperation *operation = user_data;

	operation->timeout_source = 0;
	operation->timed_out = TRUE;
	g_cancellable_cancel(operation->cancellable);
	return G_SOURCE_REMOVE;
}

static void
advanced_operation_free(AdvancedOperation *operation)
{
	FibocomUbus *ubus = operation->ubus;

	if (operation->timeout_source != 0)
		g_source_remove(operation->timeout_source);
	advanced_operation_apply_cooldown(operation);
	if (operation->modem->mutation_kind == FIBOCOM_MUTATION_ADVANCED &&
	    operation->modem->mutation_cancellable == operation->cancellable) {
		g_clear_object(&operation->modem->mutation_cancellable);
		operation->modem->mutation_busy = FALSE;
		operation->modem->mutation_kind = FIBOCOM_MUTATION_NONE;
	}
	g_clear_object(&operation->cancellable);
	g_free(operation->bands);
	fibocom_modem_unref(operation->modem);
	if (ubus != NULL && ubus->advanced_operations != NULL)
		g_hash_table_remove(ubus->advanced_operations, operation);
	g_free(operation);
	fibocom_ubus_unref_internal(ubus);
}

static void
advanced_operation_complete_buffer(AdvancedOperation *operation,
				   struct blob_buf *buffer)
{
	if (operation->deferred && operation->ubus->connected &&
	    operation->ubus->context_initialized && !operation->ubus->stopping) {
		(void)ubus_send_reply(&operation->ubus->context,
			&operation->request, buffer->head);
		ubus_complete_deferred_request(&operation->ubus->context,
			&operation->request, UBUS_STATUS_OK);
	}
	operation->deferred = FALSE;
	blob_buf_free(buffer);
	advanced_operation_free(operation);
}

static void
advanced_operation_complete_error(AdvancedOperation *operation,
				  const gchar *code, const gchar *message,
				  gboolean retryable)
{
	struct blob_buf buffer = {};
	void *error;

	blob_buf_init(&buffer, 0);
	add_common(&buffer, FALSE);
	error = blobmsg_open_table(&buffer, "error");
	blobmsg_add_string(&buffer, "code", code);
	blobmsg_add_string(&buffer, "message", message);
	blobmsg_add_u8(&buffer, "retryable", retryable);
	blobmsg_close_table(&buffer, error);
	advanced_operation_complete_buffer(operation, &buffer);
}

static gboolean
advanced_error_has_unknown_outcome(GError *error, const gchar *remote)
{
	if (error == NULL)
		return FALSE;
	return g_error_matches(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT) ||
		g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED) ||
		g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CLOSED) ||
		g_error_matches(error, G_IO_ERROR, G_IO_ERROR_BROKEN_PIPE) ||
		g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_NO_REPLY) ||
		g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_TIMEOUT) ||
		remote_error_has_suffix(remote, ".Core.Timeout") ||
		remote_error_has_suffix(remote,
			".MobileEquipment.NetworkTimeout");
}

static gboolean
advanced_operation_outcome_is_unknown(AdvancedOperation *operation,
				      GError *error)
{
	g_autofree gchar *remote = NULL;

	if (!operation->dispatched)
		return FALSE;
	if (error != NULL && g_dbus_error_is_remote_error(error))
		remote = g_dbus_error_get_remote_error(error);
	return operation->timed_out || operation->transport_lost ||
		!operation->modem->live ||
		advanced_error_has_unknown_outcome(error, remote);
}

static AdvancedNormalizedError
normalize_advanced_error(GError *error, AdvancedOperation *operation)
{
	g_autofree gchar *remote = NULL;

	if (advanced_operation_outcome_is_unknown(operation, error))
		return (AdvancedNormalizedError){ "outcome_unknown",
			advanced_error_message("outcome_unknown"), FALSE };
	if (error != NULL && g_dbus_error_is_remote_error(error))
		remote = g_dbus_error_get_remote_error(error);
	if (error == NULL)
		return (AdvancedNormalizedError){ "operation_failed",
			advanced_error_message("operation_failed"), FALSE };
	if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED) ||
	    g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED) ||
	    g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_AUTH_FAILED) ||
	    g_error_matches(error, MM_CORE_ERROR, MM_CORE_ERROR_UNAUTHORIZED) ||
	    g_error_matches(error, MM_MOBILE_EQUIPMENT_ERROR,
		MM_MOBILE_EQUIPMENT_ERROR_NOT_ALLOWED))
		return (AdvancedNormalizedError){ "permission_denied",
			advanced_error_message("permission_denied"), FALSE };
	if (g_error_matches(error, G_DBUS_ERROR,
			    G_DBUS_ERROR_SERVICE_UNKNOWN) ||
	    g_error_matches(error, G_DBUS_ERROR,
			    G_DBUS_ERROR_NAME_HAS_NO_OWNER))
		return (AdvancedNormalizedError){ "dependency_unavailable",
			advanced_error_message("dependency_unavailable"), TRUE };
	if (g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD) ||
	    g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED) ||
	    g_error_matches(error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED) ||
	    g_error_matches(error, MM_MOBILE_EQUIPMENT_ERROR,
		MM_MOBILE_EQUIPMENT_ERROR_NOT_SUPPORTED) ||
	    remote_error_has_suffix(remote, ".Core.Unsupported") ||
	    remote_error_has_suffix(remote,
		".MobileEquipment.NotSupported"))
		return (AdvancedNormalizedError){ "unsupported",
			advanced_error_message("unsupported"), FALSE };
	if (g_error_matches(error, MM_CORE_ERROR, MM_CORE_ERROR_UNAUTHORIZED) ||
	    remote_error_has_suffix(remote, ".Core.Unauthorized"))
		return (AdvancedNormalizedError){ "permission_denied",
			advanced_error_message("permission_denied"), FALSE };
	if (g_error_matches(error, MM_CORE_ERROR, MM_CORE_ERROR_INVALID_ARGS) ||
	    g_error_matches(error, MM_MOBILE_EQUIPMENT_ERROR,
		MM_MOBILE_EQUIPMENT_ERROR_INCORRECT_PARAMETERS) ||
	    remote_error_has_suffix(remote, ".Core.InvalidArgs"))
		return (AdvancedNormalizedError){ "invalid_argument",
			advanced_error_message("invalid_argument"), FALSE };
	if (g_error_matches(error, MM_CORE_ERROR, MM_CORE_ERROR_IN_PROGRESS) ||
	    g_error_matches(error, MM_CORE_ERROR, MM_CORE_ERROR_THROTTLED) ||
	    g_error_matches(error, MM_MOBILE_EQUIPMENT_ERROR,
		MM_MOBILE_EQUIPMENT_ERROR_SIM_BUSY) ||
	    remote_error_has_suffix(remote, ".Core.InProgress") ||
	    remote_error_has_suffix(remote, ".Core.Throttled") ||
	    remote_error_has_suffix(remote, ".MobileEquipment.SimBusy"))
		return (AdvancedNormalizedError){ "busy",
			advanced_error_message("busy"), TRUE };
	if (g_error_matches(error, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE) ||
	    g_error_matches(error, MM_CORE_ERROR,
		MM_CORE_ERROR_WRONG_SIM_STATE) ||
	    g_error_matches(error, MM_CORE_ERROR, MM_CORE_ERROR_RETRY) ||
	    g_error_matches(error, MM_CORE_ERROR,
		MM_CORE_ERROR_RESET_AND_RETRY) ||
	    remote_error_has_suffix(remote, ".Core.WrongState") ||
	    remote_error_has_suffix(remote, ".Core.WrongSimState") ||
	    remote_error_has_suffix(remote, ".Core.Retry") ||
	    remote_error_has_suffix(remote, ".Core.ResetAndRetry"))
		return (AdvancedNormalizedError){ "not_ready",
			advanced_error_message("not_ready"), TRUE };
	return (AdvancedNormalizedError){ "operation_failed",
		advanced_error_message("operation_failed"), FALSE };
}

static void
advanced_operation_complete_success(AdvancedOperation *operation)
{
	struct blob_buf buffer = {};

	blob_buf_init(&buffer, 0);
	add_common(&buffer, TRUE);
	add_modem_identity(&buffer, operation->modem);
	blobmsg_add_u8(&buffer, "accepted", TRUE);
	blobmsg_add_string(&buffer, "operation",
		advanced_operation_name(operation->type));
	if (operation->type == ADVANCED_OPERATION_SIM_SLOT)
		blobmsg_add_u32(&buffer, "slot", operation->slot);
	if (operation->type == ADVANCED_OPERATION_RESET ||
	    operation->type == ADVANCED_OPERATION_SIM_SLOT)
		blobmsg_add_u8(&buffer, "reprobe_expected", TRUE);
	blobmsg_add_u32(&buffer, "cooldown_ms",
		operation->cooldown_seconds * 1000U);
	advanced_operation_complete_buffer(operation, &buffer);
}

static void
advanced_operation_ready(GObject *source, GAsyncResult *result,
			 gpointer user_data)
{
	AdvancedOperation *operation = user_data;
	g_autoptr(GError) error = NULL;
	AdvancedNormalizedError normalized;
	const gchar *stale;
	gboolean success = FALSE;

	switch (operation->type) {
	case ADVANCED_OPERATION_BANDS:
		success = mm_modem_set_current_bands_finish(MM_MODEM(source),
			result, &error);
		break;
	case ADVANCED_OPERATION_RADIO_ENABLE:
		success = mm_modem_enable_finish(MM_MODEM(source), result, &error);
		break;
	case ADVANCED_OPERATION_RADIO_DISABLE:
		success = mm_modem_disable_finish(MM_MODEM(source), result, &error);
		break;
	case ADVANCED_OPERATION_RESET:
		success = mm_modem_reset_finish(MM_MODEM(source), result, &error);
		break;
	case ADVANCED_OPERATION_SIM_SLOT:
		success = mm_modem_set_primary_sim_slot_finish(MM_MODEM(source),
			result, &error);
		break;
	default:
		break;
	}
	if (advanced_operation_outcome_is_unknown(operation, error)) {
		advanced_operation_complete_error(operation, "outcome_unknown",
			advanced_error_message("outcome_unknown"), FALSE);
		return;
	}
	stale = advanced_operation_stale_code(operation, source);
	if (stale != NULL) {
		advanced_operation_complete_error(operation, stale,
			advanced_error_message(stale), FALSE);
		return;
	}
	if (!success) {
		normalized = normalize_advanced_error(error, operation);
		advanced_operation_complete_error(operation, normalized.code,
			normalized.message, normalized.retryable);
		return;
	}
	advanced_operation_complete_success(operation);
}

static AdvancedOperation *
advanced_operation_new(FibocomUbus *ubus, FibocomModem *modem,
		       AdvancedOperationType type,
		       struct ubus_context *context,
		       struct ubus_request_data *request)
{
	AdvancedOperation *operation = g_new0(AdvancedOperation, 1);

	operation->type = type;
	operation->ubus = fibocom_ubus_ref_internal(ubus);
	operation->modem = fibocom_modem_ref(modem);
	operation->cancellable = g_cancellable_new();
	operation->generation = modem->generation;
	operation->cooldown_seconds =
		(type == ADVANCED_OPERATION_RESET ||
		 type == ADVANCED_OPERATION_SIM_SLOT) ?
		ADVANCED_REPROBE_COOLDOWN_SECONDS :
		ADVANCED_SHORT_COOLDOWN_SECONDS;
	ubus_defer_request(context, request, &operation->request);
	operation->deferred = TRUE;
	g_hash_table_add(ubus->advanced_operations, operation);
	operation->timeout_source = g_timeout_add_seconds(
		ADVANCED_OPERATION_TIMEOUT_SECONDS,
		advanced_operation_timeout, operation);
	modem->mutation_busy = TRUE;
	modem->mutation_kind = FIBOCOM_MUTATION_ADVANCED;
	modem->mutation_cancellable = g_object_ref(operation->cancellable);
	advanced_operation_apply_cooldown(operation);
	g_dbus_proxy_set_default_timeout(G_DBUS_PROXY(modem->modem),
		ADVANCED_PROXY_TIMEOUT_MS);
	return operation;
}

static int
method_set_bands(struct ubus_context *context, struct ubus_object *object,
		 struct ubus_request_data *request, const char *method,
		 struct blob_attr *message)
{
	FibocomUbus *ubus = from_object(object);
	struct blob_attr *parsed[__SET_BANDS_MAX] = {};
	const gchar *requested[FIBOCOM_RADIO_REQUEST_BANDS_MAX];
	struct FibocomRadioBand supported[MAX_RADIO_BANDS];
	unsigned int resolved[FIBOCOM_RADIO_REQUEST_BANDS_MAX];
	const gchar *modem_id;
	const gchar *error_code;
	guint32 retry_after;
	guint32 generation;
	guint requested_count;
	guint supported_count;
	guint supported_families;
	guint allowed_families;
	guint index;
	FibocomModem *modem;
	AdvancedOperation *operation;
	enum FibocomRadioPolicyResult policy_result;

	(void)method;
	if (!parse_exact_fields(message, set_bands_policy, __SET_BANDS_MAX,
		(G_GUINT64_CONSTANT(1) << __SET_BANDS_MAX) - 1U, parsed) ||
	    !fibocom_identity_is_valid(
		blobmsg_get_string(parsed[SET_BANDS_MODEM_ID])) ||
	    !blobmsg_get_bool(parsed[SET_BANDS_CONFIRM]) ||
	    !parse_requested_bands(parsed[SET_BANDS_BANDS], requested,
		&requested_count))
		return send_advanced_error(context, request, "invalid_argument",
			FALSE, 0U);
	modem_id = blobmsg_get_string(parsed[SET_BANDS_MODEM_ID]);
	generation = blobmsg_get_u32(parsed[SET_BANDS_GENERATION]);
	modem = advanced_mutation_modem(ubus, modem_id, generation,
		&error_code, &retry_after);
	if (modem == NULL)
		return send_advanced_error(context, request, error_code,
			g_str_equal(error_code, "busy") ||
			g_str_equal(error_code, "dependency_unavailable"),
			retry_after);
	if (!snapshot_supported_radio_bands(modem, supported,
		&supported_count, &supported_families) ||
	    !current_radio_families(modem, supported_families,
		&allowed_families))
		return send_advanced_error(context, request, "not_ready", TRUE, 0U);
	policy_result = fibocom_radio_resolve_bands(requested,
		requested_count, supported, supported_count, TRUE,
		allowed_families, (guint)MM_MODEM_BAND_ANY, resolved);
	if (policy_result != FIBOCOM_RADIO_POLICY_OK) {
		const gchar *code =
			policy_result == FIBOCOM_RADIO_POLICY_MODES_UNKNOWN ?
			"not_ready" : "invalid_argument";

		return send_advanced_error(context, request, code,
			g_str_equal(code, "not_ready"), 0U);
	}
	operation = advanced_operation_new(ubus, modem,
		ADVANCED_OPERATION_BANDS, context, request);
	operation->bands = g_new(MMModemBand, requested_count);
	operation->n_bands = requested_count;
	for (index = 0; index < requested_count; index++)
		operation->bands[index] = (MMModemBand)resolved[index];
	operation->dispatched = TRUE;
	mm_modem_set_current_bands(operation->modem->modem, operation->bands,
		operation->n_bands, operation->cancellable,
		advanced_operation_ready, operation);
	return UBUS_STATUS_OK;
}

static int
method_set_radio(struct ubus_context *context, struct ubus_object *object,
		 struct ubus_request_data *request, const char *method,
		 struct blob_attr *message)
{
	FibocomUbus *ubus = from_object(object);
	struct blob_attr *parsed[__SET_RADIO_MAX] = {};
	struct FibocomNetworkBinding binding;
	enum FibocomNetworkBindingResult binding_result;
	const gchar *modem_id;
	const gchar *error_code;
	guint32 retry_after;
	guint32 generation;
	gboolean enabled;
	FibocomModem *modem;
	AdvancedOperation *operation;

	(void)method;
	if (!parse_exact_fields(message, set_radio_policy, __SET_RADIO_MAX,
		(G_GUINT64_CONSTANT(1) << __SET_RADIO_MAX) - 1U, parsed) ||
	    !fibocom_identity_is_valid(
		blobmsg_get_string(parsed[SET_RADIO_MODEM_ID])) ||
	    !blobmsg_get_bool(parsed[SET_RADIO_CONFIRM]))
		return send_advanced_error(context, request, "invalid_argument",
			FALSE, 0U);
	modem_id = blobmsg_get_string(parsed[SET_RADIO_MODEM_ID]);
	generation = blobmsg_get_u32(parsed[SET_RADIO_GENERATION]);
	enabled = blobmsg_get_bool(parsed[SET_RADIO_ENABLED]);
	modem = advanced_mutation_modem(ubus, modem_id, generation,
		&error_code, &retry_after);
	if (modem == NULL)
		return send_advanced_error(context, request, error_code,
			g_str_equal(error_code, "busy") ||
			g_str_equal(error_code, "dependency_unavailable"),
			retry_after);
	binding_result = lookup_network_binding(modem, &binding);
	(void)binding;
	if (binding_result != FIBOCOM_NETWORK_BINDING_NONE) {
		if (binding_result == FIBOCOM_NETWORK_BINDING_UNIQUE)
			error_code = "managed_by_netifd";
		else if (binding_result == FIBOCOM_NETWORK_BINDING_AMBIGUOUS)
			error_code = "ambiguous_device";
		else
			error_code = "dependency_unavailable";
		return send_advanced_error(context, request, error_code,
			binding_result == FIBOCOM_NETWORK_BINDING_ERROR, 0U);
	}
	operation = advanced_operation_new(ubus, modem, enabled ?
		ADVANCED_OPERATION_RADIO_ENABLE :
		ADVANCED_OPERATION_RADIO_DISABLE, context, request);
	operation->dispatched = TRUE;
	if (enabled)
		mm_modem_enable(operation->modem->modem, operation->cancellable,
			advanced_operation_ready, operation);
	else
		mm_modem_disable(operation->modem->modem, operation->cancellable,
			advanced_operation_ready, operation);
	return UBUS_STATUS_OK;
}

static int
method_reset(struct ubus_context *context, struct ubus_object *object,
	     struct ubus_request_data *request, const char *method,
	     struct blob_attr *message)
{
	FibocomUbus *ubus = from_object(object);
	struct blob_attr *parsed[__RESET_MAX] = {};
	const gchar *modem_id;
	const gchar *error_code;
	guint32 retry_after;
	guint32 generation;
	FibocomModem *modem;
	AdvancedOperation *operation;

	(void)method;
	if (!parse_exact_fields(message, reset_policy, __RESET_MAX,
		(G_GUINT64_CONSTANT(1) << __RESET_MAX) - 1U, parsed) ||
	    !fibocom_identity_is_valid(
		blobmsg_get_string(parsed[RESET_MODEM_ID])) ||
	    !blobmsg_get_bool(parsed[RESET_CONFIRM]))
		return send_advanced_error(context, request, "invalid_argument",
			FALSE, 0U);
	modem_id = blobmsg_get_string(parsed[RESET_MODEM_ID]);
	generation = blobmsg_get_u32(parsed[RESET_GENERATION]);
	modem = advanced_mutation_modem(ubus, modem_id, generation,
		&error_code, &retry_after);
	if (modem == NULL)
		return send_advanced_error(context, request, error_code,
			g_str_equal(error_code, "busy") ||
			g_str_equal(error_code, "dependency_unavailable"),
			retry_after);
	operation = advanced_operation_new(ubus, modem,
		ADVANCED_OPERATION_RESET, context, request);
	operation->dispatched = TRUE;
	mm_modem_reset(operation->modem->modem, operation->cancellable,
		advanced_operation_ready, operation);
	return UBUS_STATUS_OK;
}

static int
method_set_primary_sim_slot(struct ubus_context *context,
			    struct ubus_object *object,
			    struct ubus_request_data *request,
			    const char *method, struct blob_attr *message)
{
	FibocomUbus *ubus = from_object(object);
	struct blob_attr *parsed[__SET_SIM_SLOT_MAX] = {};
	const gchar *modem_id;
	const gchar *error_code;
	guint32 retry_after;
	guint32 generation;
	guint32 slot;
	guint slots;
	FibocomModem *modem;
	AdvancedOperation *operation;

	(void)method;
	if (!parse_exact_fields(message, set_sim_slot_policy,
		__SET_SIM_SLOT_MAX,
		(G_GUINT64_CONSTANT(1) << __SET_SIM_SLOT_MAX) - 1U, parsed) ||
	    !fibocom_identity_is_valid(
		blobmsg_get_string(parsed[SET_SIM_SLOT_MODEM_ID])) ||
	    !blobmsg_get_bool(parsed[SET_SIM_SLOT_CONFIRM]))
		return send_advanced_error(context, request, "invalid_argument",
			FALSE, 0U);
	modem_id = blobmsg_get_string(parsed[SET_SIM_SLOT_MODEM_ID]);
	generation = blobmsg_get_u32(parsed[SET_SIM_SLOT_GENERATION]);
	slot = blobmsg_get_u32(parsed[SET_SIM_SLOT_SLOT]);
	modem = advanced_mutation_modem(ubus, modem_id, generation,
		&error_code, &retry_after);
	if (modem == NULL)
		return send_advanced_error(context, request, error_code,
			g_str_equal(error_code, "busy") ||
			g_str_equal(error_code, "dependency_unavailable"),
			retry_after);
	slots = sim_slot_count(modem);
	if (slots < 2U || slot == 0U || slot > slots ||
	    slot == mm_modem_get_primary_sim_slot(modem->modem))
		return send_advanced_error(context, request, "invalid_argument",
			FALSE, 0U);
	operation = advanced_operation_new(ubus, modem,
		ADVANCED_OPERATION_SIM_SLOT, context, request);
	operation->slot = slot;
	operation->dispatched = TRUE;
	mm_modem_set_primary_sim_slot(operation->modem->modem, slot,
		operation->cancellable, advanced_operation_ready, operation);
	return UBUS_STATUS_OK;
}

static gboolean reconnect_cb(gpointer user_data);

static void
schedule_reconnect(FibocomUbus *ubus)
{
	guint delay;

	if (ubus->stopping || ubus->reconnect_source != 0)
		return;
	delay = ubus->reconnect_delay_ms;
	delay += g_random_int_range(0, MAX(1U, delay / 5U));
	ubus->reconnect_source = g_timeout_add_full(G_PRIORITY_DEFAULT, delay,
		reconnect_cb, ubus, NULL);
	ubus->reconnect_delay_ms = MIN(RECONNECT_MAX_MS,
		ubus->reconnect_delay_ms * 2U);
}

static void
connection_lost(struct ubus_context *context)
{
	FibocomUbus *ubus = (FibocomUbus *)((gchar *)context -
		G_STRUCT_OFFSET(FibocomUbus, context));
	guint old_source;

	if (ubus->stopping)
		return;
	ubus->connected = FALSE;
	cancel_sms_operations(ubus, TRUE);
	cancel_advanced_operations(ubus, TRUE);
	old_source = ubus->fd_source;
	ubus->fd_source = 0;
	if (old_source != 0)
		g_source_remove(old_source);
	g_warning("ubus connection lost; reconnect scheduled");
	schedule_reconnect(ubus);
}

static gboolean
ubus_fd_ready(gint fd, GIOCondition condition, gpointer user_data)
{
	FibocomUbus *ubus = user_data;

	(void)fd;
	if (ubus->stopping || !ubus->connected)
		return G_SOURCE_REMOVE;
	if ((condition & (G_IO_IN | G_IO_HUP | G_IO_ERR)) != 0)
		ubus_handle_event(&ubus->context);
	if (ubus->connected && (condition & (G_IO_HUP | G_IO_ERR)) != 0)
		connection_lost(&ubus->context);
	if (!ubus->connected || (condition & G_IO_NVAL) != 0) {
		if (ubus->connected)
			connection_lost(&ubus->context);
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

static void
install_fd_source(FibocomUbus *ubus)
{
	if (ubus->fd_source != 0)
		g_source_remove(ubus->fd_source);
	ubus->fd_source = g_unix_fd_add_full(G_PRIORITY_DEFAULT,
		ubus->context.sock.fd, G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
		ubus_fd_ready, ubus, NULL);
}

static gboolean
connect_ubus(FibocomUbus *ubus)
{
	int status;

	if (!ubus->context_initialized) {
		status = ubus_connect_ctx(&ubus->context, ubus->socket_path);
		if (status != 0)
			return FALSE;
		ubus->context_initialized = TRUE;
		ubus->context.connection_lost = connection_lost;
		ubus->object.id = 0;
		memset(&ubus->object.avl, 0, sizeof(ubus->object.avl));
		fibocom_object_type.id = 0;
		status = ubus_add_object(&ubus->context, &ubus->object);
		if (status != 0) {
			g_warning("cannot publish fibocom.mm ubus object");
			ubus_shutdown(&ubus->context);
			ubus->context_initialized = FALSE;
			return FALSE;
		}
	} else {
		status = ubus_reconnect(&ubus->context, ubus->socket_path);
		if (status != 0)
			return FALSE;
		if (ubus->object.id == 0) {
			ubus_shutdown(&ubus->context);
			ubus->context_initialized = FALSE;
			fibocom_object_type.id = 0;
			return FALSE;
		}
	}
	ubus->connected = TRUE;
	ubus->reconnect_delay_ms = RECONNECT_INITIAL_MS;
	install_fd_source(ubus);
	g_message("companion API published as fibocom.mm");
	return TRUE;
}

static gboolean
reconnect_cb(gpointer user_data)
{
	FibocomUbus *ubus = user_data;

	ubus->reconnect_source = 0;
	if (ubus->stopping)
		return G_SOURCE_REMOVE;
	if (!connect_ubus(ubus))
		schedule_reconnect(ubus);
	return G_SOURCE_REMOVE;
}

static FibocomUbus *
fibocom_ubus_ref_internal(FibocomUbus *ubus)
{
	g_return_val_if_fail(ubus != NULL, NULL);
	g_atomic_int_inc(&ubus->refs);
	return ubus;
}

static void
fibocom_ubus_unref_internal(FibocomUbus *ubus)
{
	if (ubus == NULL || !g_atomic_int_dec_and_test(&ubus->refs))
		return;
	g_clear_pointer(&ubus->sms_operations, g_hash_table_unref);
	g_clear_pointer(&ubus->advanced_operations, g_hash_table_unref);
	g_free(ubus->socket_path);
	g_free(ubus);
}

FibocomUbus *
fibocom_ubus_new(FibocomBridge *bridge, const gchar *socket_path)
{
	FibocomUbus *ubus;

	g_return_val_if_fail(bridge != NULL, NULL);
	ubus = g_new0(FibocomUbus, 1);
	ubus->refs = 1;
	ubus->bridge = bridge;
	ubus->socket_path = g_strdup(socket_path);
	ubus->sms_operations = g_hash_table_new(g_direct_hash, g_direct_equal);
	ubus->advanced_operations = g_hash_table_new(g_direct_hash,
		g_direct_equal);
	ubus->reconnect_delay_ms = RECONNECT_INITIAL_MS;
	ubus->object.name = "fibocom.mm";
	ubus->object.type = &fibocom_object_type;
	ubus->object.methods = fibocom_methods;
	ubus->object.n_methods = G_N_ELEMENTS(fibocom_methods);
	return ubus;
}

void
fibocom_ubus_start(FibocomUbus *ubus)
{
	if (ubus->stopping || ubus->connected || ubus->reconnect_source != 0)
		return;
	if (!connect_ubus(ubus)) {
		g_warning("ubus unavailable; ModemManager observation continues");
		schedule_reconnect(ubus);
	}
}

void
fibocom_ubus_stop(FibocomUbus *ubus)
{
	if (ubus == NULL || ubus->stopping)
		return;
	ubus->stopping = TRUE;
	ubus->connected = FALSE;
	cancel_sms_operations(ubus, TRUE);
	cancel_advanced_operations(ubus, TRUE);
	if (ubus->fd_source != 0) {
		g_source_remove(ubus->fd_source);
		ubus->fd_source = 0;
	}
	if (ubus->reconnect_source != 0) {
		g_source_remove(ubus->reconnect_source);
		ubus->reconnect_source = 0;
	}
	if (ubus->context_initialized) {
		ubus->context.connection_lost = NULL;
		ubus_shutdown(&ubus->context);
		ubus->context_initialized = FALSE;
	}
}

void
fibocom_ubus_free(FibocomUbus *ubus)
{
	if (ubus == NULL)
		return;
	fibocom_ubus_stop(ubus);
	fibocom_ubus_unref_internal(ubus);
}
