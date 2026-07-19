/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ubus_glib.h"

#include "identity.h"

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

struct _FibocomUbus {
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
};

enum {
	MODEM_ID,
	__MODEM_MAX,
};

static const struct blobmsg_policy modem_policy[__MODEM_MAX] = {
	[MODEM_ID] = { .name = "modem_id", .type = BLOBMSG_TYPE_STRING },
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

static const struct ubus_method fibocom_methods[] = {
	UBUS_METHOD_NOARG("list_modems", method_list_modems),
	UBUS_METHOD("get_overview", method_get_overview, modem_policy),
	UBUS_METHOD("get_status", method_get_status, modem_policy),
	UBUS_METHOD("get_capabilities", method_get_capabilities, modem_policy),
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
	blobmsg_add_string(buffer, "reason", "netifd-correlation-not-implemented-p0");
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
	blobmsg_add_string(&buffer, NULL, "openwrt-state-not-probed-p0");
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
		while (paths[count] != NULL && count < 16U)
			count++;
	}
	return count;
}

static void
add_sim_status(struct blob_buf *buffer, FibocomModem *modem)
{
	const gchar *sim_path = mm_modem_get_sim_path(modem->modem);
	void *sim = blobmsg_open_table(buffer, "sim");

	blobmsg_add_u8(buffer, "present",
		sim_path != NULL && !g_str_equal(sim_path, "/"));
	blobmsg_add_string(buffer, "cache_state", modem->sim_cache_state);
	blobmsg_add_u32(buffer, "slots", sim_slot_count(modem));
	blobmsg_add_u32(buffer, "primary_slot",
		mm_modem_get_primary_sim_slot(modem->modem));
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
	blobmsg_close_table(buffer, sim);
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
	add_signal_status(&buffer, modem);
	cell = blobmsg_open_table(&buffer, "cell");
	blobmsg_add_string(&buffer, "state", "unknown");
	blobmsg_add_string(&buffer, "reason", "get-cell-info-not-polled-p0");
	blobmsg_close_table(&buffer, cell);
	add_bearers_status(&buffer, modem);
	add_openwrt_unavailable(&buffer);
	diagnostics = blobmsg_open_table(&buffer, "diagnostics");
	blobmsg_add_string(&buffer, "bridge_version", FIBOCOM_MM_BRIDGE_VERSION);
	add_safe_string(&buffer, "modemmanager_version",
		fibocom_bridge_manager_version(ubus->bridge), 32U);
	blobmsg_add_u8(&buffer, "read_only", TRUE);
	blobmsg_add_u8(&buffer, "raw_dbus_paths_exposed", FALSE);
	blobmsg_add_u8(&buffer, "at_commands_enabled", FALSE);
	blobmsg_close_table(&buffer, diagnostics);
	return send_buffer(context, request, &buffer);
}

static void
add_feature(struct blob_buf *buffer, const gchar *name, const gchar *state,
	    const gchar *reason)
{
	void *feature = blobmsg_open_table(buffer, name);

	blobmsg_add_string(buffer, "state", state);
	blobmsg_add_u8(buffer, "mutable", FALSE);
	blobmsg_add_string(buffer, "reason", reason);
	blobmsg_close_table(buffer, feature);
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
	const MMModemBand *bands = NULL;
	const MMModemModeCombination *modes = NULL;
	guint n_bands = 0;
	guint n_modes = 0;
	guint slots;
	struct blob_buf buffer = {};
	void *capabilities;

	(void)method;
	modem = requested_modem(ubus, message, parsed, &error_code);
	if (modem == NULL)
		return send_requested_error(context, request, error_code);
	messaging = mm_object_peek_modem_messaging(modem->object);
	(void)mm_modem_peek_supported_bands(modem->modem, &bands, &n_bands);
	(void)mm_modem_peek_supported_modes(modem->modem, &modes, &n_modes);
	slots = sim_slot_count(modem);
	blob_buf_init(&buffer, 0);
	add_common(&buffer, TRUE);
	add_modem_identity(&buffer, modem);
	capabilities = blobmsg_open_table(&buffer, "capabilities");
	add_feature(&buffer, "mbim_data",
		g_str_equal(fibocom_modem_composition(modem), "mbim") ?
		"available" : "unavailable",
		"observed-modemmanager-composition");
	add_feature(&buffer, "ncm_data", "unsupported",
		"missing-l850-modemmanager-ncm-backend");
	add_feature(&buffer, "messaging", messaging != NULL ? "available" :
		"unavailable", messaging != NULL ? "dbus-interface-present-read-only-p0" :
		"dbus-interface-absent");
	add_feature(&buffer, "bands", n_bands > 0 ? "available" : "unknown",
		n_bands > 0 ? "supported-bands-present-read-only-p0" :
		"supported-bands-not-advertised");
	add_feature(&buffer, "modes", n_modes > 0 ? "available" : "unknown",
		n_modes > 0 ? "network-uci-owned-read-only-p0" :
		"supported-modes-not-advertised");
	add_feature(&buffer, "reset", "available", "standard-method-read-only-p0");
	add_feature(&buffer, "sim_slot", slots > 1U ? "available" : "unsupported",
		slots > 1U ? "multiple-slots-advertised-read-only-p0" : "single-slot");
	add_feature(&buffer, "cell_info", "unknown", "not-probed-read-only-p0");
	add_feature(&buffer, "l850_cell_extension", "unavailable",
		"expert-build-disabled");
	blobmsg_close_table(&buffer, capabilities);
	return send_buffer(context, request, &buffer);
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
	g_message("read-only API published as fibocom.mm");
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

FibocomUbus *
fibocom_ubus_new(FibocomBridge *bridge, const gchar *socket_path)
{
	FibocomUbus *ubus;

	g_return_val_if_fail(bridge != NULL, NULL);
	ubus = g_new0(FibocomUbus, 1);
	ubus->bridge = bridge;
	ubus->socket_path = g_strdup(socket_path);
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
	g_free(ubus->socket_path);
	g_free(ubus);
}
