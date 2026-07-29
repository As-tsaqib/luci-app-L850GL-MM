/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ubus_glib.h"

#include "identity.h"
#include "l850_ca.h"
#include "l850_cell.h"
#include "l850_voltage.h"
#include "network_binding.h"
#include "radio_policy.h"
#include "sms_dedupe_policy.h"
#include "sms_policy.h"
#include "ubus_request.h"

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
#define SAFE_IDENTIFIER_MAX 64U
#define SAFE_PHONE_NUMBER_MAX 32U
#define MAX_OWN_NUMBERS 16U
#define SMS_DEFAULT_LIMIT 50U
#define SMS_MAX_LIMIT 100U
#define SMS_INBOUND_CHARS_MAX 4096U
#define SMS_INBOUND_BYTES_MAX 16384U
#define SMS_OPERATION_TIMEOUT_SECONDS 320U
#define ADVANCED_OPERATION_TIMEOUT_SECONDS 130U
#define ADVANCED_PROXY_TIMEOUT_MS 120000
#define ADVANCED_SHORT_COOLDOWN_SECONDS 10U
#define L850_SCAN_OPERATION_TIMEOUT_SECONDS 45U
#define L850_STATUS_OPERATION_TIMEOUT_SECONDS 20U
#define L850_CARRIER_OPERATION_TIMEOUT_MS 20000U
#define L850_CARRIER_COMMAND_TIMEOUT_SECONDS 15U
#define L850_VOLTAGE_COMMAND_TIMEOUT_SECONDS 5U
#define L850_VOLTAGE_FRESH_SECONDS 60U
#define L850_VOLTAGE_RETRY_SECONDS 10U
#define L850_MUTATION_OPERATION_TIMEOUT_SECONDS 130U
#define L850_COMMAND_TIMEOUT_SECONDS 30U
#define L850_REPROBE_TIMEOUT_SECONDS 45U
#define L850_REGISTRATION_TIMEOUT_SECONDS 60U
#define L850_REPROBE_POLL_MS 500U
#define L850_REGISTRATION_POLL_MS 1000U
#define L850_POST_MUTATION_COOLDOWN_SECONDS 60U
#define MAX_RADIO_BANDS 256U
#define SERVING_CELL_FRESH_SECONDS 90U
#define SERVING_CELL_RETRY_SECONDS 30U

G_STATIC_ASSERT(L850GL_SMS_REQUEST_DIGEST_LEN ==
		L850GL_SMS_SHA256_DIGEST_LEN);

struct _L850GLUbus {
	gint refs;
	L850GLBridge *bridge;
	gchar *socket_path;
	struct ubus_context context;
	struct ubus_object object;
#ifdef L850GL_MM_EXPERT
	struct ubus_object l850_object;
#endif
	gboolean context_initialized;
	gboolean connected;
	gboolean stopping;
	guint fd_source;
	guint reconnect_source;
	guint reconnect_delay_ms;
	GHashTable *sms_operations;
	GHashTable *advanced_operations;
#ifdef L850GL_MM_EXPERT
	GHashTable *l850_scan_operations;
	GHashTable *l850_status_operations;
	GHashTable *l850_carrier_operations;
	GHashTable *l850_mutation_operations;
#endif
};

typedef enum {
	SMS_OPERATION_SEND,
	SMS_OPERATION_DELETE,
} SmsOperationType;

typedef struct {
	SmsOperationType type;
	L850GLUbus *ubus;
	L850GLModem *modem;
	MMModemMessaging *messaging;
	MMSmsProperties *properties;
	MMSms *sms;
	L850GLSms *sms_entry;
	GCancellable *cancellable;
	struct ubus_request_data request;
	guint32 generation;
	guint32 messaging_generation;
	guint timeout_source;
	gchar *client_token;
	gchar *sms_id;
	guint8 request_digest[L850GL_SMS_REQUEST_DIGEST_LEN];
	gboolean has_request_digest;
	gboolean timed_out;
	gboolean send_dispatched;
	gboolean transport_lost;
	gboolean deferred;
} SmsOperation;

typedef enum {
	ADVANCED_OPERATION_BANDS,
	ADVANCED_OPERATION_MODES,
} AdvancedOperationType;

typedef struct {
	AdvancedOperationType type;
	L850GLUbus *ubus;
	L850GLModem *modem;
	GCancellable *cancellable;
	struct ubus_request_data request;
	guint32 generation;
	guint timeout_source;
	guint cooldown_seconds;
	MMModemBand *bands;
	guint n_bands;
	struct ubus_request network_request;
	const gchar *activation;
	gboolean timed_out;
	gboolean transport_lost;
	gboolean dispatched;
	gboolean deferred;
	gboolean persisted;
	gboolean network_request_pending;
} AdvancedOperation;

typedef struct {
	L850GLModem *modem;
	guint32 generation;
} ServingCellQuery;

#ifdef L850GL_MM_EXPERT
typedef struct {
	L850GLModem *modem;
	guint32 generation;
} L850VoltageQuery;

typedef struct {
	L850GLUbus *ubus;
	L850GLModem *modem;
	GCancellable *cancellable;
	struct ubus_request_data request;
	guint32 generation;
	guint timeout_source;
	gulong parent_cancel_handler;
	gboolean timed_out;
	gboolean transport_lost;
	gboolean deferred;
	gboolean vendor_fallback;
} L850ScanOperation;

typedef struct {
	L850GLUbus *ubus;
	L850GLModem *modem;
	GCancellable *cancellable;
	struct ubus_request_data request;
	guint32 generation;
	guint timeout_source;
	gulong parent_cancel_handler;
	gboolean timed_out;
	gboolean transport_lost;
	gboolean deferred;
} L850StatusOperation;

typedef struct {
	L850GLUbus *ubus;
	L850GLModem *modem;
	GCancellable *cancellable;
	struct ubus_request_data request;
	guint32 generation;
	guint timeout_source;
	gulong parent_cancel_handler;
	gboolean timed_out;
	gboolean transport_lost;
	gboolean deferred;
} L850CarrierOperation;

typedef enum {
	L850_MUTATION_SET,
	L850_MUTATION_CLEAR,
} L850MutationType;

typedef enum {
	L850_MUTATION_PHASE_SET_COMMAND,
	L850_MUTATION_PHASE_RESET_COMMAND,
	L850_MUTATION_PHASE_REPROBE,
	L850_MUTATION_PHASE_REGISTRATION,
	L850_MUTATION_PHASE_NVM_VERIFY,
	L850_MUTATION_PHASE_CELL_VERIFY,
} L850MutationPhase;

typedef struct {
	L850MutationType type;
	L850MutationPhase phase;
	L850GLUbus *ubus;
	L850GLModem *original;
	L850GLModem *replacement;
	GCancellable *cancellable;
	struct ubus_request_data request;
	guint32 original_generation;
	guint32 replacement_generation;
	guint timeout_source;
	guint poll_source;
	gint64 phase_deadline;
	gchar *physdev;
	gchar set_command[L850GL_L850_COMMAND_MAX];
	guint32 earfcn;
	guint16 pci;
	guint16 band;
	gboolean has_pci;
	gboolean timed_out;
	gboolean transport_lost;
	gboolean deferred;
	gboolean command_pending;
	gboolean set_dispatched;
	gboolean configuration_acknowledged;
	gboolean reset_dispatched;
} L850MutationOperation;

typedef struct {
	guint8 type;
	gboolean serving;
	guint32 earfcn;
	guint16 pci;
	guint16 band;
	gdouble rsrp;
	gdouble rsrq;
	gboolean has_rsrp;
	gboolean has_rsrq;
} L850StandardCell;

typedef struct {
	const gchar *code;
	gboolean retryable;
} L850NormalizedError;

static gboolean l850_modem_has_active_mutation(L850GLUbus *ubus,
						L850GLModem *modem);
static gboolean l850_modem_has_active_scan(L850GLUbus *ubus,
					    L850GLModem *modem);
static gboolean l850_modem_has_active_carrier_query(L850GLUbus *ubus,
						    L850GLModem *modem);
static gboolean l850_firmware_allowed(L850GLModem *modem);
static gboolean l850_voltage_cache_is_fresh(L850GLModem *modem,
					    gint64 now);
static void l850_voltage_refresh(L850GLUbus *ubus, L850GLModem *modem);
#endif

static L850GLUbus *l850gl_ubus_ref_internal(L850GLUbus *ubus);
static void l850gl_ubus_unref_internal(L850GLUbus *ubus);
static void advanced_operation_free(AdvancedOperation *operation);
static void advanced_operation_complete_success(AdvancedOperation *operation);

static void
cancel_sms_operations(L850GLUbus *ubus, gboolean transport_lost)
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
cancel_advanced_operations(L850GLUbus *ubus, gboolean transport_lost)
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
		if (operation->type == ADVANCED_OPERATION_MODES) {
			if (operation->network_request_pending &&
			    ubus->context_initialized) {
				operation->network_request_pending = FALSE;
				ubus_abort_request(&ubus->context,
					&operation->network_request);
			}
			advanced_operation_free(operation);
			continue;
		}
		g_cancellable_cancel(operation->cancellable);
	}
	g_list_free(operations);
}

#ifdef L850GL_MM_EXPERT
static void
cancel_l850_scan_operations(L850GLUbus *ubus, gboolean transport_lost)
{
	GList *operations;
	GList *cursor;

	if (ubus == NULL || ubus->l850_scan_operations == NULL)
		return;
	operations = g_hash_table_get_keys(ubus->l850_scan_operations);
	for (cursor = operations; cursor != NULL; cursor = cursor->next) {
		L850ScanOperation *operation = cursor->data;

		operation->deferred = FALSE;
		operation->transport_lost = transport_lost;
		g_cancellable_cancel(operation->cancellable);
	}
	g_list_free(operations);
}

static void
cancel_l850_status_operations(L850GLUbus *ubus, gboolean transport_lost)
{
	GList *operations;
	GList *cursor;

	if (ubus == NULL || ubus->l850_status_operations == NULL)
		return;
	operations = g_hash_table_get_keys(ubus->l850_status_operations);
	for (cursor = operations; cursor != NULL; cursor = cursor->next) {
		L850StatusOperation *operation = cursor->data;

		operation->deferred = FALSE;
		operation->transport_lost = transport_lost;
		g_cancellable_cancel(operation->cancellable);
	}
	g_list_free(operations);
}

static void
cancel_l850_carrier_operations(L850GLUbus *ubus, gboolean transport_lost)
{
	GList *operations;
	GList *cursor;

	if (ubus == NULL || ubus->l850_carrier_operations == NULL)
		return;
	operations = g_hash_table_get_keys(ubus->l850_carrier_operations);
	for (cursor = operations; cursor != NULL; cursor = cursor->next) {
		L850CarrierOperation *operation = cursor->data;

		operation->deferred = FALSE;
		operation->transport_lost = transport_lost;
		g_cancellable_cancel(operation->cancellable);
	}
	g_list_free(operations);
}

static void
cancel_l850_mutation_operations(L850GLUbus *ubus,
				gboolean transport_lost)
{
	GList *operations;
	GList *cursor;

	if (ubus == NULL || ubus->l850_mutation_operations == NULL)
		return;
	operations = g_hash_table_get_keys(ubus->l850_mutation_operations);
	for (cursor = operations; cursor != NULL; cursor = cursor->next) {
		L850MutationOperation *operation = cursor->data;

		operation->deferred = FALSE;
		operation->transport_lost = transport_lost;
		g_cancellable_cancel(operation->cancellable);
	}
	g_list_free(operations);
}
#endif

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
	SET_MODES_MODEM_ID,
	SET_MODES_GENERATION,
	SET_MODES_ALLOWED,
	SET_MODES_PREFERRED,
	SET_MODES_CONFIRM,
	__SET_MODES_MAX,
};

static const struct blobmsg_policy set_modes_policy[__SET_MODES_MAX] = {
	[SET_MODES_MODEM_ID] = { .name = "modem_id", .type = BLOBMSG_TYPE_STRING },
	[SET_MODES_GENERATION] = {
		.name = "generation", .type = BLOBMSG_TYPE_INT32 },
	[SET_MODES_ALLOWED] = { .name = "allowed", .type = BLOBMSG_TYPE_STRING },
	[SET_MODES_PREFERRED] = {
		.name = "preferred", .type = BLOBMSG_TYPE_STRING },
	[SET_MODES_CONFIRM] = { .name = "confirm", .type = BLOBMSG_TYPE_BOOL },
};

#ifdef L850GL_MM_EXPERT
enum {
	L850_MODEM_ID,
	L850_GENERATION,
	__L850_STATUS_MAX,
};

static const struct blobmsg_policy l850_status_policy[__L850_STATUS_MAX] = {
	[L850_MODEM_ID] = { .name = "modem_id", .type = BLOBMSG_TYPE_STRING },
	[L850_GENERATION] = { .name = "generation", .type = BLOBMSG_TYPE_INT32 },
};

enum {
	L850_SET_MODEM_ID,
	L850_SET_GENERATION,
	L850_SET_EARFCN,
	L850_SET_PCI,
	L850_SET_CONFIRM,
	__L850_SET_MAX,
};

static const struct blobmsg_policy l850_set_policy[__L850_SET_MAX] = {
	[L850_SET_MODEM_ID] = { .name = "modem_id", .type = BLOBMSG_TYPE_STRING },
	[L850_SET_GENERATION] = {
		.name = "generation", .type = BLOBMSG_TYPE_INT32 },
	[L850_SET_EARFCN] = { .name = "earfcn", .type = BLOBMSG_TYPE_INT32 },
	[L850_SET_PCI] = { .name = "pci", .type = BLOBMSG_TYPE_INT32 },
	[L850_SET_CONFIRM] = { .name = "confirm", .type = BLOBMSG_TYPE_BOOL },
};

enum {
	L850_CLEAR_MODEM_ID,
	L850_CLEAR_GENERATION,
	L850_CLEAR_CONFIRM,
	__L850_CLEAR_MAX,
};

static const struct blobmsg_policy l850_clear_policy[__L850_CLEAR_MAX] = {
	[L850_CLEAR_MODEM_ID] = {
		.name = "modem_id", .type = BLOBMSG_TYPE_STRING },
	[L850_CLEAR_GENERATION] = {
		.name = "generation", .type = BLOBMSG_TYPE_INT32 },
	[L850_CLEAR_CONFIRM] = { .name = "confirm", .type = BLOBMSG_TYPE_BOOL },
};
#endif

static int method_list_modems(struct ubus_context *context,
			      struct ubus_object *object,
			      struct ubus_request_data *request,
			      const char *method, struct blob_attr *message);
static int method_get_overview(struct ubus_context *context,
			       struct ubus_object *object,
			       struct ubus_request_data *request,
			       const char *method, struct blob_attr *message);
static int method_get_lock_status(struct ubus_context *context,
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
static int method_set_modes(struct ubus_context *context,
			    struct ubus_object *object,
			    struct ubus_request_data *request,
			    const char *method, struct blob_attr *message);

#ifdef L850GL_MM_EXPERT
static int method_cell_scan(struct ubus_context *context,
			    struct ubus_object *object,
			    struct ubus_request_data *request,
			    const char *method, struct blob_attr *message);
static int method_get_carrier_info(struct ubus_context *context,
				   struct ubus_object *object,
				   struct ubus_request_data *request,
				   const char *method,
				   struct blob_attr *message);
static int method_cell_lock_status(struct ubus_context *context,
				   struct ubus_object *object,
				   struct ubus_request_data *request,
				   const char *method,
				   struct blob_attr *message);
static int method_set_cell_lock(struct ubus_context *context,
				struct ubus_object *object,
				struct ubus_request_data *request,
				const char *method, struct blob_attr *message);
static int method_clear_cell_lock(struct ubus_context *context,
				  struct ubus_object *object,
				  struct ubus_request_data *request,
				  const char *method,
				  struct blob_attr *message);
#endif
static const struct ubus_method l850gl_methods[] = {
	UBUS_METHOD_NOARG("list_modems", method_list_modems),
	UBUS_METHOD("get_overview", method_get_overview, modem_policy),
	UBUS_METHOD("get_lock_status", method_get_lock_status, modem_policy),
	UBUS_METHOD("set_bands", method_set_bands, set_bands_policy),
	UBUS_METHOD("set_modes", method_set_modes, set_modes_policy),
	UBUS_METHOD("list_sms", method_list_sms, list_sms_policy),
	UBUS_METHOD("send_sms", method_send_sms, send_sms_policy),
	UBUS_METHOD("delete_sms", method_delete_sms, delete_sms_policy),
};

static struct ubus_object_type l850gl_object_type =
	UBUS_OBJECT_TYPE("l850gl.mm", l850gl_methods);

#ifdef L850GL_MM_EXPERT
static const struct ubus_method l850_methods[] = {
	UBUS_METHOD("cell_scan", method_cell_scan, l850_status_policy),
	UBUS_METHOD("get_carrier_info", method_get_carrier_info,
		l850_status_policy),
	UBUS_METHOD("cell_lock_status", method_cell_lock_status,
		l850_status_policy),
	UBUS_METHOD("set_cell_lock", method_set_cell_lock, l850_set_policy),
	UBUS_METHOD("clear_cell_lock", method_clear_cell_lock,
		l850_clear_policy),
};

static struct ubus_object_type l850_object_type =
	UBUS_OBJECT_TYPE("l850gl.mm.l850", l850_methods);
#endif

static L850GLUbus *
from_object(struct ubus_object *object)
{
	return (L850GLUbus *)((gchar *)object -
		G_STRUCT_OFFSET(L850GLUbus, object));
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
preferred_own_number(MMModem *modem)
{
	const gchar *const *numbers = mm_modem_get_own_numbers(modem);
	gchar *selected = NULL;
	guint index;

	/*
	 * OwnNumbers is supplied by ModemManager, but its ordering is not a
	 * stable API contract. Pick the lexicographically smallest usable value
	 * so identical snapshots always produce the same scalar MSISDN. Keep
	 * both traversal and output bounded before exporting it over ubus.
	 */
	for (index = 0; numbers != NULL && numbers[index] != NULL &&
	     index < MAX_OWN_NUMBERS; index++) {
		g_autofree gchar *candidate =
			safe_text(numbers[index], SAFE_PHONE_NUMBER_MAX);

		if (candidate[0] == '\0')
			continue;
		if (selected == NULL || g_strcmp0(candidate, selected) < 0) {
			g_free(selected);
			selected = g_steal_pointer(&candidate);
		}
	}
	return selected != NULL ? selected : g_strdup("");
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

static void
add_common(struct blob_buf *buffer, gboolean ok)
{
	blobmsg_add_u32(buffer, "schema", L850GL_MM_API_SCHEMA);
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
blob_attribute_is_structurally_valid(struct blob_attr *attribute,
				     size_t remaining,
				     gboolean name_required)
{
	return l850gl_ubus_blob_attr_is_valid(attribute, remaining,
		name_required);
}

#ifdef L850GL_MM_EXPERT
static L850GLUbus *
from_l850_object(struct ubus_object *object)
{
	return (L850GLUbus *)((gchar *)object -
		G_STRUCT_OFFSET(L850GLUbus, l850_object));
}
#endif

static gboolean
message_is_empty(struct blob_attr *message)
{
	return l850gl_ubus_message_is_empty(message);
}

static gboolean
parse_modem_id(struct blob_attr *message, struct blob_attr **parsed)
{
	if (!l850gl_ubus_parse_exact(message, modem_policy, __MODEM_MAX,
		G_GUINT64_CONSTANT(1) << MODEM_ID, parsed))
		return FALSE;
	return parsed[MODEM_ID] != NULL &&
		l850gl_identity_is_valid(blobmsg_get_string(parsed[MODEM_ID]));
}

static L850GLModem *
requested_modem(L850GLUbus *ubus, struct blob_attr *message,
		struct blob_attr **parsed, const gchar **error_code)
{
	L850GLModem *modem;

	if (!parse_modem_id(message, parsed)) {
		*error_code = "invalid_argument";
		return NULL;
	}
	modem = l850gl_bridge_find_modem(ubus->bridge,
		blobmsg_get_string(parsed[MODEM_ID]));
	if (modem == NULL) {
		*error_code = l850gl_bridge_manager_available(ubus->bridge) ?
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
blob_string_is_canonical(struct blob_attr *attribute, size_t remaining,
			 gboolean name_required)
{
	return l850gl_ubus_blob_string_is_canonical(attribute, remaining,
		name_required);
}

static gboolean
parse_exact_fields(struct blob_attr *message,
		   const struct blobmsg_policy *policy, guint policy_length,
		   guint64 required, struct blob_attr **parsed)
{
	return l850gl_ubus_parse_exact(message, policy, policy_length,
		required, parsed);
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
	const L850GLSms *left_entry = *(L850GLSms *const *)left;
	const L850GLSms *right_entry = *(L850GLSms *const *)right;
	const gchar *left_timestamp = mm_sms_get_timestamp(left_entry->sms);
	const gchar *right_timestamp = mm_sms_get_timestamp(right_entry->sms);
	gint compared = g_strcmp0(right_timestamp, left_timestamp);

	if (compared != 0)
		return compared;
	return g_strcmp0(left_entry->sms_id, right_entry->sms_id);
}

static void
add_sms_entry(struct blob_buf *buffer, L850GLSms *entry)
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
	L850GLModem *modem = operation->modem;

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
sms_dedupe_prune(L850GLModem *modem)
{
	gint64 now = g_get_monotonic_time();
	GList *cursor;
	GList *next;

	for (cursor = modem->sms_dedupe->head; cursor != NULL; cursor = next) {
		L850GLSmsDedupe *entry = cursor->data;

		next = cursor->next;
		if (l850gl_sms_dedupe_is_expired(entry->expires_at, now)) {
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
sms_dedupe_digest_matches(const L850GLSmsDedupe *entry,
			  const guint8 digest[L850GL_SMS_REQUEST_DIGEST_LEN])
{
	return entry != NULL && entry->has_request_digest && digest != NULL &&
		l850gl_sms_digest_equal(entry->request_digest, digest);
}

static L850GLSmsDedupe *
sms_dedupe_find(L850GLModem *modem, const gchar *client_token)
{
	GList *cursor;

	sms_dedupe_prune(modem);
	for (cursor = modem->sms_dedupe->head; cursor != NULL;
	     cursor = cursor->next) {
		L850GLSmsDedupe *entry = cursor->data;

		if (g_str_equal(entry->client_token, client_token))
			return entry;
	}
	return NULL;
}

static void
sms_dedupe_remove(L850GLModem *modem, const gchar *client_token)
{
	L850GLSmsDedupe *entry = sms_dedupe_find(modem, client_token);

	if (entry == NULL || !g_queue_remove(modem->sms_dedupe, entry))
		return;
	g_free(entry->client_token);
	g_free(entry->sms_id);
	g_free(entry->state);
	g_free(entry->error_code);
	g_free(entry);
}

static void
sms_dedupe_store(L850GLModem *modem, const gchar *client_token,
		 const guint8 request_digest[L850GL_SMS_REQUEST_DIGEST_LEN],
		 gboolean ok, const gchar *sms_id, const gchar *state,
		 const gchar *error_code, gboolean retryable)
{
	L850GLSmsDedupe *entry;

	if (client_token == NULL || modem->sms_dedupe == NULL)
		return;
	sms_dedupe_prune(modem);
	entry = sms_dedupe_find(modem, client_token);
	if (entry == NULL) {
		while (l850gl_sms_dedupe_evictions_required(
			g_queue_get_length(modem->sms_dedupe)) > 0U) {
			entry = g_queue_pop_head(modem->sms_dedupe);
			g_free(entry->client_token);
			g_free(entry->sms_id);
			g_free(entry->state);
			g_free(entry->error_code);
			g_free(entry);
		}
		entry = g_new0(L850GLSmsDedupe, 1);
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
			L850GL_SMS_REQUEST_DIGEST_LEN);
	entry->expires_at = l850gl_sms_dedupe_expiry(g_get_monotonic_time());
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
preferred_bearer(L850GLModem *modem)
{
	GList *cursor;

	for (cursor = modem->bearers; cursor != NULL; cursor = cursor->next) {
		if (mm_bearer_get_connected(MM_BEARER(cursor->data)))
			return MM_BEARER(cursor->data);
	}
	return modem->bearers != NULL ? MM_BEARER(modem->bearers->data) : NULL;
}

static void
add_modem_identity(struct blob_buf *buffer, L850GLModem *modem)
{
	blobmsg_add_string(buffer, "modem_id", modem->modem_id);
	blobmsg_add_u32(buffer, "generation", modem->generation);
}

static void
add_network(struct blob_buf *buffer, L850GLModem *modem)
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
	blobmsg_add_u8(buffer, "roaming", registration_is_roaming(registration));
	add_access_technologies(buffer,
		mm_modem_get_access_technologies(modem->modem));
	blobmsg_close_table(buffer, network);
}

static void add_metric(struct blob_buf *buffer, const gchar *name,
		       gdouble value);
static guint add_band_array(struct blob_buf *buffer, const gchar *name,
			    const MMModemBand *bands, guint n_bands);
static void add_overview_capabilities(struct blob_buf *buffer,
			      L850GLUbus *ubus, L850GLModem *modem);
static void add_serving_cell_summary(struct blob_buf *buffer,
			     L850GLModem *modem);
static void add_modem_voltage(struct blob_buf *buffer, L850GLUbus *ubus,
			      L850GLModem *modem);

static void
add_signal_summary(struct blob_buf *buffer, L850GLModem *modem)
{
	MMModemSignal *signal_proxy = mm_object_peek_modem_signal(modem->object);
	MMSignal *metrics = NULL;
	gboolean recent = FALSE;
	guint quality = mm_modem_get_signal_quality(modem->modem, &recent);
	void *signal = blobmsg_open_table(buffer, "signal");

	blobmsg_add_u32(buffer, "quality", MIN(quality, 100U));
	blobmsg_add_u8(buffer, "recent", recent);
	if (signal_proxy != NULL) {
		metrics = mm_modem_signal_peek_lte(signal_proxy);
		if (metrics == NULL)
			metrics = mm_modem_signal_peek_nr5g(signal_proxy);
		if (metrics == NULL)
			metrics = mm_modem_signal_peek_umts(signal_proxy);
		if (metrics == NULL)
			metrics = mm_modem_signal_peek_gsm(signal_proxy);
	}
	if (metrics != NULL) {
		add_metric(buffer, "rsrp", mm_signal_get_rsrp(metrics));
		add_metric(buffer, "rsrq", mm_signal_get_rsrq(metrics));
		add_metric(buffer, "sinr", mm_signal_get_sinr(metrics));
	}
	blobmsg_close_table(buffer, signal);
}

static void
add_sim_summary(struct blob_buf *buffer, L850GLModem *modem)
{
	const gchar *sim_path = mm_modem_get_sim_path(modem->modem);
	gboolean present = sim_path != NULL && !g_str_equal(sim_path, "/");
	MMSim *cached_sim = present &&
		g_str_equal(modem->sim_cache_state, "ready") ? modem->sim : NULL;
	g_autofree gchar *number = cached_sim != NULL ?
		preferred_own_number(modem->modem) : g_strdup("");
	void *sim = blobmsg_open_table(buffer, "sim");

	blobmsg_add_u8(buffer, "present", present);
	blobmsg_add_string(buffer, "lock",
		lock_name(mm_modem_get_unlock_required(modem->modem)));
	add_safe_string(buffer, "number", number, SAFE_PHONE_NUMBER_MAX);
	add_safe_string(buffer, "imsi",
		cached_sim != NULL ? mm_sim_get_imsi(cached_sim) : "",
		SAFE_IDENTIFIER_MAX);
	add_safe_string(buffer, "iccid",
		cached_sim != NULL ? mm_sim_get_identifier(cached_sim) : "",
		SAFE_IDENTIFIER_MAX);
	blobmsg_close_table(buffer, sim);
}

static void
add_bearer_summary(struct blob_buf *buffer, L850GLModem *modem)
{
	MMBearer *bearer = preferred_bearer(modem);
	void *entry = blobmsg_open_table(buffer, "bearer");

	blobmsg_add_u8(buffer, "connected",
		bearer != NULL && mm_bearer_get_connected(bearer));
	add_safe_string(buffer, "interface",
		bearer != NULL ? mm_bearer_get_interface(bearer) : "",
		SAFE_PORT_MAX);
	blobmsg_close_table(buffer, entry);
}

static void
add_current_bands_summary(struct blob_buf *buffer, L850GLModem *modem)
{
	const MMModemBand *bands = NULL;
	guint n_bands = 0;

	(void)mm_modem_peek_current_bands(modem->modem, &bands, &n_bands);
	(void)add_band_array(buffer, "current_bands", bands, n_bands);
}

static int
method_list_modems(struct ubus_context *context, struct ubus_object *object,
		   struct ubus_request_data *request, const char *method,
		   struct blob_attr *message)
{
	L850GLUbus *ubus = from_object(object);
	g_autoptr(GPtrArray) modems = NULL;
	struct blob_buf buffer = {};
	void *array;
	guint i;

	(void)method;
	if (!message_is_empty(message))
		return send_error(context, request, "invalid_argument",
			"list_modems does not accept arguments", FALSE);
	modems = l850gl_bridge_snapshot_modems(ubus->bridge);
	blob_buf_init(&buffer, 0);
	add_common(&buffer, TRUE);
	array = blobmsg_open_array(&buffer, "modems");
	for (i = 0; i < modems->len; i++) {
		L850GLModem *modem = g_ptr_array_index(modems, i);
		void *entry = blobmsg_open_table(&buffer, NULL);

		add_modem_identity(&buffer, modem);
		add_safe_string(&buffer, "manufacturer",
			mm_modem_get_manufacturer(modem->modem), SAFE_TEXT_MAX);
		add_safe_string(&buffer, "model", mm_modem_get_model(modem->modem),
			SAFE_TEXT_MAX);
		add_safe_string(&buffer, "revision",
			mm_modem_get_revision(modem->modem),
			SAFE_TEXT_MAX);
		blobmsg_add_string(&buffer, "state",
			state_name(mm_modem_get_state(modem->modem)));
		blobmsg_add_string(&buffer, "power",
			power_state_name(mm_modem_get_power_state(modem->modem)));
		blobmsg_close_table(&buffer, entry);
	}
	blobmsg_close_array(&buffer, array);
	return send_buffer(context, request, &buffer);
}

static int
method_get_overview(struct ubus_context *context, struct ubus_object *object,
		    struct ubus_request_data *request, const char *method,
		    struct blob_attr *message)
{
	L850GLUbus *ubus = from_object(object);
	struct blob_attr *parsed[__MODEM_MAX] = {};
	const gchar *error_code;
	L850GLModem *modem;
	struct blob_buf buffer = {};
	void *identity;
	void *modem_state;
	void *warnings;

	(void)method;
	modem = requested_modem(ubus, message, parsed, &error_code);
	if (modem == NULL)
		return send_requested_error(context, request, error_code);
	blob_buf_init(&buffer, 0);
	add_common(&buffer, TRUE);
	add_modem_identity(&buffer, modem);
	blobmsg_add_string(&buffer, "freshness", modem->live ? "fresh" : "stale");
	identity = blobmsg_open_table(&buffer, "identity");
	add_safe_string(&buffer, "manufacturer",
		mm_modem_get_manufacturer(modem->modem), SAFE_TEXT_MAX);
	add_safe_string(&buffer, "model", mm_modem_get_model(modem->modem),
		SAFE_TEXT_MAX);
	add_safe_string(&buffer, "revision", mm_modem_get_revision(modem->modem),
		SAFE_TEXT_MAX);
	add_safe_string(&buffer, "imei",
		mm_modem_get_equipment_identifier(modem->modem),
		SAFE_IDENTIFIER_MAX);
	blobmsg_close_table(&buffer, identity);
	blobmsg_add_string(&buffer, "usb_mode",
		l850gl_modem_composition(modem));
	modem_state = blobmsg_open_table(&buffer, "modem");
	blobmsg_add_string(&buffer, "state",
		state_name(mm_modem_get_state(modem->modem)));
	blobmsg_add_string(&buffer, "power",
		power_state_name(mm_modem_get_power_state(modem->modem)));
	add_modem_voltage(&buffer, ubus, modem);
	blobmsg_close_table(&buffer, modem_state);
	add_sim_summary(&buffer, modem);
	add_network(&buffer, modem);
	add_signal_summary(&buffer, modem);
	add_bearer_summary(&buffer, modem);
	add_current_bands_summary(&buffer, modem);
	add_serving_cell_summary(&buffer, modem);
	add_overview_capabilities(&buffer, ubus, modem);
	warnings = blobmsg_open_array(&buffer, "warnings");
	if (!g_str_equal(modem->sim_cache_state, "ready") &&
	    !g_str_equal(modem->sim_cache_state, "absent"))
		blobmsg_add_string(&buffer, NULL, "sim-snapshot-incomplete");
	if (!g_str_equal(modem->bearer_cache_state, "ready"))
		blobmsg_add_string(&buffer, NULL, "bearer-snapshot-incomplete");
	if (!g_file_test("/lib/netifd/proto/modemmanager.sh", G_FILE_TEST_EXISTS))
		blobmsg_add_string(&buffer, NULL,
			"netifd-modemmanager-protocol-unavailable");
	if (!g_str_equal(l850gl_modem_composition(modem), "mbim"))
		blobmsg_add_string(&buffer, NULL, "mbim-composition-required");
	if (!l850gl_modem_attest_mutation_target(modem))
		blobmsg_add_string(&buffer, NULL,
			"mutations-disabled-hardware-attestation");
	blobmsg_close_array(&buffer, warnings);
	return send_buffer(context, request, &buffer);
}

static guint
mode_family_mask(MMModemMode modes)
{
	guint families = L850GL_RADIO_FAMILY_NONE;

	if ((modes & MM_MODEM_MODE_2G) != 0)
		families |= L850GL_RADIO_FAMILY_2G;
	if ((modes & MM_MODEM_MODE_3G) != 0)
		families |= L850GL_RADIO_FAMILY_3G;
	if ((modes & MM_MODEM_MODE_4G) != 0)
		families |= L850GL_RADIO_FAMILY_4G;
	if ((modes & MM_MODEM_MODE_5G) != 0)
		families |= L850GL_RADIO_FAMILY_5G;
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
		if (!l850gl_radio_band_name_is_canonical(band_name))
			continue;
		blobmsg_add_string(buffer, NULL, band_name);
		added++;
	}
	blobmsg_close_array(buffer, array);
	return added;
}

static void
add_metric(struct blob_buf *buffer, const gchar *name, gdouble value)
{
	if (value != MM_SIGNAL_UNKNOWN && isfinite(value))
		blobmsg_add_double(buffer, name, value);
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
advanced_retry_after_ms(L850GLUbus *ubus, L850GLModem *modem)
{
	(void)ubus;
	return cooldown_remaining_ms(modem->advanced_cooldown_until);
}

static void
add_standard_feature(struct blob_buf *buffer, const gchar *name,
		     L850GLUbus *ubus, L850GLModem *modem,
		     gboolean attested, gboolean available,
		     const gchar *unavailable_state,
		     const gchar *available_reason,
		     const gchar *unavailable_reason)
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
add_messaging_feature(struct blob_buf *buffer, const gchar *name,
		      L850GLModem *modem,
		      gboolean present, gboolean attested)
{
	const gchar *state;
	const gchar *reason;
	gboolean mutable = FALSE;
	gboolean busy = FALSE;
	void *feature = blobmsg_open_table(buffer, name);

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
snapshot_supported_radio_bands(L850GLModem *modem,
			       struct L850GLRadioBand *choices,
			       guint *choice_count,
			       guint *supported_families)
{
	const MMModemBand *bands = NULL;
	guint n_bands = 0;
	guint count = 0;
	guint families = L850GL_RADIO_FAMILY_NONE;
	guint index;

	*choice_count = 0U;
	*supported_families = L850GL_RADIO_FAMILY_NONE;
	if (!mm_modem_peek_supported_bands(modem->modem, &bands, &n_bands) ||
	    bands == NULL || n_bands == 0U || n_bands > MAX_RADIO_BANDS)
		return FALSE;
	for (index = 0; index < n_bands; index++) {
		const gchar *name;
		guint family;

		if (bands[index] == MM_MODEM_BAND_UNKNOWN)
			continue;
		name = mm_modem_band_get_string(bands[index]);
		if (!l850gl_radio_band_name_is_canonical(name))
			continue;
		family = l850gl_radio_band_family(name);
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
lte_band_is_supported(L850GLModem *modem, guint16 band)
{
	struct L850GLRadioBand choices[MAX_RADIO_BANDS];
	const gchar *names[MAX_RADIO_BANDS];
	guint count = 0U;
	guint families = L850GL_RADIO_FAMILY_NONE;
	guint index;

	if (!snapshot_supported_radio_bands(modem, choices, &count, &families))
		return FALSE;
	(void)families;
	for (index = 0U; index < count; index++)
		names[index] = choices[index].name;
	return l850gl_l850_band_is_supported(band, names, count);
}

static gboolean
parse_standard_lte_pci(const gchar *value, guint16 *pci)
{
	guint32 parsed = 0U;
	gsize index;
	gsize length;

	if (value == NULL || pci == NULL)
		return FALSE;
	length = strlen(value);
	if (length == 0U || length > 8U)
		return FALSE;
	for (index = 0U; index < length; index++) {
		guint32 digit;
		guchar character = (guchar)value[index];

		if (character >= '0' && character <= '9')
			digit = character - '0';
		else if (character >= 'A' && character <= 'F')
			digit = character - 'A' + 10U;
		else if (character >= 'a' && character <= 'f')
			digit = character - 'a' + 10U;
		else
			return FALSE;
		if (parsed > (G_MAXUINT32 - digit) / 16U)
			return FALSE;
		parsed = parsed * 16U + digit;
	}
	if (parsed > 503U)
		return FALSE;
	*pci = (guint16)parsed;
	return TRUE;
}

static gboolean
serving_cell_cache_is_fresh(L850GLModem *modem, gint64 now)
{
	return modem->live && modem->serving_cell_valid &&
		modem->serving_cell_generation == modem->generation &&
		modem->serving_cell_updated_at > 0 &&
		now - modem->serving_cell_updated_at <=
			(gint64)SERVING_CELL_FRESH_SECONDS * G_USEC_PER_SEC;
}

static gboolean
serving_cell_cache_store(L850GLModem *modem, guint32 earfcn, guint16 pci,
			 guint16 band, gdouble rsrp, gboolean has_rsrp,
			 gdouble rsrq, gboolean has_rsrq,
			 const gchar *reason)
{
	guint16 mapped_band;

	if (modem == NULL || !modem->live || pci > 503U ||
	    !l850gl_l850_earfcn_to_band(earfcn, &mapped_band) ||
	    mapped_band != band || !lte_band_is_supported(modem, band) ||
	    (has_rsrp && !isfinite(rsrp)) ||
	    (has_rsrq && !isfinite(rsrq)))
		return FALSE;
	modem->serving_cell_generation = modem->generation;
	modem->serving_cell_earfcn = earfcn;
	modem->serving_cell_pci = pci;
	modem->serving_cell_band = band;
	modem->serving_cell_rsrp = rsrp;
	modem->serving_cell_rsrq = rsrq;
	modem->serving_cell_has_rsrp = has_rsrp;
	modem->serving_cell_has_rsrq = has_rsrq;
	modem->serving_cell_updated_at = g_get_monotonic_time();
	modem->serving_cell_reason = reason;
	modem->serving_cell_valid = TRUE;
	return TRUE;
}

static gboolean
serving_cell_cache_store_standard(L850GLModem *modem, GList *items)
{
	guint32 earfcn = 0U;
	guint16 pci = 0U;
	guint16 band = 0U;
	gdouble rsrp = 0.0;
	gdouble rsrq = 0.0;
	gboolean has_rsrp = FALSE;
	gboolean has_rsrq = FALSE;
	guint serving_count = 0U;
	GList *cursor;

	for (cursor = items; cursor != NULL; cursor = cursor->next) {
		MMCellInfo *cell = cursor->data;
		MMCellInfoLte *lte;
		guint candidate_earfcn;
		guint16 candidate_band;
		guint16 candidate_pci;
		gdouble candidate_rsrp;
		gdouble candidate_rsrq;

		if (!MM_IS_CELL_INFO(cell))
			return FALSE;
		if (mm_cell_info_get_cell_type(cell) != MM_CELL_TYPE_LTE)
			continue;
		if (!MM_IS_CELL_INFO_LTE(cell))
			return FALSE;
		lte = MM_CELL_INFO_LTE(cell);
		candidate_earfcn = mm_cell_info_lte_get_earfcn(lte);
		if (candidate_earfcn == G_MAXUINT ||
		    !l850gl_l850_earfcn_to_band(candidate_earfcn,
			&candidate_band) ||
		    !lte_band_is_supported(modem, candidate_band) ||
		    !parse_standard_lte_pci(
			mm_cell_info_lte_get_physical_ci(lte), &candidate_pci))
			return FALSE;
		candidate_rsrp = mm_cell_info_lte_get_rsrp(lte);
		candidate_rsrq = mm_cell_info_lte_get_rsrq(lte);
		if ((candidate_rsrp != -G_MAXDOUBLE &&
		     !isfinite(candidate_rsrp)) ||
		    (candidate_rsrq != -G_MAXDOUBLE &&
		     !isfinite(candidate_rsrq)))
			return FALSE;
		if (!mm_cell_info_get_serving(cell))
			continue;
		if (++serving_count != 1U)
			return FALSE;
		earfcn = candidate_earfcn;
		pci = candidate_pci;
		band = candidate_band;
		rsrp = candidate_rsrp;
		rsrq = candidate_rsrq;
		has_rsrp = candidate_rsrp != -G_MAXDOUBLE;
		has_rsrq = candidate_rsrq != -G_MAXDOUBLE;
	}
	return serving_count == 1U && serving_cell_cache_store(modem, earfcn,
		pci, band, rsrp, has_rsrp, rsrq, has_rsrq,
		"standard-cell-info");
}

static void
serving_cell_query_ready(GObject *source, GAsyncResult *result,
			 gpointer user_data)
{
	ServingCellQuery *query = user_data;
	L850GLModem *modem = query->modem;
	g_autoptr(GError) error = NULL;
	GList *cells;
	gboolean fresh;

	cells = mm_modem_get_cell_info_finish(MM_MODEM(source), result, &error);
	if (modem->live && modem->generation == query->generation &&
	    source == G_OBJECT(modem->modem)) {
		modem->serving_cell_refresh_pending = FALSE;
		fresh = serving_cell_cache_is_fresh(modem,
			g_get_monotonic_time());
		if (error == NULL &&
		    serving_cell_cache_store_standard(modem, cells)) {
			/* The validated cache was updated by the helper. */
		} else if (!fresh) {
			modem->serving_cell_valid = FALSE;
			modem->serving_cell_reason = error != NULL ?
				"standard-cell-info-unavailable" :
				"standard-cell-info-malformed";
		}
	}
	g_list_free_full(cells, g_object_unref);
	l850gl_modem_unref(modem);
	g_free(query);
}

static void
serving_cell_refresh(L850GLModem *modem)
{
	ServingCellQuery *query;
	gint64 now = g_get_monotonic_time();

	if (!modem->live || modem->serving_cell_refresh_pending ||
	    serving_cell_cache_is_fresh(modem, now) ||
	    (modem->serving_cell_last_attempt_at > 0 &&
	     now - modem->serving_cell_last_attempt_at <
		(gint64)SERVING_CELL_RETRY_SECONDS * G_USEC_PER_SEC))
		return;
	query = g_new0(ServingCellQuery, 1);
	query->modem = l850gl_modem_ref(modem);
	query->generation = modem->generation;
	modem->serving_cell_refresh_pending = TRUE;
	modem->serving_cell_last_attempt_at = now;
	mm_modem_get_cell_info(modem->modem, modem->cancellable,
		serving_cell_query_ready, query);
}

static void
add_serving_cell_summary(struct blob_buf *buffer, L850GLModem *modem)
{
	gint64 now = g_get_monotonic_time();
	gboolean fresh;
	void *serving;

	serving_cell_refresh(modem);
	fresh = serving_cell_cache_is_fresh(modem, now);
	serving = blobmsg_open_table(buffer, "serving_cell");
	blobmsg_add_string(buffer, "state", fresh ? "available" : "unavailable");
	blobmsg_add_string(buffer, "reason", fresh ?
		modem->serving_cell_reason :
		(modem->serving_cell_refresh_pending ? "refresh-pending" :
		 (modem->serving_cell_valid ? "stale" :
		  (modem->serving_cell_reason != NULL ?
		   modem->serving_cell_reason : "standard-cell-info-unavailable"))));
	if (fresh) {
		blobmsg_add_u32(buffer, "earfcn", modem->serving_cell_earfcn);
		blobmsg_add_u32(buffer, "pci", modem->serving_cell_pci);
		blobmsg_add_u32(buffer, "band", modem->serving_cell_band);
		if (modem->serving_cell_has_rsrp)
			blobmsg_add_double(buffer, "rsrp", modem->serving_cell_rsrp);
		if (modem->serving_cell_has_rsrq)
			blobmsg_add_double(buffer, "rsrq", modem->serving_cell_rsrq);
	}
	blobmsg_close_table(buffer, serving);
}

static void
add_modem_voltage(struct blob_buf *buffer, L850GLUbus *ubus,
		  L850GLModem *modem)
{
#ifdef L850GL_MM_EXPERT
	gint64 now;

	l850_voltage_refresh(ubus, modem);
	now = g_get_monotonic_time();
	if (l850_voltage_cache_is_fresh(modem, now)) {
		blobmsg_add_u32(buffer, "voltage_mv", modem->l850_voltage_mv);
		return;
	}
#else
	(void)ubus;
	(void)modem;
#endif
	(void)blobmsg_add_field(buffer, BLOBMSG_TYPE_UNSPEC, "voltage_mv",
		NULL, 0U);
}

static gboolean
current_radio_families(L850GLModem *modem, guint supported_families,
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
	if (families == L850GL_RADIO_FAMILY_NONE)
		return FALSE;
	*allowed_families = families;
	return TRUE;
}

static void
mode_policy_fallback(MMModemMode allowed, MMModemMode preferred,
		     const gchar **allowed_name, const gchar **preferred_name)
{
	if (allowed == MM_MODEM_MODE_3G)
		*allowed_name = "3g";
	else if (allowed == MM_MODEM_MODE_4G)
		*allowed_name = "4g";
	else
		*allowed_name = "3g|4g";
	if (g_str_equal(*allowed_name, "3g|4g") &&
	    preferred == MM_MODEM_MODE_3G)
		*preferred_name = "3g";
	else if (g_str_equal(*allowed_name, "3g|4g") &&
		 preferred == MM_MODEM_MODE_4G)
		*preferred_name = "4g";
	else
		*preferred_name = "none";
}

static void
add_mode_policy(struct blob_buf *buffer, L850GLUbus *ubus,
		L850GLModem *modem, MMModemMode current_allowed,
		MMModemMode current_preferred)
{
	struct L850GLNetworkBinding binding;
	enum L850GLNetworkBindingResult lookup_result;
	const gchar *device = mm_modem_get_device(modem->modem);
	const gchar *allowed_name;
	const gchar *preferred_name;
	const gchar *state = "unavailable";
	const gchar *reason = "netifd-binding-unavailable";
	gboolean configured = FALSE;
	gboolean mutable = FALSE;
	gboolean busy = FALSE;
	guint32 retry_after = advanced_retry_after_ms(ubus, modem);
	void *policy;

	mode_policy_fallback(current_allowed, current_preferred,
		&allowed_name, &preferred_name);
	lookup_result = device != NULL ?
		l850gl_network_binding_lookup(device, &binding) :
		L850GL_NETWORK_BINDING_ERROR;
	if (lookup_result == L850GL_NETWORK_BINDING_UNIQUE &&
	    binding.has_allowedmode && binding.has_preferredmode &&
	    l850gl_network_modes_are_valid(binding.allowedmode,
		binding.preferredmode)) {
		allowed_name = binding.allowedmode;
		preferred_name = binding.preferredmode;
		configured = TRUE;
	}
	if (lookup_result == L850GL_NETWORK_BINDING_NONE)
		reason = "netifd-binding-not-found";
	else if (lookup_result == L850GL_NETWORK_BINDING_AMBIGUOUS)
		reason = "netifd-binding-ambiguous";
	else if (lookup_result == L850GL_NETWORK_BINDING_UNIQUE &&
		 !l850gl_modem_attest_mutation_target(modem)) {
		state = "unsupported";
		reason = "exact-l850-mbim-hardware-not-attested";
	} else if (lookup_result == L850GL_NETWORK_BINDING_UNIQUE &&
		   (modem->mutation_busy || retry_after > 0U)) {
		state = "busy";
		reason = modem->mutation_busy ?
			"per-modem-mutation-in-progress" : "advanced-cooldown";
		busy = TRUE;
	} else if (lookup_result == L850GL_NETWORK_BINDING_UNIQUE) {
		state = "available";
		reason = configured ? "unique-netifd-binding" :
			"unique-netifd-binding-unconfigured";
		mutable = TRUE;
	}
	policy = blobmsg_open_table(buffer, "mode_policy");
	blobmsg_add_string(buffer, "state", state);
	blobmsg_add_u8(buffer, "mutable", mutable);
	blobmsg_add_string(buffer, "reason", reason);
	blobmsg_add_u8(buffer, "configured", configured);
	blobmsg_add_string(buffer, "allowed", allowed_name);
	blobmsg_add_string(buffer, "preferred", preferred_name);
	blobmsg_add_u8(buffer, "busy", busy);
	if (retry_after > 0U)
		blobmsg_add_u32(buffer, "retry_after_ms", retry_after);
	blobmsg_close_table(buffer, policy);
}

static void
add_pci_feature(struct blob_buf *buffer, const gchar *name,
		L850GLUbus *ubus, L850GLModem *modem)
{
#ifdef L850GL_MM_EXPERT
	guint32 retry_after = advanced_retry_after_ms(ubus, modem);

	if (!l850gl_modem_attest_mutation_target(modem))
		add_feature(buffer, name, "unsupported", FALSE,
			"exact-l850-mbim-hardware-not-attested");
	else if (!l850gl_l850_firmware_is_allowed(
			 mm_modem_get_revision(modem->modem)))
		add_feature(buffer, name, "unsupported_firmware", FALSE,
			"firmware-not-live-validated");
	else if (modem->mutation_busy || retry_after > 0U ||
		 l850_modem_has_active_mutation(ubus, modem))
		add_feature(buffer, name, "busy", FALSE,
			retry_after > 0U ? "advanced-cooldown" :
			"per-modem-mutation-in-progress");
	else
		add_feature(buffer, name, "available", TRUE,
			"live-validated-l850-command-state-machine");
#else
	(void)ubus;
	(void)modem;
	add_feature(buffer, name, "unsupported_build", FALSE,
		"expert-object-absent");
#endif
}

static void
add_overview_capabilities(struct blob_buf *buffer, L850GLUbus *ubus,
			  L850GLModem *modem)
{
	struct L850GLRadioBand band_choices[MAX_RADIO_BANDS];
	guint band_choice_count = 0;
	guint supported_families = L850GL_RADIO_FAMILY_NONE;
	guint allowed_families = L850GL_RADIO_FAMILY_NONE;
	gboolean attested = l850gl_modem_attest_mutation_target(modem);
	gboolean bands_available = snapshot_supported_radio_bands(modem,
		band_choices, &band_choice_count, &supported_families);
	gboolean modes_known = current_radio_families(modem,
		supported_families, &allowed_families);
	MMModemMessaging *messaging = mm_object_peek_modem_messaging(modem->object);
	void *capabilities = blobmsg_open_table(buffer, "capabilities");

	(void)band_choice_count;
	(void)allowed_families;
	add_messaging_feature(buffer, "sms", modem, messaging != NULL, attested);
	add_standard_feature(buffer, "band_lock", ubus, modem, attested,
		bands_available && modes_known, "unknown",
		"standard-set-current-bands",
		bands_available ? "current-modes-not-advertised" :
		"supported-bands-not-advertised");
	add_pci_feature(buffer, "pci_lock", ubus, modem);
	blobmsg_close_table(buffer, capabilities);
}

static int
method_get_lock_status(struct ubus_context *context,
		       struct ubus_object *object,
		       struct ubus_request_data *request, const char *method,
		       struct blob_attr *message)
{
	L850GLUbus *ubus = from_object(object);
	struct blob_attr *parsed[__MODEM_MAX] = {};
	struct L850GLRadioBand band_choices[MAX_RADIO_BANDS];
	const MMModemBand *supported_bands = NULL;
	const MMModemBand *current_bands = NULL;
	MMModemMode allowed = MM_MODEM_MODE_NONE;
	MMModemMode preferred = MM_MODEM_MODE_NONE;
	const gchar *error_code;
	L850GLModem *modem;
	guint n_supported_bands = 0;
	guint n_current_bands = 0;
	guint band_choice_count = 0;
	guint supported_families = L850GL_RADIO_FAMILY_NONE;
	guint allowed_families = L850GL_RADIO_FAMILY_NONE;
	gboolean current_bands_known;
	gboolean current_modes_known;
	gboolean bands_available;
	gboolean attested;
	struct blob_buf buffer = {};
	void *current_modes;

	(void)method;
	modem = requested_modem(ubus, message, parsed, &error_code);
	if (modem == NULL)
		return send_requested_error(context, request, error_code);
	(void)mm_modem_peek_supported_bands(modem->modem,
		&supported_bands, &n_supported_bands);
	current_bands_known = mm_modem_peek_current_bands(modem->modem,
		&current_bands, &n_current_bands);
	current_modes_known = mm_modem_get_current_modes(modem->modem,
		&allowed, &preferred);
	bands_available = snapshot_supported_radio_bands(modem, band_choices,
		&band_choice_count, &supported_families) &&
		current_radio_families(modem, supported_families,
			&allowed_families);
	attested = l850gl_modem_attest_mutation_target(modem);
	(void)band_choice_count;
	(void)allowed_families;

	blob_buf_init(&buffer, 0);
	add_common(&buffer, TRUE);
	add_modem_identity(&buffer, modem);
	(void)add_band_array(&buffer, "supported_bands", supported_bands,
		n_supported_bands);
	(void)add_band_array(&buffer, "current_bands", current_bands,
		n_current_bands);
	if (!current_bands_known || n_current_bands == 0U)
		blobmsg_add_string(&buffer, "band_selection", "unknown");
	else if (n_current_bands == 1U &&
		 current_bands[0] == MM_MODEM_BAND_ANY)
		blobmsg_add_string(&buffer, "band_selection", "automatic");
	else
		blobmsg_add_string(&buffer, "band_selection", "explicit");
	current_modes = blobmsg_open_table(&buffer, "current_modes");
	blobmsg_add_u8(&buffer, "known", current_modes_known);
	add_mode_array(&buffer, "allowed",
		current_modes_known ? allowed : MM_MODEM_MODE_NONE);
	blobmsg_add_string(&buffer, "preferred", current_modes_known ?
		preferred_mode_name(preferred) : "unknown");
	blobmsg_close_table(&buffer, current_modes);
	add_mode_policy(&buffer, ubus, modem,
		current_modes_known ? allowed : MM_MODEM_MODE_NONE,
		current_modes_known ? preferred : MM_MODEM_MODE_NONE);
	add_standard_feature(&buffer, "band_lock", ubus, modem, attested,
		bands_available, "unknown", "standard-set-current-bands",
		"supported-bands-or-current-modes-not-advertised");
	add_pci_feature(&buffer, "pci_lock", ubus, modem);
	return send_buffer(context, request, &buffer);
}

static int
method_list_sms(struct ubus_context *context, struct ubus_object *object,
		struct ubus_request_data *request, const char *method,
		struct blob_attr *message)
{
	L850GLUbus *ubus = from_object(object);
	struct blob_attr *parsed[__LIST_SMS_MAX] = {};
	g_autoptr(GPtrArray) snapshot = NULL;
	g_autoptr(GPtrArray) filtered = NULL;
	L850GLModem *modem;
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
	if (!l850gl_identity_is_valid(
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
	if (cursor[0] != '\0' && !l850gl_sms_identity_is_valid(cursor))
		return send_error(context, request, "invalid_argument",
			"cursor is invalid", FALSE);
	modem = l850gl_bridge_find_modem(ubus->bridge,
		blobmsg_get_string(parsed[LIST_SMS_MODEM_ID]));
	if (modem == NULL)
		return send_error(context, request,
			l850gl_bridge_manager_available(ubus->bridge) ?
			"stale_identity" : "dependency_unavailable",
			l850gl_bridge_manager_available(ubus->bridge) ?
			"modem identity is no longer live" :
			"ModemManager is not available",
			!l850gl_bridge_manager_available(ubus->bridge));
	if (modem->messaging == NULL)
		return send_error(context, request, "unsupported",
			"Messaging is not available on this modem", FALSE);
	if (!g_str_equal(modem->sms_cache_state, "ready") &&
	    !g_str_equal(modem->sms_cache_state, "ready-truncated")) {
		l850gl_modem_refresh_sms(modem);
		return send_error(context, request, "not_ready",
			"SMS inventory is still loading", TRUE);
	}
	snapshot = l850gl_modem_snapshot_sms(modem);
	filtered = g_ptr_array_new_with_free_func(
		(GDestroyNotify)l850gl_sms_unref);
	for (i = 0; i < snapshot->len; i++) {
		L850GLSms *entry = g_ptr_array_index(snapshot, i);

		if (g_str_equal(folder, "all") ||
		    g_str_equal(folder, sms_folder(entry->sms)))
			g_ptr_array_add(filtered, l850gl_sms_ref(entry));
	}
	g_ptr_array_sort(filtered, sms_compare);
	if (cursor[0] != '\0') {
		for (i = 0; i < filtered->len; i++) {
			L850GLSms *entry = g_ptr_array_index(filtered, i);

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
	blobmsg_add_u32(&buffer, "dedupe_capacity", L850GL_SMS_DEDUPE_MAX);
	blobmsg_add_u32(&buffer, "dedupe_window_seconds",
		L850GL_SMS_DEDUPE_SECONDS);
	blobmsg_add_string(&buffer, "folder", folder);
	blobmsg_add_u32(&buffer, "limit", limit);
	messages = blobmsg_open_array(&buffer, "messages");
	for (i = start; i < end; i++)
		add_sms_entry(&buffer, g_ptr_array_index(filtered, i));
	blobmsg_close_array(&buffer, messages);
	blobmsg_add_u8(&buffer, "has_more", has_more);
	blobmsg_add_string(&buffer, "next_cursor", has_more && end > start ?
		((L850GLSms *)g_ptr_array_index(filtered, end - 1U))->sms_id : "");
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
	L850GLUbus *ubus = operation->ubus;

	if (operation->timeout_source != 0)
		g_source_remove(operation->timeout_source);
	l850gl_modem_release_sms_id(operation->modem, operation->sms_id);
	if (operation->modem->mutation_kind == L850GL_MUTATION_SMS &&
	    operation->modem->mutation_cancellable ==
	    operation->cancellable) {
		g_clear_object(&operation->modem->mutation_cancellable);
		operation->modem->mutation_busy = FALSE;
		operation->modem->mutation_kind = L850GL_MUTATION_NONE;
	}
	g_clear_object(&operation->properties);
	g_clear_object(&operation->sms);
	if (operation->sms_entry != NULL)
		l850gl_sms_unref(operation->sms_entry);
	g_clear_object(&operation->messaging);
	g_clear_object(&operation->cancellable);
	l850gl_modem_unref(operation->modem);
	g_free(operation->client_token);
	g_free(operation->sms_id);
	if (ubus != NULL && ubus->sms_operations != NULL)
		g_hash_table_remove(ubus->sms_operations, operation);
	g_free(operation);
	l850gl_ubus_unref_internal(ubus);
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
sms_operation_new(L850GLUbus *ubus, L850GLModem *modem,
		  SmsOperationType type, struct ubus_context *context,
		  struct ubus_request_data *request)
{
	SmsOperation *operation = g_new0(SmsOperation, 1);

	operation->type = type;
	operation->ubus = l850gl_ubus_ref_internal(ubus);
	operation->modem = l850gl_modem_ref(modem);
	operation->messaging = g_object_ref(modem->messaging);
	operation->cancellable = g_cancellable_new();
	operation->generation = modem->generation;
	operation->messaging_generation = modem->messaging_generation;
	ubus_defer_request(context, request, &operation->request);
	operation->deferred = TRUE;
	g_hash_table_add(ubus->sms_operations, operation);
	sms_operation_arm_timeout(operation);
	modem->mutation_busy = TRUE;
	modem->mutation_kind = L850GL_MUTATION_SMS;
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
	l850gl_modem_refresh_sms(operation->modem);
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
	operation->sms_entry = l850gl_modem_admit_reserved_sms(
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
	if (!l850gl_modem_attest_mutation_target(operation->modem)) {
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
		l850gl_modem_refresh_sms(operation->modem);
	/* Delete() succeeded authoritatively even if its captured epoch is stale. */
	sms_operation_complete_delete(operation);
}

static L850GLModem *
sms_mutation_modem(L850GLUbus *ubus, const gchar *modem_id,
		   guint32 generation, guint32 messaging_generation,
		   const gchar **error_code)
{
	L850GLModem *modem;

	modem = l850gl_bridge_find_modem(ubus->bridge, modem_id);
	if (modem == NULL) {
		*error_code = l850gl_bridge_manager_available(ubus->bridge) ?
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
	if (!l850gl_modem_attest_mutation_target(modem)) {
		*error_code = "unsupported";
		return NULL;
	}
#ifdef L850GL_MM_EXPERT
	if (l850_modem_has_active_mutation(ubus, modem) ||
	    l850_modem_has_active_scan(ubus, modem) ||
	    l850_modem_has_active_carrier_query(ubus, modem) ||
	    modem->l850_voltage_refresh_pending) {
		*error_code = "busy";
		return NULL;
	}
#endif
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
		       struct ubus_request_data *request, L850GLModem *modem,
		       L850GLSmsDedupe *cached)
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
	L850GLUbus *ubus = from_object(object);
	struct blob_attr *parsed[__SEND_SMS_MAX] = {};
	const gchar *modem_id;
	const gchar *recipient;
	const gchar *text;
	const gchar *client_token;
	const gchar *error_code;
	L850GLSmsDedupe *cached;
	L850GLModem *modem;
	SmsOperation *operation;
	MMSmsProperties *properties;
	g_autofree gchar *sms_id = NULL;
	guint8 request_digest[L850GL_SMS_REQUEST_DIGEST_LEN];
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
	if (!l850gl_identity_is_valid(modem_id) ||
	    !l850gl_sms_recipient_is_valid(recipient) ||
	    !l850gl_sms_outbound_text_is_valid(text) ||
	    !l850gl_sms_operation_token_is_valid(client_token))
		return send_error(context, request, "invalid_argument",
			"recipient, text, modem_id, or client_token is invalid",
			FALSE);
	if (!l850gl_sms_request_digest(recipient, text, request_digest))
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
	sms_id = l850gl_modem_reserve_sms_id(modem);
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
		L850GL_SMS_REQUEST_DIGEST_LEN);
	operation->has_request_digest = TRUE;
	sms_dedupe_store(modem, client_token, operation->request_digest, FALSE,
		operation->sms_id, NULL, "busy", TRUE);
	if (!l850gl_modem_attest_mutation_target(modem)) {
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
	L850GLUbus *ubus = from_object(object);
	struct blob_attr *parsed[__DELETE_SMS_MAX] = {};
	const gchar *modem_id;
	const gchar *sms_id;
	const gchar *sms_path;
	const gchar *error_code;
	L850GLModem *modem;
	L850GLSms *entry;
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
	if (!l850gl_identity_is_valid(modem_id) ||
	    !l850gl_sms_identity_is_valid(sms_id) ||
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
		l850gl_modem_refresh_sms(modem);
		return send_sms_mutation_lookup_error(context, request, "not_ready");
	}
	if (modem->mutation_busy)
		return send_sms_mutation_lookup_error(context, request, "busy");
	entry = l850gl_modem_find_sms(modem, sms_id);
	if (entry == NULL)
		return send_sms_mutation_lookup_error(context, request, "not_found");
	if (mm_sms_get_state(entry->sms) == MM_SMS_STATE_SENDING) {
		l850gl_sms_unref(entry);
		return send_sms_mutation_lookup_error(context, request, "busy");
	}
	sms_path = mm_sms_get_path(entry->sms);
	if (sms_path == NULL || !g_variant_is_object_path(sms_path)) {
		l850gl_sms_unref(entry);
		return send_sms_mutation_lookup_error(context, request,
			"internal_error");
	}
	operation = sms_operation_new(ubus, modem, SMS_OPERATION_DELETE,
		context, request);
	operation->sms_entry = entry;
	operation->sms_id = g_strdup(sms_id);
	if (!l850gl_modem_attest_mutation_target(modem)) {
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
	if (g_str_equal(code, "netifd_binding_not_found"))
		return "No uniquely bound netifd ModemManager interface was found";
	if (g_str_equal(code, "netifd_binding_ambiguous"))
		return "The modem has an ambiguous or unsafe netifd binding";
	if (g_str_equal(code, "persistence_failed"))
		return "The netifd radio-mode intent could not be committed and verified";
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

static L850GLModem *
advanced_mutation_modem(L850GLUbus *ubus, const gchar *modem_id,
			guint32 generation, const gchar **error_code,
			guint32 *retry_after_ms)
{
	L850GLModem *modem;

	*retry_after_ms = 0U;
	modem = l850gl_bridge_find_modem(ubus->bridge, modem_id);
	if (modem == NULL) {
		*error_code = l850gl_bridge_manager_available(ubus->bridge) ?
			"stale_identity" : "dependency_unavailable";
		return NULL;
	}
	if (modem->generation != generation) {
		*error_code = "stale_generation";
		return NULL;
	}
	if (!l850gl_modem_attest_mutation_target(modem)) {
		*error_code = "unsupported";
		return NULL;
	}
#ifdef L850GL_MM_EXPERT
	if (l850_modem_has_active_mutation(ubus, modem) ||
	    l850_modem_has_active_scan(ubus, modem) ||
	    l850_modem_has_active_carrier_query(ubus, modem) ||
	    modem->l850_voltage_refresh_pending) {
		*error_code = "busy";
		return NULL;
	}
#endif
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
	size_t remaining;
	guint count = 0;

	if (array == NULL || blobmsg_type(array) != BLOBMSG_TYPE_ARRAY)
		return FALSE;
	remaining = blobmsg_data_len(array);
	item = (struct blob_attr *)blobmsg_data(array);
	while (remaining > 0U) {
		const gchar *name;
		size_t padded;

		if (!blob_attribute_is_structurally_valid(item, remaining, FALSE))
			return FALSE;
		name = blobmsg_name(item);
		if (count >= L850GL_RADIO_REQUEST_BANDS_MAX ||
		    blobmsg_type(item) != BLOBMSG_TYPE_STRING ||
		    !blob_string_is_canonical(item, remaining, FALSE) ||
		    (name != NULL && name[0] != '\0'))
			return FALSE;
		requested[count] = blobmsg_get_string(item);
		if (!l850gl_radio_band_name_is_canonical(requested[count]))
			return FALSE;
		count++;
		padded = blob_pad_len(item);
		if (padded > remaining)
			return FALSE;
		remaining -= padded;
		item = (struct blob_attr *)((gchar *)item + padded);
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
	case ADVANCED_OPERATION_MODES: return "set_modes";
	default: return "unknown";
	}
}

static void
advanced_operation_apply_cooldown(AdvancedOperation *operation)
{
	gint64 deadline = g_get_monotonic_time() +
		((gint64)operation->cooldown_seconds * G_USEC_PER_SEC);

	operation->modem->advanced_cooldown_until = MAX(
		operation->modem->advanced_cooldown_until, deadline);
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
	if (operation->type == ADVANCED_OPERATION_MODES) {
		if (operation->network_request_pending &&
		    operation->ubus->context_initialized) {
			operation->network_request_pending = FALSE;
			ubus_abort_request(&operation->ubus->context,
				&operation->network_request);
		}
		operation->activation = "outcome_unknown";
		advanced_operation_complete_success(operation);
		return G_SOURCE_REMOVE;
	}
	g_cancellable_cancel(operation->cancellable);
	return G_SOURCE_REMOVE;
}

static void
advanced_operation_free(AdvancedOperation *operation)
{
	L850GLUbus *ubus = operation->ubus;

	if (operation->timeout_source != 0)
		g_source_remove(operation->timeout_source);
	if (operation->network_request_pending && ubus != NULL &&
	    ubus->context_initialized) {
		operation->network_request_pending = FALSE;
		ubus_abort_request(&ubus->context, &operation->network_request);
	}
	advanced_operation_apply_cooldown(operation);
	if (operation->modem->mutation_kind == L850GL_MUTATION_ADVANCED &&
	    operation->modem->mutation_cancellable == operation->cancellable) {
		g_clear_object(&operation->modem->mutation_cancellable);
		operation->modem->mutation_busy = FALSE;
		operation->modem->mutation_kind = L850GL_MUTATION_NONE;
	}
	g_clear_object(&operation->cancellable);
	g_free(operation->bands);
	l850gl_modem_unref(operation->modem);
	if (ubus != NULL && ubus->advanced_operations != NULL)
		g_hash_table_remove(ubus->advanced_operations, operation);
	g_free(operation);
	l850gl_ubus_unref_internal(ubus);
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
	blobmsg_add_u32(&buffer, "cooldown_ms",
		operation->cooldown_seconds * 1000U);
	if (operation->type == ADVANCED_OPERATION_MODES) {
		blobmsg_add_u8(&buffer, "persisted", operation->persisted);
		blobmsg_add_string(&buffer, "activation",
			operation->activation != NULL ? operation->activation :
			"pending");
	}
	advanced_operation_complete_buffer(operation, &buffer);
}

static void
advanced_network_reload_complete(struct ubus_request *request, int status)
{
	AdvancedOperation *operation = request->priv;

	if (operation == NULL || !operation->network_request_pending)
		return;
	operation->network_request_pending = FALSE;
	operation->activation = status == UBUS_STATUS_OK ? "reloaded" : "failed";
	advanced_operation_complete_success(operation);
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
advanced_operation_new(L850GLUbus *ubus, L850GLModem *modem,
		       AdvancedOperationType type,
		       struct ubus_context *context,
		       struct ubus_request_data *request)
{
	AdvancedOperation *operation = g_new0(AdvancedOperation, 1);

	operation->type = type;
	operation->ubus = l850gl_ubus_ref_internal(ubus);
	operation->modem = l850gl_modem_ref(modem);
	operation->cancellable = g_cancellable_new();
	operation->generation = modem->generation;
	operation->cooldown_seconds = ADVANCED_SHORT_COOLDOWN_SECONDS;
	ubus_defer_request(context, request, &operation->request);
	operation->deferred = TRUE;
	g_hash_table_add(ubus->advanced_operations, operation);
	operation->timeout_source = g_timeout_add_seconds(
		ADVANCED_OPERATION_TIMEOUT_SECONDS,
		advanced_operation_timeout, operation);
	modem->mutation_busy = TRUE;
	modem->mutation_kind = L850GL_MUTATION_ADVANCED;
	modem->mutation_cancellable = g_object_ref(operation->cancellable);
	advanced_operation_apply_cooldown(operation);
	if (type == ADVANCED_OPERATION_BANDS)
		g_dbus_proxy_set_default_timeout(G_DBUS_PROXY(modem->modem),
			ADVANCED_PROXY_TIMEOUT_MS);
	return operation;
}

static int
method_set_bands(struct ubus_context *context, struct ubus_object *object,
		 struct ubus_request_data *request, const char *method,
		 struct blob_attr *message)
{
	L850GLUbus *ubus = from_object(object);
	struct blob_attr *parsed[__SET_BANDS_MAX] = {};
	const gchar *requested[L850GL_RADIO_REQUEST_BANDS_MAX];
	const gchar *effective[L850GL_RADIO_REQUEST_BANDS_MAX];
	struct L850GLRadioBand supported[MAX_RADIO_BANDS];
	unsigned int resolved[L850GL_RADIO_REQUEST_BANDS_MAX];
	const gchar *modem_id;
	const gchar *error_code;
	guint32 retry_after;
	guint32 generation;
	guint requested_count;
	size_t effective_count;
	guint supported_count;
	guint supported_families;
	guint allowed_families;
	guint index;
	L850GLModem *modem;
	AdvancedOperation *operation;
	enum L850GLRadioPolicyResult policy_result;

	(void)method;
	if (!parse_exact_fields(message, set_bands_policy, __SET_BANDS_MAX,
		(G_GUINT64_CONSTANT(1) << __SET_BANDS_MAX) - 1U, parsed) ||
	    !l850gl_identity_is_valid(
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
	policy_result = l850gl_radio_expand_lte_selection(requested,
		requested_count, supported, supported_count, allowed_families,
		effective, &effective_count);
	if (policy_result != L850GL_RADIO_POLICY_OK)
		return send_advanced_error(context, request,
			policy_result == L850GL_RADIO_POLICY_FAMILY_MISMATCH ?
			"not_ready" : "invalid_argument",
			policy_result == L850GL_RADIO_POLICY_FAMILY_MISMATCH,
			0U);
	policy_result = l850gl_radio_resolve_bands(effective,
		effective_count, supported, supported_count, TRUE,
		allowed_families, (guint)MM_MODEM_BAND_ANY, resolved);
	if (policy_result != L850GL_RADIO_POLICY_OK) {
		const gchar *code =
			policy_result == L850GL_RADIO_POLICY_MODES_UNKNOWN ?
			"not_ready" : "invalid_argument";

		return send_advanced_error(context, request, code,
			g_str_equal(code, "not_ready"), 0U);
	}
	operation = advanced_operation_new(ubus, modem,
		ADVANCED_OPERATION_BANDS, context, request);
	operation->bands = g_new(MMModemBand, effective_count);
	operation->n_bands = (guint)effective_count;
	for (index = 0; index < effective_count; index++)
		operation->bands[index] = (MMModemBand)resolved[index];
	operation->dispatched = TRUE;
	mm_modem_set_current_bands(operation->modem->modem, operation->bands,
		operation->n_bands, operation->cancellable,
		advanced_operation_ready, operation);
	return UBUS_STATUS_OK;
}

static int
method_set_modes(struct ubus_context *context, struct ubus_object *object,
		 struct ubus_request_data *request, const char *method,
		 struct blob_attr *message)
{
	L850GLUbus *ubus = from_object(object);
	struct blob_attr *parsed[__SET_MODES_MAX] = {};
	struct L850GLNetworkBinding binding;
	struct blob_buf empty = {};
	const gchar *modem_id;
	const gchar *allowed;
	const gchar *preferred;
	const gchar *device;
	const gchar *error_code;
	guint32 generation;
	guint32 retry_after;
	uint32_t network_id;
	int status;
	L850GLModem *modem;
	AdvancedOperation *operation;
	enum L850GLNetworkModeUpdateResult update_result;

	(void)method;
	if (!parse_exact_fields(message, set_modes_policy, __SET_MODES_MAX,
		(G_GUINT64_CONSTANT(1) << __SET_MODES_MAX) - 1U, parsed) ||
	    !l850gl_identity_is_valid(
		blobmsg_get_string(parsed[SET_MODES_MODEM_ID])) ||
	    !blobmsg_get_bool(parsed[SET_MODES_CONFIRM]))
		return send_advanced_error(context, request, "invalid_argument",
			FALSE, 0U);
	allowed = blobmsg_get_string(parsed[SET_MODES_ALLOWED]);
	preferred = blobmsg_get_string(parsed[SET_MODES_PREFERRED]);
	if (!l850gl_network_modes_are_valid(allowed, preferred))
		return send_advanced_error(context, request, "invalid_argument",
			FALSE, 0U);
	modem_id = blobmsg_get_string(parsed[SET_MODES_MODEM_ID]);
	generation = blobmsg_get_u32(parsed[SET_MODES_GENERATION]);
	modem = advanced_mutation_modem(ubus, modem_id, generation,
		&error_code, &retry_after);
	if (modem == NULL)
		return send_advanced_error(context, request, error_code,
			g_str_equal(error_code, "busy") ||
			g_str_equal(error_code, "dependency_unavailable"),
			retry_after);
	device = mm_modem_get_device(modem->modem);
	if (device == NULL || device[0] == '\0')
		return send_advanced_error(context, request,
			"dependency_unavailable", TRUE, 0U);
	operation = advanced_operation_new(ubus, modem,
		ADVANCED_OPERATION_MODES, context, request);
	update_result = l850gl_network_modes_update(device, allowed, preferred,
		&binding);
	if (update_result != L850GL_NETWORK_MODE_UPDATE_OK) {
		const gchar *code = "persistence_failed";
		gboolean retryable = TRUE;

		if (update_result == L850GL_NETWORK_MODE_UPDATE_NONE)
			code = "netifd_binding_not_found";
		else if (update_result == L850GL_NETWORK_MODE_UPDATE_AMBIGUOUS)
			code = "netifd_binding_ambiguous";
		else if (update_result == L850GL_NETWORK_MODE_UPDATE_INVALID) {
			code = "invalid_argument";
			retryable = FALSE;
		} else if (update_result ==
			   L850GL_NETWORK_MODE_UPDATE_VERIFY_FAILED) {
			code = "outcome_unknown";
			retryable = FALSE;
		}
		advanced_operation_complete_error(operation, code,
			advanced_error_message(code), retryable);
		return UBUS_STATUS_OK;
	}
	operation->persisted = TRUE;
	operation->dispatched = TRUE;
	operation->activation = "pending";
	status = ubus_lookup_id(&ubus->context, "network", &network_id);
	if (status != UBUS_STATUS_OK) {
		advanced_operation_complete_success(operation);
		return UBUS_STATUS_OK;
	}
	blob_buf_init(&empty, 0);
	status = ubus_invoke_async(&ubus->context, network_id, "reload",
		empty.head, &operation->network_request);
	blob_buf_free(&empty);
	if (status != UBUS_STATUS_OK) {
		advanced_operation_complete_success(operation);
		return UBUS_STATUS_OK;
	}
	operation->network_request.priv = operation;
	operation->network_request.complete_cb =
		advanced_network_reload_complete;
	operation->network_request_pending = TRUE;
	ubus_complete_request_async(&ubus->context,
		&operation->network_request);
	return UBUS_STATUS_OK;
}

#ifdef L850GL_MM_EXPERT
static gboolean
l850_modem_has_active_mutation(L850GLUbus *ubus, L850GLModem *modem)
{
	GHashTableIter iter;
	gpointer key;
	const gchar *physdev;

	if (ubus == NULL || modem == NULL ||
	    ubus->l850_mutation_operations == NULL)
		return FALSE;
	physdev = mm_modem_get_physdev(modem->modem);
	if (physdev == NULL || physdev[0] == '\0')
		return FALSE;
	g_hash_table_iter_init(&iter, ubus->l850_mutation_operations);
	while (g_hash_table_iter_next(&iter, &key, NULL)) {
		L850MutationOperation *operation = key;

		if (operation->physdev != NULL &&
		    g_str_equal(operation->physdev, physdev))
			return TRUE;
	}
	return FALSE;
}

static gboolean
l850_modem_has_active_scan(L850GLUbus *ubus, L850GLModem *modem)
{
	GHashTableIter iter;
	gpointer key;

	if (ubus == NULL || modem == NULL ||
	    ubus->l850_scan_operations == NULL)
		return FALSE;
	g_hash_table_iter_init(&iter, ubus->l850_scan_operations);
	while (g_hash_table_iter_next(&iter, &key, NULL)) {
		L850ScanOperation *operation = key;

		if (operation->modem == modem)
			return TRUE;
	}
	return FALSE;
}

static gboolean
l850_modem_has_active_carrier_query(L850GLUbus *ubus,
				    L850GLModem *modem)
{
	GHashTableIter iter;
	gpointer key;

	if (ubus == NULL || modem == NULL ||
	    ubus->l850_carrier_operations == NULL)
		return FALSE;
	g_hash_table_iter_init(&iter, ubus->l850_carrier_operations);
	while (g_hash_table_iter_next(&iter, &key, NULL)) {
		L850CarrierOperation *operation = key;

		if (operation->modem == modem)
			return TRUE;
	}
	return FALSE;
}

static gboolean
l850_voltage_cache_is_fresh(L850GLModem *modem, gint64 now)
{
	return modem->live && modem->l850_voltage_valid &&
		modem->l850_voltage_generation == modem->generation &&
		modem->l850_voltage_updated_at > 0 &&
		now - modem->l850_voltage_updated_at <=
			(gint64)L850_VOLTAGE_FRESH_SECONDS * G_USEC_PER_SEC;
}

static void
l850_voltage_ready(GObject *source, GAsyncResult *result, gpointer user_data)
{
	L850VoltageQuery *query = user_data;
	L850GLModem *modem = query->modem;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *response = NULL;
	struct L850GLL850Voltage parsed;
	gsize response_length;

	response = mm_modem_command_finish(MM_MODEM(source), result, &error);
	if (modem->live && modem->generation == query->generation &&
	    source == G_OBJECT(modem->modem)) {
		modem->l850_voltage_refresh_pending = FALSE;
		if (error == NULL && response != NULL &&
		    l850gl_modem_attest_mutation_target(modem) &&
		    l850_firmware_allowed(modem)) {
			response_length = strnlen(response,
				L850GL_L850_VOLTAGE_MAX_RESPONSE + 1U);
			if (l850gl_l850_voltage_parse(response, response_length,
				&parsed) == L850GL_L850_VOLTAGE_PARSE_OK) {
				modem->l850_voltage_generation = modem->generation;
				modem->l850_voltage_mv = parsed.millivolts;
				modem->l850_voltage_updated_at =
					g_get_monotonic_time();
				modem->l850_voltage_valid = TRUE;
			} else {
				modem->l850_voltage_valid = FALSE;
			}
		} else {
			modem->l850_voltage_valid = FALSE;
		}
	}
	l850gl_modem_unref(modem);
	g_free(query);
}

static void
l850_voltage_refresh(L850GLUbus *ubus, L850GLModem *modem)
{
	L850VoltageQuery *query;
	gint64 now = g_get_monotonic_time();

	if (!modem->live || modem->l850_voltage_refresh_pending ||
	    l850_voltage_cache_is_fresh(modem, now) ||
	    (modem->l850_voltage_last_attempt_at > 0 &&
	     now - modem->l850_voltage_last_attempt_at <
		(gint64)L850_VOLTAGE_RETRY_SECONDS * G_USEC_PER_SEC) ||
	    !l850gl_modem_attest_mutation_target(modem) ||
	    !l850_firmware_allowed(modem) || modem->mutation_busy ||
	    l850_modem_has_active_mutation(ubus, modem) ||
	    l850_modem_has_active_scan(ubus, modem) ||
	    l850_modem_has_active_carrier_query(ubus, modem))
		return;
	query = g_new0(L850VoltageQuery, 1);
	query->modem = l850gl_modem_ref(modem);
	query->generation = modem->generation;
	modem->l850_voltage_refresh_pending = TRUE;
	modem->l850_voltage_last_attempt_at = now;
	mm_modem_command(modem->modem, l850gl_l850_voltage_query_command(),
		L850_VOLTAGE_COMMAND_TIMEOUT_SECONDS, modem->cancellable,
		l850_voltage_ready, query);
}

static const gchar *
l850_error_message(const gchar *code)
{
	if (g_str_equal(code, "invalid_argument"))
		return "The expert request arguments are invalid";
	if (g_str_equal(code, "stale_identity"))
		return "The opaque modem identity is no longer live";
	if (g_str_equal(code, "stale_generation"))
		return "The modem generation changed; refresh before continuing";
	if (g_str_equal(code, "device_gone"))
		return "The modem was removed during the expert operation";
	if (g_str_equal(code, "dependency_unavailable"))
		return "ModemManager is not available";
	if (g_str_equal(code, "unsupported"))
		return "The exact L850-GL MBIM hardware could not be attested";
	if (g_str_equal(code, "unsupported_firmware"))
		return "Firmware is not in the live-validated allowlist";
	if (g_str_equal(code, "rate_limited"))
		return "The expert modem query is rate limited";
	if (g_str_equal(code, "timeout"))
		return "The expert ModemManager operation timed out";
	if (g_str_equal(code, "permission_denied"))
		return "ModemManager denied the reviewed expert command transport";
	if (g_str_equal(code, "malformed_response"))
		return "ModemManager returned malformed or oversized expert data";
	if (g_str_equal(code, "busy"))
		return "Another per-modem expert operation is in progress";
	if (g_str_equal(code, "not_ready"))
		return "Supported LTE bands are not available";
	if (g_str_equal(code, "outcome_unknown"))
		return "The command was dispatched but its final hardware outcome is unknown";
	if (g_str_equal(code, "reprobe_timeout"))
		return "The attested modem did not reappear before the reprobe deadline";
	if (g_str_equal(code, "registration_timeout"))
		return "The replacement modem did not register before the deadline";
	if (g_str_equal(code, "verification_mismatch"))
		return "The post-reset NVM or serving-cell state does not match the request";
	if (g_str_equal(code, "operation_failed"))
		return "The reviewed firmware command was rejected before verification";
	return "The expert command transport is unavailable";
}

static int
send_l850_error(struct ubus_context *context,
		struct ubus_request_data *request, L850GLModem *modem,
		const gchar *code, gboolean retryable, guint32 retry_after_ms)
{
	struct blob_buf buffer = {};
	void *error;

	blob_buf_init(&buffer, 0);
	add_common(&buffer, FALSE);
	if (modem != NULL)
		add_modem_identity(&buffer, modem);
	blobmsg_add_string(&buffer, "state", code);
	if (retry_after_ms > 0U)
		blobmsg_add_u32(&buffer, "retry_after_ms", retry_after_ms);
	error = blobmsg_open_table(&buffer, "error");
	blobmsg_add_string(&buffer, "code", code);
	blobmsg_add_string(&buffer, "message", l850_error_message(code));
	blobmsg_add_u8(&buffer, "retryable", retryable);
	blobmsg_close_table(&buffer, error);
	return send_buffer(context, request, &buffer);
}

static L850GLModem *
l850_requested_modem(L850GLUbus *ubus, struct blob_attr **parsed,
		     guint modem_index, guint generation_index,
		     const gchar **error_code)
{
	const gchar *modem_id = blobmsg_get_string(parsed[modem_index]);
	L850GLModem *modem;

	if (!l850gl_identity_is_valid(modem_id)) {
		*error_code = "invalid_argument";
		return NULL;
	}
	modem = l850gl_bridge_find_modem(ubus->bridge, modem_id);
	if (modem == NULL) {
		*error_code = l850gl_bridge_manager_available(ubus->bridge) ?
			"stale_identity" : "dependency_unavailable";
		return NULL;
	}
	if (modem->generation != blobmsg_get_u32(parsed[generation_index])) {
		*error_code = "stale_generation";
		return NULL;
	}
	if (!l850gl_modem_attest_mutation_target(modem)) {
		*error_code = "unsupported";
		return NULL;
	}
#ifdef L850GL_MM_EXPERT
	if (l850_modem_has_active_mutation(ubus, modem) ||
	    l850_modem_has_active_scan(ubus, modem) ||
	    l850_modem_has_active_carrier_query(ubus, modem) ||
	    modem->l850_voltage_refresh_pending) {
		*error_code = "busy";
		return NULL;
	}
#endif
	*error_code = NULL;
	return modem;
}

static gboolean
l850_firmware_allowed(L850GLModem *modem)
{
	return l850gl_l850_firmware_is_allowed(
		mm_modem_get_revision(modem->modem));
}

static gboolean l850_band_is_supported(L850GLModem *modem, guint16 band);
static L850NormalizedError l850_normalize_error(GError *error,
						gboolean timed_out,
						gboolean transport_lost);

static gboolean
l850_supported_lte_bands_available(L850GLModem *modem)
{
	struct L850GLRadioBand band_choices[MAX_RADIO_BANDS];
	guint band_choice_count = 0U;
	guint supported_families = L850GL_RADIO_FAMILY_NONE;

	return snapshot_supported_radio_bands(modem, band_choices,
		&band_choice_count, &supported_families) &&
		band_choice_count > 0U &&
		(supported_families & L850GL_RADIO_FAMILY_4G) != 0U;
}

static void
l850_add_scan_capability(struct blob_buf *buffer, L850GLUbus *ubus,
			 L850GLModem *modem)
{
	guint32 retry_after = l850gl_l850_scan_retry_after_ms(
		g_get_monotonic_time(), modem->l850_last_scan_completed_at);
	void *scan = blobmsg_open_table(buffer, "scan");

	if (l850_modem_has_active_scan(ubus, modem)) {
		blobmsg_add_string(buffer, "state", "busy");
		blobmsg_add_u8(buffer, "available", FALSE);
		blobmsg_add_string(buffer, "reason",
			"per-modem-scan-in-progress");
	} else if (modem->mutation_busy ||
	    l850_modem_has_active_mutation(ubus, modem)) {
		blobmsg_add_string(buffer, "state", "busy");
		blobmsg_add_u8(buffer, "available", FALSE);
		blobmsg_add_string(buffer, "reason",
			"per-modem-mutation-in-progress");
	} else if (!l850_supported_lte_bands_available(modem)) {
		blobmsg_add_string(buffer, "state", "not_ready");
		blobmsg_add_u8(buffer, "available", FALSE);
		blobmsg_add_string(buffer, "reason",
			"supported-bands-not-advertised");
	} else if (retry_after > 0U) {
		blobmsg_add_string(buffer, "state", "rate_limited");
		blobmsg_add_u8(buffer, "available", FALSE);
		blobmsg_add_string(buffer, "reason", "scan-rate-limit");
		blobmsg_add_u32(buffer, "retry_after_ms", retry_after);
	} else {
		blobmsg_add_string(buffer, "state", "available");
		blobmsg_add_u8(buffer, "available", TRUE);
		blobmsg_add_string(buffer, "reason",
			l850_firmware_allowed(modem) ?
			"standard-with-live-validated-xmci-fallback" :
			"standard-modemmanager-get-cell-info-only");
	}
	blobmsg_add_string(buffer, "source", "modemmanager");
	blobmsg_close_table(buffer, scan);
}

static void
l850_add_lock_observation(struct blob_buf *buffer,
			  const struct L850GLL850LockState *state)
{
	void *lock = blobmsg_open_table(buffer, "lock");

	blobmsg_add_string(buffer, "state", !state->enabled ? "clear" :
		(state->has_pci ? "configured_exact" :
		 "configured_earfcn"));
	blobmsg_add_u8(buffer, "enabled", state->enabled);
	blobmsg_add_u8(buffer, "postcondition_verified", FALSE);
	if (state->enabled) {
		blobmsg_add_u32(buffer, "earfcn", state->earfcn);
		blobmsg_add_u32(buffer, "band", state->band);
		if (state->has_pci)
			blobmsg_add_u32(buffer, "pci", state->pci);
	}
	blobmsg_add_string(buffer, "source", "l850-nvm-via-modemmanager");
	blobmsg_close_table(buffer, lock);
}

static gboolean
l850_status_timeout(gpointer user_data)
{
	L850StatusOperation *operation = user_data;

	operation->timeout_source = 0U;
	operation->timed_out = TRUE;
	g_cancellable_cancel(operation->cancellable);
	return G_SOURCE_REMOVE;
}

static void
l850_status_parent_cancelled(GCancellable *parent, gpointer user_data)
{
	L850StatusOperation *operation = user_data;

	(void)parent;
	g_cancellable_cancel(operation->cancellable);
}

static void
l850_status_operation_free(L850StatusOperation *operation)
{
	L850GLUbus *ubus = operation->ubus;

	if (operation->timeout_source != 0U)
		g_source_remove(operation->timeout_source);
	if (operation->parent_cancel_handler != 0U &&
	    operation->modem->cancellable != NULL)
		g_cancellable_disconnect(operation->modem->cancellable,
			operation->parent_cancel_handler);
	g_clear_object(&operation->cancellable);
	l850gl_modem_unref(operation->modem);
	if (ubus != NULL && ubus->l850_status_operations != NULL)
		g_hash_table_remove(ubus->l850_status_operations, operation);
	g_free(operation);
	l850gl_ubus_unref_internal(ubus);
}

static void
l850_status_complete_buffer(L850StatusOperation *operation,
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
	l850_status_operation_free(operation);
}

static void
l850_status_complete_error(L850StatusOperation *operation, const gchar *code,
			   gboolean retryable)
{
	struct blob_buf buffer = {};
	void *error;

	blob_buf_init(&buffer, 0);
	add_common(&buffer, FALSE);
	add_modem_identity(&buffer, operation->modem);
	blobmsg_add_string(&buffer, "state", code);
	error = blobmsg_open_table(&buffer, "error");
	blobmsg_add_string(&buffer, "code", code);
	blobmsg_add_string(&buffer, "message", l850_error_message(code));
	blobmsg_add_u8(&buffer, "retryable", retryable);
	blobmsg_close_table(&buffer, error);
	l850_status_complete_buffer(operation, &buffer);
}

static void
l850_status_ready(GObject *source, GAsyncResult *result, gpointer user_data)
{
	L850StatusOperation *operation = user_data;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *response = NULL;
	struct L850GLL850LockState lock_state;
	L850NormalizedError normalized;
	enum L850GLL850CellParseResult parse_result;
	struct blob_buf buffer = {};
	guint32 retry_after;
	gboolean mutable;

	response = mm_modem_command_finish(MM_MODEM(source), result, &error);
	if (!operation->modem->live ||
	    source != G_OBJECT(operation->modem->modem)) {
		l850_status_complete_error(operation, "device_gone", FALSE);
		return;
	}
	if (operation->modem->generation != operation->generation) {
		l850_status_complete_error(operation, "stale_generation", FALSE);
		return;
	}
	if (error != NULL || response == NULL) {
		normalized = l850_normalize_error(error, operation->timed_out,
			operation->transport_lost);
		l850_status_complete_error(operation, normalized.code,
			normalized.retryable);
		return;
	}
	parse_result = l850gl_l850_nvm_parse(response, strlen(response),
		&lock_state);
	if (parse_result != L850GL_L850_CELL_PARSE_OK) {
		l850_status_complete_error(operation, "malformed_response", FALSE);
		return;
	}
	blob_buf_init(&buffer, 0);
	add_common(&buffer, TRUE);
	add_modem_identity(&buffer, operation->modem);
	retry_after = advanced_retry_after_ms(operation->ubus, operation->modem);
	mutable = !operation->modem->mutation_busy && retry_after == 0U &&
		!l850_modem_has_active_mutation(operation->ubus,
			operation->modem);
	blobmsg_add_string(&buffer, "state", mutable ? "available" : "busy");
	blobmsg_add_u8(&buffer, "mutable", mutable);
	blobmsg_add_string(&buffer, "reason", mutable ?
		"live-validated-firmware-and-nvm-state" :
		(retry_after > 0U ? "advanced-cooldown" :
		 "per-modem-mutation-in-progress"));
	if (retry_after > 0U)
		blobmsg_add_u32(&buffer, "retry_after_ms", retry_after);
	l850_add_lock_observation(&buffer, &lock_state);
	l850_add_scan_capability(&buffer, operation->ubus, operation->modem);
	l850_status_complete_buffer(operation, &buffer);
}

static L850StatusOperation *
l850_status_operation_new(L850GLUbus *ubus, L850GLModem *modem,
			  struct ubus_context *context,
			  struct ubus_request_data *request)
{
	L850StatusOperation *operation = g_new0(L850StatusOperation, 1);

	operation->ubus = l850gl_ubus_ref_internal(ubus);
	operation->modem = l850gl_modem_ref(modem);
	operation->cancellable = g_cancellable_new();
	operation->generation = modem->generation;
	ubus_defer_request(context, request, &operation->request);
	operation->deferred = TRUE;
	g_hash_table_add(ubus->l850_status_operations, operation);
	operation->timeout_source = g_timeout_add_seconds(
		L850_STATUS_OPERATION_TIMEOUT_SECONDS, l850_status_timeout,
		operation);
	if (modem->cancellable != NULL)
		operation->parent_cancel_handler = g_cancellable_connect(
			modem->cancellable,
			G_CALLBACK(l850_status_parent_cancelled), operation, NULL);
	return operation;
}

static int
method_cell_lock_status(struct ubus_context *context,
			struct ubus_object *object,
			struct ubus_request_data *request, const char *method,
			struct blob_attr *message)
{
	L850GLUbus *ubus = from_l850_object(object);
	struct blob_attr *parsed[__L850_STATUS_MAX] = {};
	const gchar *error_code;
	L850GLModem *modem;
	struct blob_buf buffer = {};
	L850StatusOperation *operation;

	(void)method;
	if (!parse_exact_fields(message, l850_status_policy, __L850_STATUS_MAX,
		(G_GUINT64_CONSTANT(1) << __L850_STATUS_MAX) - 1U, parsed))
		return send_l850_error(context, request, NULL,
			"invalid_argument", FALSE, 0U);
	modem = l850_requested_modem(ubus, parsed, L850_MODEM_ID,
		L850_GENERATION, &error_code);
	if (modem == NULL)
		return send_l850_error(context, request, NULL, error_code,
			g_str_equal(error_code, "dependency_unavailable") ||
			g_str_equal(error_code, "busy"), 0U);
	if (!l850_supported_lte_bands_available(modem))
		return send_l850_error(context, request, modem, "not_ready", TRUE,
			0U);
	if (!l850_firmware_allowed(modem)) {
		blob_buf_init(&buffer, 0);
		add_common(&buffer, TRUE);
		add_modem_identity(&buffer, modem);
		blobmsg_add_string(&buffer, "state", "unsupported_firmware");
		blobmsg_add_u8(&buffer, "mutable", FALSE);
		blobmsg_add_string(&buffer, "reason",
			"firmware-not-live-validated");
		l850_add_scan_capability(&buffer, ubus, modem);
		return send_buffer(context, request, &buffer);
	}
	operation = l850_status_operation_new(ubus, modem, context, request);
	mm_modem_command(operation->modem->modem,
		l850gl_l850_nvm_query_command(), L850_COMMAND_TIMEOUT_SECONDS,
		operation->cancellable, l850_status_ready, operation);
	return UBUS_STATUS_OK;
}

static gboolean
l850_band_is_supported(L850GLModem *modem, guint16 band)
{
	return lte_band_is_supported(modem, band);
}

static gboolean
l850_parse_standard_pci(const gchar *value, guint16 *pci)
{
	return parse_standard_lte_pci(value, pci);
}

static gboolean
l850_normalize_standard_cells(L850GLModem *modem, GList *items,
			      L850StandardCell *normalized, guint *n_normalized)
{
	guint count = 0U;
	gboolean serving_seen = FALSE;
	GList *cursor;

	for (cursor = items; cursor != NULL; cursor = cursor->next) {
		MMCellInfo *cell = cursor->data;
		MMCellInfoLte *lte;
		L850StandardCell *output;
		guint earfcn;
		guint16 band;
		gboolean serving;

		if (!MM_IS_CELL_INFO(cell))
			return FALSE;
		if (mm_cell_info_get_cell_type(cell) != MM_CELL_TYPE_LTE)
			continue;
		if (!MM_IS_CELL_INFO_LTE(cell) ||
		    count >= L850GL_L850_CELL_MAX_RESULTS)
			return FALSE;
		lte = MM_CELL_INFO_LTE(cell);
		earfcn = mm_cell_info_lte_get_earfcn(lte);
		if (earfcn == G_MAXUINT ||
		    !l850gl_l850_earfcn_to_band(earfcn, &band) ||
		    !l850_band_is_supported(modem, band))
			return FALSE;
		serving = mm_cell_info_get_serving(cell);
		if (serving && serving_seen)
			return FALSE;
		serving_seen = serving_seen || serving;
		output = &normalized[count];
		memset(output, 0, sizeof(*output));
		output->type = serving ? 4U : 5U;
		output->serving = serving;
		output->earfcn = earfcn;
		output->band = band;
		if (!l850_parse_standard_pci(
			mm_cell_info_lte_get_physical_ci(lte), &output->pci))
			return FALSE;
		output->rsrp = mm_cell_info_lte_get_rsrp(lte);
		output->rsrq = mm_cell_info_lte_get_rsrq(lte);
		if (output->rsrp != -G_MAXDOUBLE) {
			if (!isfinite(output->rsrp))
				return FALSE;
			output->has_rsrp = TRUE;
		}
		if (output->rsrq != -G_MAXDOUBLE) {
			if (!isfinite(output->rsrq))
				return FALSE;
			output->has_rsrq = TRUE;
		}
		count++;
	}
	*n_normalized = count;
	return TRUE;
}

static const gchar *
l850_scan_stale_code(L850ScanOperation *operation, GObject *source)
{
	if (!operation->modem->live)
		return "device_gone";
	if (operation->modem->generation != operation->generation ||
	    source != G_OBJECT(operation->modem->modem))
		return "stale_generation";
	return NULL;
}

static gboolean
l850_scan_timeout(gpointer user_data)
{
	L850ScanOperation *operation = user_data;

	operation->timeout_source = 0U;
	operation->timed_out = TRUE;
	g_cancellable_cancel(operation->cancellable);
	return G_SOURCE_REMOVE;
}

static void
l850_scan_parent_cancelled(GCancellable *parent, gpointer user_data)
{
	L850ScanOperation *operation = user_data;

	(void)parent;
	g_cancellable_cancel(operation->cancellable);
}

static void
l850_scan_operation_free(L850ScanOperation *operation)
{
	L850GLUbus *ubus = operation->ubus;

	if (operation->timeout_source != 0U)
		g_source_remove(operation->timeout_source);
	if (operation->parent_cancel_handler != 0U &&
	    operation->modem->cancellable != NULL)
		g_cancellable_disconnect(operation->modem->cancellable,
			operation->parent_cancel_handler);
	g_clear_object(&operation->cancellable);
	l850gl_modem_unref(operation->modem);
	if (ubus != NULL && ubus->l850_scan_operations != NULL)
		g_hash_table_remove(ubus->l850_scan_operations, operation);
	g_free(operation);
	l850gl_ubus_unref_internal(ubus);
}

static void
l850_scan_complete_buffer(L850ScanOperation *operation,
			  struct blob_buf *buffer)
{
	operation->modem->l850_last_scan_completed_at =
		g_get_monotonic_time();
	if (operation->deferred && operation->ubus->connected &&
	    operation->ubus->context_initialized && !operation->ubus->stopping) {
		(void)ubus_send_reply(&operation->ubus->context,
			&operation->request, buffer->head);
		ubus_complete_deferred_request(&operation->ubus->context,
			&operation->request, UBUS_STATUS_OK);
	}
	operation->deferred = FALSE;
	blob_buf_free(buffer);
	l850_scan_operation_free(operation);
}

static void
l850_scan_complete_error(L850ScanOperation *operation, const gchar *code,
			 gboolean retryable)
{
	struct blob_buf buffer = {};
	void *error;

	blob_buf_init(&buffer, 0);
	add_common(&buffer, FALSE);
	add_modem_identity(&buffer, operation->modem);
	blobmsg_add_string(&buffer, "state", code);
	error = blobmsg_open_table(&buffer, "error");
	blobmsg_add_string(&buffer, "code", code);
	blobmsg_add_string(&buffer, "message", l850_error_message(code));
	blobmsg_add_u8(&buffer, "retryable", retryable);
	blobmsg_close_table(&buffer, error);
	l850_scan_complete_buffer(operation, &buffer);
}

static void
l850_scan_complete_success(L850ScanOperation *operation,
			   const L850StandardCell *cells, guint n_cells)
{
	struct blob_buf buffer = {};
	void *array;
	guint index;
	guint serving_count = 0U;
	const L850StandardCell *serving = NULL;

	for (index = 0U; index < n_cells; index++) {
		if (cells[index].serving) {
			serving = &cells[index];
			serving_count++;
		}
	}
	if (serving_count == 1U)
		(void)serving_cell_cache_store(operation->modem,
			serving->earfcn, serving->pci, serving->band,
			serving->rsrp, serving->has_rsrp,
			serving->rsrq, serving->has_rsrq,
			operation->vendor_fallback ? "expert-xmci-scan" :
			"expert-standard-cell-scan");

	blob_buf_init(&buffer, 0);
	add_common(&buffer, TRUE);
	add_modem_identity(&buffer, operation->modem);
	blobmsg_add_string(&buffer, "state", "scan_ready");
	blobmsg_add_string(&buffer, "source", "modemmanager");
	blobmsg_add_string(&buffer, "method", operation->vendor_fallback ?
		"l850-xmci" : "standard-cell-info");
	array = blobmsg_open_array(&buffer, "cells");
	for (index = 0U; index < n_cells; index++) {
		const L850StandardCell *cell = &cells[index];
		void *entry = blobmsg_open_table(&buffer, NULL);

		blobmsg_add_u32(&buffer, "type", cell->type);
		blobmsg_add_u8(&buffer, "serving", cell->serving);
		blobmsg_add_u32(&buffer, "earfcn", cell->earfcn);
		blobmsg_add_u32(&buffer, "pci", cell->pci);
		blobmsg_add_u32(&buffer, "band", cell->band);
		if (cell->has_rsrp)
			blobmsg_add_double(&buffer, "rsrp", cell->rsrp);
		if (cell->has_rsrq)
			blobmsg_add_double(&buffer, "rsrq", cell->rsrq);
		blobmsg_close_table(&buffer, entry);
	}
	blobmsg_close_array(&buffer, array);
	l850_scan_complete_buffer(operation, &buffer);
}

static gboolean
l850_error_is_unsupported(GError *error)
{
	g_autofree gchar *remote = NULL;

	if (error != NULL && g_dbus_error_is_remote_error(error))
		remote = g_dbus_error_get_remote_error(error);
	return error != NULL &&
		(g_error_matches(error, G_DBUS_ERROR,
				 G_DBUS_ERROR_UNKNOWN_METHOD) ||
		 g_error_matches(error, G_DBUS_ERROR,
				 G_DBUS_ERROR_NOT_SUPPORTED) ||
		 g_error_matches(error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED) ||
		 g_error_matches(error, MM_MOBILE_EQUIPMENT_ERROR,
				 MM_MOBILE_EQUIPMENT_ERROR_NOT_SUPPORTED) ||
		 remote_error_has_suffix(remote, ".Core.Unsupported") ||
		 remote_error_has_suffix(remote,
			".MobileEquipment.NotSupported"));
}

static L850NormalizedError
l850_normalize_error(GError *error, gboolean timed_out,
		     gboolean transport_lost)
{
	g_autofree gchar *remote = NULL;

	if (error != NULL && g_dbus_error_is_remote_error(error))
		remote = g_dbus_error_get_remote_error(error);
	if (timed_out)
		return (L850NormalizedError){ "timeout", TRUE };
	if (transport_lost)
		return (L850NormalizedError){ "dependency_unavailable", TRUE };
	if (error == NULL)
		return (L850NormalizedError){ "unavailable", FALSE };
	if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT) ||
	    g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_NO_REPLY) ||
	    g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_TIMEOUT) ||
	    g_error_matches(error, MM_CORE_ERROR, MM_CORE_ERROR_TIMEOUT) ||
	    remote_error_has_suffix(remote, ".Core.Timeout"))
		return (L850NormalizedError){ "timeout", TRUE };
	if (g_error_matches(error, G_DBUS_ERROR,
			    G_DBUS_ERROR_SERVICE_UNKNOWN) ||
	    g_error_matches(error, G_DBUS_ERROR,
			    G_DBUS_ERROR_NAME_HAS_NO_OWNER) ||
	    g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CLOSED) ||
	    g_error_matches(error, G_IO_ERROR, G_IO_ERROR_BROKEN_PIPE))
		return (L850NormalizedError){ "dependency_unavailable", TRUE };
	if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED) ||
	    g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED) ||
	    g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_AUTH_FAILED) ||
	    g_error_matches(error, MM_CORE_ERROR, MM_CORE_ERROR_UNAUTHORIZED) ||
	    remote_error_has_suffix(remote, ".Core.Unauthorized"))
		return (L850NormalizedError){ "permission_denied", FALSE };
	if (g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD) ||
	    g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED) ||
	    g_error_matches(error, MM_CORE_ERROR, MM_CORE_ERROR_UNSUPPORTED) ||
	    g_error_matches(error, MM_MOBILE_EQUIPMENT_ERROR,
			    MM_MOBILE_EQUIPMENT_ERROR_NOT_SUPPORTED) ||
	    remote_error_has_suffix(remote, ".Core.Unsupported") ||
	    remote_error_has_suffix(remote,
		".MobileEquipment.NotSupported"))
		return (L850NormalizedError){ "unsupported_firmware", FALSE };
	if (g_error_matches(error, MM_CORE_ERROR, MM_CORE_ERROR_IN_PROGRESS) ||
	    g_error_matches(error, MM_CORE_ERROR, MM_CORE_ERROR_THROTTLED) ||
	    remote_error_has_suffix(remote, ".Core.InProgress") ||
	    remote_error_has_suffix(remote, ".Core.Throttled"))
		return (L850NormalizedError){ "busy", TRUE };
	if (g_error_matches(error, MM_CORE_ERROR, MM_CORE_ERROR_WRONG_STATE) ||
	    g_error_matches(error, MM_CORE_ERROR, MM_CORE_ERROR_RETRY) ||
	    remote_error_has_suffix(remote, ".Core.WrongState") ||
	    remote_error_has_suffix(remote, ".Core.Retry"))
		return (L850NormalizedError){ "not_ready", TRUE };
	return (L850NormalizedError){ "unavailable", FALSE };
}

static L850NormalizedError
l850_normalize_scan_error(GError *error, L850ScanOperation *operation)
{
	return l850_normalize_error(error, operation->timed_out,
		operation->transport_lost);
}

static gboolean
l850_normalize_vendor_cells(L850GLModem *modem,
			    const struct L850GLL850CellScan *scan,
			    L850StandardCell *normalized,
			    guint *n_normalized)
{
	size_t index;

	if (scan == NULL || scan->length > L850GL_L850_CELL_MAX_RESULTS)
		return FALSE;
	for (index = 0U; index < scan->length; index++) {
		const struct L850GLL850Cell *input = &scan->cells[index];
		L850StandardCell *output = &normalized[index];

		if (!l850_band_is_supported(modem, input->band))
			return FALSE;
		memset(output, 0, sizeof(*output));
		output->type = input->type;
		output->serving = input->serving;
		output->earfcn = input->earfcn;
		output->pci = input->pci;
		output->band = input->band;
		output->rsrp = (gdouble)input->rsrp_dbm;
		output->rsrq = (gdouble)input->rsrq_tenths_db / 10.0;
		output->has_rsrp = TRUE;
		output->has_rsrq = TRUE;
	}
	*n_normalized = (guint)scan->length;
	return TRUE;
}

static void
l850_vendor_scan_ready(GObject *source, GAsyncResult *result,
		       gpointer user_data)
{
	L850ScanOperation *operation = user_data;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *response = NULL;
	struct L850GLL850CellScan parsed;
	L850StandardCell normalized[L850GL_L850_CELL_MAX_RESULTS];
	L850NormalizedError normalized_error;
	enum L850GLL850CellParseResult parse_result;
	const gchar *stale;
	guint n_cells = 0U;

	response = mm_modem_command_finish(MM_MODEM(source), result, &error);
	stale = l850_scan_stale_code(operation, source);
	if (stale != NULL) {
		l850_scan_complete_error(operation, stale, FALSE);
		return;
	}
	if (error != NULL || response == NULL) {
		normalized_error = l850_normalize_scan_error(error, operation);
		l850_scan_complete_error(operation, normalized_error.code,
			normalized_error.retryable);
		return;
	}
	parse_result = l850gl_l850_cell_parse(response, strlen(response),
		&parsed);
	if (parse_result != L850GL_L850_CELL_PARSE_OK ||
	    !l850_normalize_vendor_cells(operation->modem, &parsed, normalized,
		&n_cells)) {
		l850_scan_complete_error(operation, "malformed_response", FALSE);
		return;
	}
	l850_scan_complete_success(operation, normalized, n_cells);
}

static void
l850_scan_ready(GObject *source, GAsyncResult *result, gpointer user_data)
{
	L850ScanOperation *operation = user_data;
	g_autoptr(GError) error = NULL;
	L850StandardCell normalized[L850GL_L850_CELL_MAX_RESULTS];
	L850NormalizedError normalized_error;
	const gchar *stale;
	GList *cells;
	guint n_cells = 0U;

	cells = mm_modem_get_cell_info_finish(MM_MODEM(source), result, &error);
	stale = l850_scan_stale_code(operation, source);
	if (stale != NULL) {
		g_list_free_full(cells, g_object_unref);
		l850_scan_complete_error(operation, stale, FALSE);
		return;
	}
	if (error != NULL) {
		if (l850_error_is_unsupported(error) &&
		    l850_firmware_allowed(operation->modem)) {
			g_list_free_full(cells, g_object_unref);
			operation->vendor_fallback = TRUE;
			mm_modem_command(operation->modem->modem,
				l850gl_l850_scan_command(),
				L850_COMMAND_TIMEOUT_SECONDS,
				operation->cancellable, l850_vendor_scan_ready,
				operation);
			return;
		}
		normalized_error = l850_normalize_scan_error(error, operation);
		g_list_free_full(cells, g_object_unref);
		l850_scan_complete_error(operation, normalized_error.code,
			normalized_error.retryable);
		return;
	}
	if (!l850_normalize_standard_cells(operation->modem, cells, normalized,
		&n_cells)) {
		g_list_free_full(cells, g_object_unref);
		l850_scan_complete_error(operation, "malformed_response", FALSE);
		return;
	}
	g_list_free_full(cells, g_object_unref);
	l850_scan_complete_success(operation, normalized, n_cells);
}

static L850ScanOperation *
l850_scan_operation_new(L850GLUbus *ubus, L850GLModem *modem,
			struct ubus_context *context,
			struct ubus_request_data *request)
{
	L850ScanOperation *operation = g_new0(L850ScanOperation, 1);

	operation->ubus = l850gl_ubus_ref_internal(ubus);
	operation->modem = l850gl_modem_ref(modem);
	operation->cancellable = g_cancellable_new();
	operation->generation = modem->generation;
	ubus_defer_request(context, request, &operation->request);
	operation->deferred = TRUE;
	g_hash_table_add(ubus->l850_scan_operations, operation);
	operation->timeout_source = g_timeout_add_seconds(
		L850_SCAN_OPERATION_TIMEOUT_SECONDS, l850_scan_timeout, operation);
	if (modem->cancellable != NULL)
		operation->parent_cancel_handler = g_cancellable_connect(
			modem->cancellable, G_CALLBACK(l850_scan_parent_cancelled),
			operation, NULL);
	return operation;
}

static int
method_cell_scan(struct ubus_context *context, struct ubus_object *object,
		 struct ubus_request_data *request, const char *method,
		 struct blob_attr *message)
{
	L850GLUbus *ubus = from_l850_object(object);
	struct blob_attr *parsed[__L850_STATUS_MAX] = {};
	struct L850GLRadioBand band_choices[MAX_RADIO_BANDS];
	const gchar *error_code;
	L850GLModem *modem;
	L850ScanOperation *operation;
	guint band_choice_count = 0U;
	guint supported_families = L850GL_RADIO_FAMILY_NONE;
	gint64 now;
	guint32 retry_after;

	(void)method;
	if (!parse_exact_fields(message, l850_status_policy, __L850_STATUS_MAX,
		(G_GUINT64_CONSTANT(1) << __L850_STATUS_MAX) - 1U, parsed))
		return send_l850_error(context, request, NULL,
			"invalid_argument", FALSE, 0U);
	modem = l850_requested_modem(ubus, parsed, L850_MODEM_ID,
		L850_GENERATION, &error_code);
	if (modem == NULL)
		return send_l850_error(context, request, NULL, error_code,
			g_str_equal(error_code, "dependency_unavailable") ||
			g_str_equal(error_code, "busy"), 0U);
	if (modem->mutation_busy)
		return send_l850_error(context, request, modem, "busy", TRUE, 0U);
	if (l850_modem_has_active_scan(ubus, modem))
		return send_l850_error(context, request, modem, "busy", TRUE, 0U);
	if (!snapshot_supported_radio_bands(modem, band_choices,
		&band_choice_count, &supported_families) ||
	    (supported_families & L850GL_RADIO_FAMILY_4G) == 0U)
		return send_l850_error(context, request, modem,
			"not_ready", TRUE, 0U);
	(void)band_choice_count;
	(void)supported_families;
	now = g_get_monotonic_time();
	retry_after = l850gl_l850_scan_retry_after_ms(now,
		modem->l850_last_scan_completed_at);
	if (retry_after > 0U)
		return send_l850_error(context, request, modem,
			"rate_limited", TRUE, retry_after);
	operation = l850_scan_operation_new(ubus, modem, context, request);
	mm_modem_get_cell_info(operation->modem->modem, operation->cancellable,
		l850_scan_ready, operation);
	return UBUS_STATUS_OK;
}

static const gchar *
l850_carrier_stale_code(L850CarrierOperation *operation, GObject *source)
{
	if (!operation->modem->live)
		return "device_gone";
	if (operation->modem->generation != operation->generation ||
	    source != G_OBJECT(operation->modem->modem))
		return "stale_generation";
	return NULL;
}

static gboolean
l850_carrier_timeout(gpointer user_data)
{
	L850CarrierOperation *operation = user_data;

	operation->timeout_source = 0U;
	operation->timed_out = TRUE;
	g_cancellable_cancel(operation->cancellable);
	return G_SOURCE_REMOVE;
}

static void
l850_carrier_parent_cancelled(GCancellable *parent, gpointer user_data)
{
	L850CarrierOperation *operation = user_data;

	(void)parent;
	g_cancellable_cancel(operation->cancellable);
}

static void
l850_carrier_operation_free(L850CarrierOperation *operation)
{
	L850GLUbus *ubus = operation->ubus;

	if (operation->timeout_source != 0U)
		g_source_remove(operation->timeout_source);
	if (operation->parent_cancel_handler != 0U &&
	    operation->modem->cancellable != NULL)
		g_cancellable_disconnect(operation->modem->cancellable,
			operation->parent_cancel_handler);
	g_clear_object(&operation->cancellable);
	l850gl_modem_unref(operation->modem);
	if (ubus != NULL && ubus->l850_carrier_operations != NULL)
		g_hash_table_remove(ubus->l850_carrier_operations, operation);
	g_free(operation);
	l850gl_ubus_unref_internal(ubus);
}

static void
l850_carrier_complete_buffer(L850CarrierOperation *operation,
			     struct blob_buf *buffer)
{
	operation->modem->l850_last_carrier_query_completed_at =
		g_get_monotonic_time();
	if (operation->deferred && operation->ubus->connected &&
	    operation->ubus->context_initialized && !operation->ubus->stopping) {
		(void)ubus_send_reply(&operation->ubus->context,
			&operation->request, buffer->head);
		ubus_complete_deferred_request(&operation->ubus->context,
			&operation->request, UBUS_STATUS_OK);
	}
	operation->deferred = FALSE;
	blob_buf_free(buffer);
	l850_carrier_operation_free(operation);
}

static void
l850_carrier_complete_error(L850CarrierOperation *operation,
			    const gchar *code, gboolean retryable)
{
	struct blob_buf buffer = {};
	void *error;

	blob_buf_init(&buffer, 0);
	add_common(&buffer, FALSE);
	add_modem_identity(&buffer, operation->modem);
	blobmsg_add_string(&buffer, "state", code);
	error = blobmsg_open_table(&buffer, "error");
	blobmsg_add_string(&buffer, "code", code);
	blobmsg_add_string(&buffer, "message", l850_error_message(code));
	blobmsg_add_u8(&buffer, "retryable", retryable);
	blobmsg_close_table(&buffer, error);
	l850_carrier_complete_buffer(operation, &buffer);
}

static void
l850_add_ca_carrier(struct blob_buf *buffer,
		    const struct L850GLL850CaCarrier *carrier)
{
	blobmsg_add_u32(buffer, "index", carrier->index);
	blobmsg_add_u32(buffer, "band", carrier->band);
	blobmsg_add_u32(buffer, "earfcn", carrier->dl_earfcn);
	blobmsg_add_u32(buffer, "pci", carrier->pci);
	blobmsg_add_double(buffer, "dl_bandwidth_mhz",
		(gdouble)carrier->dl_bandwidth_tenths_mhz / 10.0);
	blobmsg_add_double(buffer, "ul_bandwidth_mhz",
		(gdouble)carrier->ul_bandwidth_tenths_mhz / 10.0);
}

static void
l850_carrier_complete_success(L850CarrierOperation *operation,
			      const struct L850GLL850CaInfo *info)
{
	const struct L850GLL850CaCarrier *primary = NULL;
	guint16 unique_bands[L850GL_L850_CA_MAX_SLOTS];
	guint n_unique_bands = 0U;
	struct blob_buf buffer = {};
	void *active_bands;
	void *primary_entry;
	void *secondary;
	size_t index;

	memset(unique_bands, 0, sizeof(unique_bands));
	for (index = 0U; index < info->length; index++) {
		const struct L850GLL850CaCarrier *carrier =
			&info->carriers[index];
		guint band_index;

		if (carrier->primary)
			primary = carrier;
		for (band_index = 0U; band_index < n_unique_bands;
		     band_index++) {
			if (unique_bands[band_index] == carrier->band)
				break;
		}
		if (band_index == n_unique_bands)
			unique_bands[n_unique_bands++] = carrier->band;
	}
	if (primary == NULL) {
		l850_carrier_complete_error(operation, "malformed_response", FALSE);
		return;
	}

	blob_buf_init(&buffer, 0);
	add_common(&buffer, TRUE);
	add_modem_identity(&buffer, operation->modem);
	blobmsg_add_string(&buffer, "state", "available");
	blobmsg_add_string(&buffer, "source", "modemmanager");
	blobmsg_add_string(&buffer, "method", "l850-gtcainfo");
	active_bands = blobmsg_open_array(&buffer, "active_bands");
	for (index = 0U; index < n_unique_bands; index++)
		blobmsg_add_u32(&buffer, NULL, unique_bands[index]);
	blobmsg_close_array(&buffer, active_bands);
	primary_entry = blobmsg_open_table(&buffer, "primary");
	l850_add_ca_carrier(&buffer, primary);
	blobmsg_close_table(&buffer, primary_entry);
	secondary = blobmsg_open_array(&buffer, "secondary");
	for (index = 0U; index < info->length; index++) {
		const struct L850GLL850CaCarrier *carrier =
			&info->carriers[index];
		void *entry;

		if (carrier->primary)
			continue;
		entry = blobmsg_open_table(&buffer, NULL);
		l850_add_ca_carrier(&buffer, carrier);
		blobmsg_close_table(&buffer, entry);
	}
	blobmsg_close_array(&buffer, secondary);
	blobmsg_add_u32(&buffer, "active_carriers", (guint32)info->length);
	l850_carrier_complete_buffer(operation, &buffer);
}

static void
l850_carrier_ready(GObject *source, GAsyncResult *result, gpointer user_data)
{
	L850CarrierOperation *operation = user_data;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *response = NULL;
	struct L850GLL850CaInfo info;
	L850NormalizedError normalized;
	enum L850GLL850CaParseResult parse_result;
	const gchar *stale;
	gsize response_length;
	size_t index;

	response = mm_modem_command_finish(MM_MODEM(source), result, &error);
	stale = l850_carrier_stale_code(operation, source);
	if (stale != NULL) {
		l850_carrier_complete_error(operation, stale, FALSE);
		return;
	}
	if (!l850gl_modem_attest_mutation_target(operation->modem)) {
		l850_carrier_complete_error(operation, "unsupported", FALSE);
		return;
	}
	if (!l850_firmware_allowed(operation->modem)) {
		l850_carrier_complete_error(operation, "unsupported_firmware", FALSE);
		return;
	}
	if (error != NULL || response == NULL) {
		normalized = l850_normalize_error(error, operation->timed_out,
			operation->transport_lost);
		l850_carrier_complete_error(operation, normalized.code,
			normalized.retryable);
		return;
	}
	response_length = strnlen(response, L850GL_L850_CA_MAX_RESPONSE + 1U);
	parse_result = l850gl_l850_ca_parse(response, response_length, &info);
	if (parse_result != L850GL_L850_CA_PARSE_OK) {
		l850_carrier_complete_error(operation, "malformed_response", FALSE);
		return;
	}
	for (index = 0U; index < info.length; index++) {
		if (!l850_band_is_supported(operation->modem,
			info.carriers[index].band)) {
			l850_carrier_complete_error(operation,
				"malformed_response", FALSE);
			return;
		}
	}
	l850_carrier_complete_success(operation, &info);
}

static L850CarrierOperation *
l850_carrier_operation_new(L850GLUbus *ubus, L850GLModem *modem,
			   struct ubus_context *context,
			   struct ubus_request_data *request)
{
	L850CarrierOperation *operation = g_new0(L850CarrierOperation, 1);

	operation->ubus = l850gl_ubus_ref_internal(ubus);
	operation->modem = l850gl_modem_ref(modem);
	operation->cancellable = g_cancellable_new();
	operation->generation = modem->generation;
	ubus_defer_request(context, request, &operation->request);
	operation->deferred = TRUE;
	g_hash_table_add(ubus->l850_carrier_operations, operation);
	operation->timeout_source = g_timeout_add(
		L850_CARRIER_OPERATION_TIMEOUT_MS, l850_carrier_timeout, operation);
	if (modem->cancellable != NULL)
		operation->parent_cancel_handler = g_cancellable_connect(
			modem->cancellable,
			G_CALLBACK(l850_carrier_parent_cancelled), operation, NULL);
	return operation;
}

static int
method_get_carrier_info(struct ubus_context *context,
			struct ubus_object *object,
			struct ubus_request_data *request, const char *method,
			struct blob_attr *message)
{
	L850GLUbus *ubus = from_l850_object(object);
	struct blob_attr *parsed[__L850_STATUS_MAX] = {};
	const gchar *error_code;
	L850GLModem *modem;
	L850CarrierOperation *operation;
	guint32 retry_after;

	(void)method;
	if (!parse_exact_fields(message, l850_status_policy, __L850_STATUS_MAX,
		(G_GUINT64_CONSTANT(1) << __L850_STATUS_MAX) - 1U, parsed))
		return send_l850_error(context, request, NULL,
			"invalid_argument", FALSE, 0U);
	modem = l850_requested_modem(ubus, parsed, L850_MODEM_ID,
		L850_GENERATION, &error_code);
	if (modem == NULL)
		return send_l850_error(context, request, NULL, error_code,
			g_str_equal(error_code, "dependency_unavailable") ||
			g_str_equal(error_code, "busy"), 0U);
	if (modem->mutation_busy || l850_modem_has_active_scan(ubus, modem) ||
	    l850_modem_has_active_carrier_query(ubus, modem))
		return send_l850_error(context, request, modem, "busy", TRUE, 0U);
	if (!l850_firmware_allowed(modem))
		return send_l850_error(context, request, modem,
			"unsupported_firmware", FALSE, 0U);
	if (!l850_supported_lte_bands_available(modem))
		return send_l850_error(context, request, modem, "not_ready", TRUE,
			0U);
	retry_after = l850gl_l850_ca_retry_after_ms(g_get_monotonic_time(),
		modem->l850_last_carrier_query_completed_at);
	if (retry_after > 0U)
		return send_l850_error(context, request, modem, "rate_limited",
			TRUE, retry_after);
	operation = l850_carrier_operation_new(ubus, modem, context, request);
	mm_modem_command(operation->modem->modem,
		l850gl_l850_ca_query_command(),
		L850_CARRIER_COMMAND_TIMEOUT_SECONDS, operation->cancellable,
		l850_carrier_ready, operation);
	return UBUS_STATUS_OK;
}

static L850GLModem *
l850_mutation_response_modem(L850MutationOperation *operation)
{
	return operation->replacement != NULL ? operation->replacement :
		operation->original;
}

static void
l850_mutation_apply_cooldown(L850GLModem *modem)
{
	gint64 deadline;

	if (modem == NULL)
		return;
	deadline = g_get_monotonic_time() +
		((gint64)L850_POST_MUTATION_COOLDOWN_SECONDS * G_USEC_PER_SEC);
	modem->advanced_cooldown_until = MAX(modem->advanced_cooldown_until,
		deadline);
}

static void
l850_mutation_release_modem(L850GLModem *modem)
{
	if (modem == NULL)
		return;
	l850_mutation_apply_cooldown(modem);
	if (modem->mutation_kind == L850GL_MUTATION_L850) {
		g_clear_object(&modem->mutation_cancellable);
		modem->mutation_busy = FALSE;
		modem->mutation_kind = L850GL_MUTATION_NONE;
	}
}

static void
l850_mutation_operation_free(L850MutationOperation *operation)
{
	L850GLUbus *ubus = operation->ubus;

	if (operation->timeout_source != 0U)
		g_source_remove(operation->timeout_source);
	if (operation->poll_source != 0U)
		g_source_remove(operation->poll_source);
	l850_mutation_release_modem(operation->replacement);
	if (operation->original != operation->replacement)
		l850_mutation_release_modem(operation->original);
	g_clear_object(&operation->cancellable);
	if (ubus != NULL && ubus->l850_mutation_operations != NULL)
		g_hash_table_remove(ubus->l850_mutation_operations, operation);
	l850gl_modem_unref(operation->replacement);
	l850gl_modem_unref(operation->original);
	g_free(operation->physdev);
	g_free(operation);
	l850gl_ubus_unref_internal(ubus);
}

static void
l850_mutation_complete_buffer(L850MutationOperation *operation,
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
	l850_mutation_operation_free(operation);
}

static void
l850_mutation_complete_failure(L850MutationOperation *operation,
			       const gchar *state, const gchar *code,
			       gboolean retryable)
{
	struct blob_buf buffer = {};
	L850GLModem *modem = l850_mutation_response_modem(operation);
	void *error;

	blob_buf_init(&buffer, 0);
	add_common(&buffer, FALSE);
	if (modem != NULL)
		add_modem_identity(&buffer, modem);
	if (operation->replacement != NULL)
		blobmsg_add_u8(&buffer, "replacement", TRUE);
	blobmsg_add_string(&buffer, "state", state);
	blobmsg_add_u8(&buffer, "configuration_acknowledged",
		operation->configuration_acknowledged);
	error = blobmsg_open_table(&buffer, "error");
	blobmsg_add_string(&buffer, "code", code);
	blobmsg_add_string(&buffer, "message", l850_error_message(code));
	blobmsg_add_u8(&buffer, "retryable", retryable);
	blobmsg_close_table(&buffer, error);
	l850_mutation_complete_buffer(operation, &buffer);
}

static void
l850_mutation_complete_success(L850MutationOperation *operation,
			       const struct L850GLL850Cell *serving)
{
	struct blob_buf buffer = {};
	const gchar *state = operation->type == L850_MUTATION_CLEAR ?
		"cleared_verified" : "applied_verified";
	void *verification;

	blob_buf_init(&buffer, 0);
	add_common(&buffer, TRUE);
	add_modem_identity(&buffer, operation->replacement);
	blobmsg_add_u8(&buffer, "accepted", TRUE);
	blobmsg_add_u8(&buffer, "replacement", TRUE);
	blobmsg_add_string(&buffer, "operation",
		operation->type == L850_MUTATION_CLEAR ?
		"clear_cell_lock" : "set_cell_lock");
	blobmsg_add_string(&buffer, "state", state);
	blobmsg_add_u32(&buffer, "cooldown_ms",
		L850_POST_MUTATION_COOLDOWN_SECONDS * 1000U);
	verification = blobmsg_open_table(&buffer, "verification");
	blobmsg_add_u8(&buffer, "registration", TRUE);
	blobmsg_add_u8(&buffer, "nvm", TRUE);
	if (operation->type == L850_MUTATION_SET && serving != NULL) {
		blobmsg_add_u8(&buffer, "serving_cell", TRUE);
		blobmsg_add_u32(&buffer, "earfcn", serving->earfcn);
		blobmsg_add_u32(&buffer, "pci", serving->pci);
		blobmsg_add_u32(&buffer, "band", serving->band);
	}
	blobmsg_close_table(&buffer, verification);
	l850_mutation_complete_buffer(operation, &buffer);
}

static gboolean
l850_mutation_error_is_uncertain(L850MutationOperation *operation,
				 GError *error)
{
	g_autofree gchar *remote = NULL;

	if (error != NULL && g_dbus_error_is_remote_error(error))
		remote = g_dbus_error_get_remote_error(error);
	return operation->timed_out || operation->transport_lost ||
		advanced_error_has_unknown_outcome(error, remote) ||
		remote_error_has_suffix(remote, ".Core.Cancelled");
}

static gboolean
l850_mutation_replacement_is_valid(L850MutationOperation *operation,
				   GObject *source)
{
	const gchar *physdev;

	if (operation->replacement == NULL ||
	    !operation->replacement->live ||
	    operation->replacement->generation !=
		operation->replacement_generation ||
	    source != G_OBJECT(operation->replacement->modem) ||
	    !l850gl_modem_attest_mutation_target(operation->replacement) ||
	    !l850_firmware_allowed(operation->replacement))
		return FALSE;
	physdev = mm_modem_get_physdev(operation->replacement->modem);
	return physdev != NULL && g_str_equal(physdev, operation->physdev);
}

static void
l850_mutation_replace_cancellable(L850MutationOperation *operation)
{
	g_clear_object(&operation->cancellable);
	operation->cancellable = g_cancellable_new();
}

static gboolean l850_mutation_poll(gpointer user_data);

static void
l850_mutation_start_reprobe(L850MutationOperation *operation)
{
	operation->phase = L850_MUTATION_PHASE_REPROBE;
	operation->phase_deadline = g_get_monotonic_time() +
		((gint64)L850_REPROBE_TIMEOUT_SECONDS * G_USEC_PER_SEC);
	l850_mutation_replace_cancellable(operation);
	operation->poll_source = g_timeout_add(L850_REPROBE_POLL_MS,
		l850_mutation_poll, operation);
}

static void
l850_mutation_reset_ready(GObject *source, GAsyncResult *result,
			  gpointer user_data)
{
	L850MutationOperation *operation = user_data;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *response = NULL;
	L850NormalizedError normalized;

	response = mm_modem_command_finish(MM_MODEM(source), result, &error);
	operation->command_pending = FALSE;
	(void)response;
	if (operation->timed_out || operation->transport_lost) {
		l850_mutation_complete_failure(operation, "outcome_unknown",
			"outcome_unknown", FALSE);
		return;
	}
	if (error != NULL &&
	    !l850_mutation_error_is_uncertain(operation, error)) {
		normalized = l850_normalize_error(error, FALSE, FALSE);
		l850_mutation_complete_failure(operation,
			"lock_applied_reset_required",
			g_str_equal(normalized.code, "unavailable") ?
			"operation_failed" : normalized.code, FALSE);
		return;
	}
	/* CFUN=15 normally removes the source object and completes as Cancelled. */
	l850_mutation_start_reprobe(operation);
}

static void
l850_mutation_start_reset(L850MutationOperation *operation)
{
	const gchar *physdev = mm_modem_get_physdev(operation->original->modem);

	if (!operation->original->live ||
	    !l850gl_modem_attest_mutation_target(operation->original) ||
	    physdev == NULL || !g_str_equal(physdev, operation->physdev)) {
		l850_mutation_complete_failure(operation,
			"lock_applied_reset_required", "device_gone", FALSE);
		return;
	}
	operation->phase = L850_MUTATION_PHASE_RESET_COMMAND;
	operation->reset_dispatched = TRUE;
	operation->command_pending = TRUE;
	l850_mutation_replace_cancellable(operation);
	mm_modem_command(operation->original->modem,
		l850gl_l850_reset_command(), L850_COMMAND_TIMEOUT_SECONDS,
		operation->cancellable, l850_mutation_reset_ready, operation);
}

static void
l850_mutation_set_ready(GObject *source, GAsyncResult *result,
			gpointer user_data)
{
	L850MutationOperation *operation = user_data;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *response = NULL;
	L850NormalizedError normalized;

	response = mm_modem_command_finish(MM_MODEM(source), result, &error);
	operation->command_pending = FALSE;
	if (l850_mutation_error_is_uncertain(operation, error) ||
	    !operation->original->live ||
	    source != G_OBJECT(operation->original->modem) ||
	    operation->original->generation != operation->original_generation) {
		l850_mutation_complete_failure(operation, "outcome_unknown",
			"outcome_unknown", FALSE);
		return;
	}
	if (error != NULL || response == NULL) {
		normalized = l850_normalize_error(error, FALSE, FALSE);
		l850_mutation_complete_failure(operation, normalized.code,
			g_str_equal(normalized.code, "unavailable") ?
			"operation_failed" : normalized.code,
			normalized.retryable);
		return;
	}
	if (!l850gl_l850_set_response_is_success(response, strlen(response))) {
		l850_mutation_complete_failure(operation, "malformed_response",
			"malformed_response", FALSE);
		return;
	}
	operation->configuration_acknowledged = TRUE;
	l850_mutation_start_reset(operation);
}

static gboolean
l850_modem_is_registered(L850GLModem *modem)
{
	switch (mm_modem_get_state(modem->modem)) {
	case MM_MODEM_STATE_REGISTERED:
	case MM_MODEM_STATE_DISCONNECTING:
	case MM_MODEM_STATE_CONNECTING:
	case MM_MODEM_STATE_CONNECTED:
		return TRUE;
	default:
		return FALSE;
	}
}

static void l850_mutation_nvm_ready(GObject *source, GAsyncResult *result,
				    gpointer user_data);

static void
l850_mutation_start_nvm_verification(L850MutationOperation *operation)
{
	operation->phase = L850_MUTATION_PHASE_NVM_VERIFY;
	operation->command_pending = TRUE;
	l850_mutation_replace_cancellable(operation);
	mm_modem_command(operation->replacement->modem,
		l850gl_l850_nvm_query_command(), L850_COMMAND_TIMEOUT_SECONDS,
		operation->cancellable, l850_mutation_nvm_ready, operation);
}

static L850GLModem *
l850_mutation_find_replacement(L850MutationOperation *operation,
			       gboolean *ambiguous)
{
	g_autoptr(GPtrArray) modems =
		l850gl_bridge_snapshot_modems(operation->ubus->bridge);
	L850GLModem *match = NULL;
	guint index;

	*ambiguous = FALSE;
	for (index = 0U; modems != NULL && index < modems->len; index++) {
		L850GLModem *candidate = g_ptr_array_index(modems, index);
		const gchar *physdev;

		if (candidate == operation->original ||
		    candidate->generation == operation->original_generation)
			continue;
		physdev = mm_modem_get_physdev(candidate->modem);
		if (physdev == NULL || !g_str_equal(physdev, operation->physdev) ||
		    !l850gl_modem_attest_mutation_target(candidate) ||
		    !l850_firmware_allowed(candidate))
			continue;
		if (match != NULL) {
			*ambiguous = TRUE;
			return NULL;
		}
		match = candidate;
	}
	return match != NULL ? l850gl_modem_ref(match) : NULL;
}

static gboolean
l850_mutation_poll(gpointer user_data)
{
	L850MutationOperation *operation = user_data;
	gint64 now = g_get_monotonic_time();

	if (operation->timed_out || operation->transport_lost ||
	    operation->ubus->stopping) {
		operation->poll_source = 0U;
		l850_mutation_complete_failure(operation, "outcome_unknown",
			"outcome_unknown", FALSE);
		return G_SOURCE_REMOVE;
	}
	if (operation->phase == L850_MUTATION_PHASE_REPROBE) {
		gboolean ambiguous;
		L850GLModem *replacement =
			l850_mutation_find_replacement(operation, &ambiguous);

		if (ambiguous) {
			operation->poll_source = 0U;
			l850_mutation_complete_failure(operation,
				"verification_mismatch", "verification_mismatch",
				FALSE);
			return G_SOURCE_REMOVE;
		}
		if (replacement != NULL) {
			if (replacement->mutation_busy) {
				l850gl_modem_unref(replacement);
				operation->poll_source = 0U;
				l850_mutation_complete_failure(operation,
					"outcome_unknown", "busy", FALSE);
				return G_SOURCE_REMOVE;
			}
			operation->replacement = replacement;
			operation->replacement_generation =
				replacement->generation;
			replacement->mutation_busy = TRUE;
			replacement->mutation_kind = L850GL_MUTATION_L850;
			replacement->mutation_cancellable = g_cancellable_new();
			operation->phase = L850_MUTATION_PHASE_REGISTRATION;
			operation->phase_deadline = now +
				((gint64)L850_REGISTRATION_TIMEOUT_SECONDS *
				 G_USEC_PER_SEC);
			operation->poll_source = 0U;
			if (l850_modem_is_registered(replacement))
				l850_mutation_start_nvm_verification(operation);
			else
				operation->poll_source = g_timeout_add(
					L850_REGISTRATION_POLL_MS,
					l850_mutation_poll, operation);
			return G_SOURCE_REMOVE;
		}
		if (now >= operation->phase_deadline) {
			operation->poll_source = 0U;
			l850_mutation_complete_failure(operation,
				"reprobe_timeout", "reprobe_timeout", FALSE);
			return G_SOURCE_REMOVE;
		}
		return G_SOURCE_CONTINUE;
	}
	if (operation->phase == L850_MUTATION_PHASE_REGISTRATION) {
		if (!l850_mutation_replacement_is_valid(operation,
			G_OBJECT(operation->replacement->modem))) {
			operation->poll_source = 0U;
			l850_mutation_complete_failure(operation,
				"outcome_unknown", "outcome_unknown", FALSE);
			return G_SOURCE_REMOVE;
		}
		if (l850_modem_is_registered(operation->replacement)) {
			operation->poll_source = 0U;
			l850_mutation_start_nvm_verification(operation);
			return G_SOURCE_REMOVE;
		}
		if (now >= operation->phase_deadline) {
			operation->poll_source = 0U;
			l850_mutation_complete_failure(operation,
				"registration_timeout", "registration_timeout",
				FALSE);
			return G_SOURCE_REMOVE;
		}
		return G_SOURCE_CONTINUE;
	}
	operation->poll_source = 0U;
	l850_mutation_complete_failure(operation, "outcome_unknown",
		"outcome_unknown", FALSE);
	return G_SOURCE_REMOVE;
}

static void
l850_mutation_cell_ready(GObject *source, GAsyncResult *result,
			 gpointer user_data)
{
	L850MutationOperation *operation = user_data;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *response = NULL;
	struct L850GLL850CellScan scan;
	const struct L850GLL850Cell *serving = NULL;
	enum L850GLL850CellParseResult parse_result;
	size_t index;

	response = mm_modem_command_finish(MM_MODEM(source), result, &error);
	operation->command_pending = FALSE;
	if (!l850_mutation_replacement_is_valid(operation, source) ||
	    l850_mutation_error_is_uncertain(operation, error) ||
	    error != NULL || response == NULL) {
		l850_mutation_complete_failure(operation, "outcome_unknown",
			"outcome_unknown", FALSE);
		return;
	}
	parse_result = l850gl_l850_cell_parse(response, strlen(response), &scan);
	if (parse_result != L850GL_L850_CELL_PARSE_OK) {
		l850_mutation_complete_failure(operation,
			"verification_mismatch", "verification_mismatch", FALSE);
		return;
	}
	for (index = 0U; index < scan.length; index++) {
		if (!l850_band_is_supported(operation->replacement,
			scan.cells[index].band)) {
			l850_mutation_complete_failure(operation,
				"verification_mismatch", "verification_mismatch",
				FALSE);
			return;
		}
		if (scan.cells[index].serving)
			serving = &scan.cells[index];
	}
	if (serving == NULL || serving->earfcn != operation->earfcn ||
	    (operation->has_pci && serving->pci != operation->pci)) {
		l850_mutation_complete_failure(operation,
			"verification_mismatch", "verification_mismatch", FALSE);
		return;
	}
	if (!serving_cell_cache_store(operation->replacement,
		serving->earfcn, serving->pci, serving->band,
		(gdouble)serving->rsrp_dbm, TRUE,
		(gdouble)serving->rsrq_tenths_db / 10.0, TRUE,
		"expert-postcondition")) {
		l850_mutation_complete_failure(operation,
			"verification_mismatch", "verification_mismatch", FALSE);
		return;
	}
	l850_mutation_complete_success(operation, serving);
}

static void
l850_mutation_nvm_ready(GObject *source, GAsyncResult *result,
			gpointer user_data)
{
	L850MutationOperation *operation = user_data;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *response = NULL;
	struct L850GLL850LockState lock_state;
	enum L850GLL850CellParseResult parse_result;

	response = mm_modem_command_finish(MM_MODEM(source), result, &error);
	operation->command_pending = FALSE;
	if (!l850_mutation_replacement_is_valid(operation, source) ||
	    l850_mutation_error_is_uncertain(operation, error) ||
	    error != NULL || response == NULL) {
		l850_mutation_complete_failure(operation, "outcome_unknown",
			"outcome_unknown", FALSE);
		return;
	}
	parse_result = l850gl_l850_nvm_parse(response, strlen(response),
		&lock_state);
	if (parse_result != L850GL_L850_CELL_PARSE_OK ||
	    !l850gl_l850_lock_state_matches(&lock_state,
		operation->type == L850_MUTATION_CLEAR, operation->earfcn,
		operation->has_pci, operation->pci)) {
		l850_mutation_complete_failure(operation,
			"verification_mismatch", "verification_mismatch", FALSE);
		return;
	}
	if (operation->type == L850_MUTATION_CLEAR) {
		l850_mutation_complete_success(operation, NULL);
		return;
	}
	operation->phase = L850_MUTATION_PHASE_CELL_VERIFY;
	operation->command_pending = TRUE;
	l850_mutation_replace_cancellable(operation);
	mm_modem_command(operation->replacement->modem,
		l850gl_l850_scan_command(), L850_COMMAND_TIMEOUT_SECONDS,
		operation->cancellable, l850_mutation_cell_ready, operation);
}

static gboolean
l850_mutation_timeout(gpointer user_data)
{
	L850MutationOperation *operation = user_data;

	operation->timeout_source = 0U;
	operation->timed_out = TRUE;
	g_cancellable_cancel(operation->cancellable);
	return G_SOURCE_REMOVE;
}

static L850MutationOperation *
l850_mutation_operation_new(L850GLUbus *ubus, L850GLModem *modem,
			    L850MutationType type, guint32 earfcn,
			    gboolean has_pci, guint16 pci, guint16 band,
			    const gchar *set_command,
			    struct ubus_context *context,
			    struct ubus_request_data *request)
{
	L850MutationOperation *operation = g_new0(L850MutationOperation, 1);
	const gchar *physdev = mm_modem_get_physdev(modem->modem);

	if (physdev == NULL || physdev[0] == '\0') {
		g_free(operation);
		return NULL;
	}
	operation->type = type;
	operation->phase = L850_MUTATION_PHASE_SET_COMMAND;
	operation->ubus = l850gl_ubus_ref_internal(ubus);
	operation->original = l850gl_modem_ref(modem);
	operation->cancellable = g_cancellable_new();
	operation->original_generation = modem->generation;
	operation->physdev = g_strdup(physdev);
	operation->earfcn = earfcn;
	operation->has_pci = has_pci;
	operation->pci = pci;
	operation->band = band;
	if (set_command != NULL)
		g_strlcpy(operation->set_command, set_command,
			sizeof(operation->set_command));
	ubus_defer_request(context, request, &operation->request);
	operation->deferred = TRUE;
	g_hash_table_add(ubus->l850_mutation_operations, operation);
	operation->timeout_source = g_timeout_add_seconds(
		L850_MUTATION_OPERATION_TIMEOUT_SECONDS, l850_mutation_timeout,
		operation);
	modem->mutation_busy = TRUE;
	modem->mutation_kind = L850GL_MUTATION_L850;
	modem->mutation_cancellable = g_cancellable_new();
	l850_mutation_apply_cooldown(modem);
	return operation;
}

static int
l850_mutation_start(L850GLUbus *ubus, L850GLModem *modem,
		    L850MutationType type, guint32 earfcn, gboolean has_pci,
		    guint16 pci, guint16 band, const gchar *set_command,
		    struct ubus_context *context,
		    struct ubus_request_data *request)
{
	L850MutationOperation *operation = l850_mutation_operation_new(ubus,
		modem, type, earfcn, has_pci, pci, band, set_command, context,
		request);
	const gchar *command;

	if (operation == NULL)
		return send_l850_error(context, request, modem, "unsupported",
			FALSE, 0U);
	command = type == L850_MUTATION_CLEAR ?
		l850gl_l850_clear_command() : operation->set_command;
	operation->set_dispatched = TRUE;
	operation->command_pending = TRUE;
	mm_modem_command(operation->original->modem, command,
		L850_COMMAND_TIMEOUT_SECONDS, operation->cancellable,
		l850_mutation_set_ready, operation);
	return UBUS_STATUS_OK;
}

static int
method_set_cell_lock(struct ubus_context *context, struct ubus_object *object,
		     struct ubus_request_data *request, const char *method,
		     struct blob_attr *message)
{
	L850GLUbus *ubus = from_l850_object(object);
	struct blob_attr *parsed[__L850_SET_MAX] = {};
	const guint64 required =
		(G_GUINT64_CONSTANT(1) << L850_SET_MODEM_ID) |
		(G_GUINT64_CONSTANT(1) << L850_SET_GENERATION) |
		(G_GUINT64_CONSTANT(1) << L850_SET_EARFCN) |
		(G_GUINT64_CONSTANT(1) << L850_SET_CONFIRM);
	const gchar *error_code;
	L850GLModem *modem;
	guint32 earfcn;
	guint32 pci = 0U;
	guint32 retry_after;
	guint16 band;
	gboolean has_pci;
	char command[L850GL_L850_COMMAND_MAX];

	(void)method;
	if (!parse_exact_fields(message, l850_set_policy, __L850_SET_MAX,
		required, parsed) || !blobmsg_get_bool(parsed[L850_SET_CONFIRM]))
		return send_l850_error(context, request, NULL,
			"invalid_argument", FALSE, 0U);
	earfcn = blobmsg_get_u32(parsed[L850_SET_EARFCN]);
	has_pci = parsed[L850_SET_PCI] != NULL;
	if (parsed[L850_SET_PCI] != NULL)
		pci = blobmsg_get_u32(parsed[L850_SET_PCI]);
	if (!l850gl_l850_earfcn_to_band(earfcn, &band) ||
	    (parsed[L850_SET_PCI] != NULL && pci > 503U))
		return send_l850_error(context, request, NULL,
			"invalid_argument", FALSE, 0U);
	modem = l850_requested_modem(ubus, parsed, L850_SET_MODEM_ID,
		L850_SET_GENERATION, &error_code);
	if (modem == NULL)
		return send_l850_error(context, request, NULL, error_code,
			g_str_equal(error_code, "dependency_unavailable") ||
			g_str_equal(error_code, "busy"), 0U);
	if (!l850_band_is_supported(modem, band))
		return send_l850_error(context, request, modem,
			"not_ready", TRUE, 0U);
	if (!l850_firmware_allowed(modem))
		return send_l850_error(context, request, modem,
			"unsupported_firmware", FALSE, 0U);
	retry_after = advanced_retry_after_ms(ubus, modem);
	if (modem->mutation_busy || modem->l850_voltage_refresh_pending ||
	    retry_after > 0U)
		return send_l850_error(context, request, modem, "busy", TRUE,
			retry_after);
	if (!l850gl_l850_build_set_command(earfcn, has_pci, (guint16)pci,
		command, sizeof(command)))
		return send_l850_error(context, request, modem,
			"invalid_argument", FALSE, 0U);
	return l850_mutation_start(ubus, modem, L850_MUTATION_SET, earfcn,
		has_pci, (guint16)pci, band, command, context, request);
}

static int
method_clear_cell_lock(struct ubus_context *context,
		       struct ubus_object *object,
		       struct ubus_request_data *request, const char *method,
		       struct blob_attr *message)
{
	L850GLUbus *ubus = from_l850_object(object);
	struct blob_attr *parsed[__L850_CLEAR_MAX] = {};
	const gchar *error_code;
	L850GLModem *modem;
	guint32 retry_after;

	(void)method;
	if (!parse_exact_fields(message, l850_clear_policy, __L850_CLEAR_MAX,
		(G_GUINT64_CONSTANT(1) << __L850_CLEAR_MAX) - 1U, parsed) ||
	    !blobmsg_get_bool(parsed[L850_CLEAR_CONFIRM]))
		return send_l850_error(context, request, NULL,
			"invalid_argument", FALSE, 0U);
	modem = l850_requested_modem(ubus, parsed, L850_CLEAR_MODEM_ID,
		L850_CLEAR_GENERATION, &error_code);
	if (modem == NULL)
		return send_l850_error(context, request, NULL, error_code,
			g_str_equal(error_code, "dependency_unavailable") ||
			g_str_equal(error_code, "busy"), 0U);
	if (!l850_firmware_allowed(modem))
		return send_l850_error(context, request, modem,
			"unsupported_firmware", FALSE, 0U);
	if (!l850_supported_lte_bands_available(modem))
		return send_l850_error(context, request, modem, "not_ready", TRUE,
			0U);
	retry_after = advanced_retry_after_ms(ubus, modem);
	if (modem->mutation_busy || modem->l850_voltage_refresh_pending ||
	    retry_after > 0U)
		return send_l850_error(context, request, modem, "busy", TRUE,
			retry_after);
	return l850_mutation_start(ubus, modem, L850_MUTATION_CLEAR,
		L850GL_L850_CLEAR_FREQUENCY, FALSE,
		L850GL_L850_PCI_WILDCARD, 0U, NULL, context, request);
}
#endif

static gboolean reconnect_cb(gpointer user_data);

static void
schedule_reconnect(L850GLUbus *ubus)
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
	L850GLUbus *ubus = (L850GLUbus *)((gchar *)context -
		G_STRUCT_OFFSET(L850GLUbus, context));
	guint old_source;

	if (ubus->stopping)
		return;
	ubus->connected = FALSE;
	cancel_sms_operations(ubus, TRUE);
	cancel_advanced_operations(ubus, TRUE);
#ifdef L850GL_MM_EXPERT
	cancel_l850_scan_operations(ubus, TRUE);
	cancel_l850_status_operations(ubus, TRUE);
	cancel_l850_carrier_operations(ubus, TRUE);
	cancel_l850_mutation_operations(ubus, TRUE);
#endif
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
	L850GLUbus *ubus = user_data;

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
install_fd_source(L850GLUbus *ubus)
{
	if (ubus->fd_source != 0)
		g_source_remove(ubus->fd_source);
	ubus->fd_source = g_unix_fd_add_full(G_PRIORITY_DEFAULT,
		ubus->context.sock.fd, G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
		ubus_fd_ready, ubus, NULL);
}

static gboolean
connect_ubus(L850GLUbus *ubus)
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
		l850gl_object_type.id = 0;
		status = ubus_add_object(&ubus->context, &ubus->object);
		if (status != 0) {
			g_warning("cannot publish l850gl.mm ubus object");
			ubus_shutdown(&ubus->context);
			ubus->context_initialized = FALSE;
			return FALSE;
		}
#ifdef L850GL_MM_EXPERT
		ubus->l850_object.id = 0;
		memset(&ubus->l850_object.avl, 0,
			sizeof(ubus->l850_object.avl));
		l850_object_type.id = 0;
		status = ubus_add_object(&ubus->context, &ubus->l850_object);
		if (status != 0) {
			g_warning("cannot publish build-gated l850gl.mm.l850 object");
			ubus_shutdown(&ubus->context);
			ubus->context_initialized = FALSE;
			return FALSE;
		}
#endif
	} else {
		status = ubus_reconnect(&ubus->context, ubus->socket_path);
		if (status != 0)
			return FALSE;
		if (ubus->object.id == 0
#ifdef L850GL_MM_EXPERT
		    || ubus->l850_object.id == 0
#endif
		   ) {
			ubus_shutdown(&ubus->context);
			ubus->context_initialized = FALSE;
			l850gl_object_type.id = 0;
#ifdef L850GL_MM_EXPERT
			l850_object_type.id = 0;
#endif
			return FALSE;
		}
	}
	ubus->connected = TRUE;
	ubus->reconnect_delay_ms = RECONNECT_INITIAL_MS;
	install_fd_source(ubus);
	g_message("companion API published as l850gl.mm");
#ifdef L850GL_MM_EXPERT
	g_message("expert API published as l850gl.mm.l850 (one live-validated firmware)");
#endif
	return TRUE;
}

static gboolean
reconnect_cb(gpointer user_data)
{
	L850GLUbus *ubus = user_data;

	ubus->reconnect_source = 0;
	if (ubus->stopping)
		return G_SOURCE_REMOVE;
	if (!connect_ubus(ubus))
		schedule_reconnect(ubus);
	return G_SOURCE_REMOVE;
}

static L850GLUbus *
l850gl_ubus_ref_internal(L850GLUbus *ubus)
{
	g_return_val_if_fail(ubus != NULL, NULL);
	g_atomic_int_inc(&ubus->refs);
	return ubus;
}

static void
l850gl_ubus_unref_internal(L850GLUbus *ubus)
{
	if (ubus == NULL || !g_atomic_int_dec_and_test(&ubus->refs))
		return;
	g_clear_pointer(&ubus->sms_operations, g_hash_table_unref);
	g_clear_pointer(&ubus->advanced_operations, g_hash_table_unref);
#ifdef L850GL_MM_EXPERT
	g_clear_pointer(&ubus->l850_scan_operations, g_hash_table_unref);
	g_clear_pointer(&ubus->l850_status_operations, g_hash_table_unref);
	g_clear_pointer(&ubus->l850_carrier_operations, g_hash_table_unref);
	g_clear_pointer(&ubus->l850_mutation_operations, g_hash_table_unref);
#endif
	g_free(ubus->socket_path);
	g_free(ubus);
}

L850GLUbus *
l850gl_ubus_new(L850GLBridge *bridge, const gchar *socket_path)
{
	L850GLUbus *ubus;

	g_return_val_if_fail(bridge != NULL, NULL);
	ubus = g_new0(L850GLUbus, 1);
	ubus->refs = 1;
	ubus->bridge = bridge;
	ubus->socket_path = g_strdup(socket_path);
	ubus->sms_operations = g_hash_table_new(g_direct_hash, g_direct_equal);
	ubus->advanced_operations = g_hash_table_new(g_direct_hash,
		g_direct_equal);
#ifdef L850GL_MM_EXPERT
	ubus->l850_scan_operations = g_hash_table_new(g_direct_hash,
		g_direct_equal);
	ubus->l850_status_operations = g_hash_table_new(g_direct_hash,
		g_direct_equal);
	ubus->l850_carrier_operations = g_hash_table_new(g_direct_hash,
		g_direct_equal);
	ubus->l850_mutation_operations = g_hash_table_new(g_direct_hash,
		g_direct_equal);
#endif
	ubus->reconnect_delay_ms = RECONNECT_INITIAL_MS;
	ubus->object.name = "l850gl.mm";
	ubus->object.type = &l850gl_object_type;
	ubus->object.methods = l850gl_methods;
	ubus->object.n_methods = G_N_ELEMENTS(l850gl_methods);
#ifdef L850GL_MM_EXPERT
	ubus->l850_object.name = "l850gl.mm.l850";
	ubus->l850_object.type = &l850_object_type;
	ubus->l850_object.methods = l850_methods;
	ubus->l850_object.n_methods = G_N_ELEMENTS(l850_methods);
#endif
	return ubus;
}

void
l850gl_ubus_start(L850GLUbus *ubus)
{
	if (ubus->stopping || ubus->connected || ubus->reconnect_source != 0)
		return;
	if (!connect_ubus(ubus)) {
		g_warning("ubus unavailable; ModemManager observation continues");
		schedule_reconnect(ubus);
	}
}

void
l850gl_ubus_stop(L850GLUbus *ubus)
{
	if (ubus == NULL || ubus->stopping)
		return;
	ubus->stopping = TRUE;
	ubus->connected = FALSE;
	cancel_sms_operations(ubus, TRUE);
	cancel_advanced_operations(ubus, TRUE);
#ifdef L850GL_MM_EXPERT
	cancel_l850_scan_operations(ubus, TRUE);
	cancel_l850_status_operations(ubus, TRUE);
	cancel_l850_carrier_operations(ubus, TRUE);
	cancel_l850_mutation_operations(ubus, TRUE);
#endif
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
l850gl_ubus_free(L850GLUbus *ubus)
{
	if (ubus == NULL)
		return;
	l850gl_ubus_stop(ubus);
	l850gl_ubus_unref_internal(ubus);
}
