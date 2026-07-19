/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ubus_glib.h"

#include <glib-unix.h>
#include <libubox/blobmsg.h>
#include <libubus.h>
#include <string.h>

#define RECONNECT_INITIAL_MS 250U
#define RECONNECT_MAX_MS 30000U
#define HINT_MAX 64U
#define DEVICE_ID_MAX 96U

struct _FibocomUbus {
	FibocomDiscovery *discovery;
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
	DEVICE_DEVICE_ID,
	__DEVICE_MAX,
};

enum {
	RESCAN_REASON,
	RESCAN_SUBSYSTEM,
	RESCAN_ACTION,
	__RESCAN_MAX,
};

static const struct blobmsg_policy device_policy[__DEVICE_MAX] = {
	[DEVICE_DEVICE_ID] = { .name = "device_id", .type = BLOBMSG_TYPE_STRING },
};

static const struct blobmsg_policy rescan_policy[__RESCAN_MAX] = {
	[RESCAN_REASON] = { .name = "reason", .type = BLOBMSG_TYPE_STRING },
	[RESCAN_SUBSYSTEM] = { .name = "subsystem", .type = BLOBMSG_TYPE_STRING },
	[RESCAN_ACTION] = { .name = "action", .type = BLOBMSG_TYPE_STRING },
};

static int method_list(struct ubus_context *context, struct ubus_object *object,
		       struct ubus_request_data *request, const char *method,
		       struct blob_attr *message);
static int method_status(struct ubus_context *context, struct ubus_object *object,
			 struct ubus_request_data *request, const char *method,
			 struct blob_attr *message);
static int method_capabilities(struct ubus_context *context,
			       struct ubus_object *object,
			       struct ubus_request_data *request,
			       const char *method, struct blob_attr *message);
static int method_diagnostics(struct ubus_context *context,
			      struct ubus_object *object,
			      struct ubus_request_data *request,
			      const char *method, struct blob_attr *message);
static int method_rescan(struct ubus_context *context, struct ubus_object *object,
			 struct ubus_request_data *request, const char *method,
			 struct blob_attr *message);

static const struct ubus_method fibocom_methods[] = {
	UBUS_METHOD_NOARG("list", method_list),
	UBUS_METHOD("status", method_status, device_policy),
	UBUS_METHOD("capabilities", method_capabilities, device_policy),
	UBUS_METHOD("diagnostics", method_diagnostics, device_policy),
	UBUS_METHOD("rescan", method_rescan, rescan_policy),
};

static struct ubus_object_type fibocom_object_type =
	UBUS_OBJECT_TYPE("fibocom", fibocom_methods);

static FibocomUbus *
from_object(struct ubus_object *object)
{
	return (FibocomUbus *)((gchar *)object - G_STRUCT_OFFSET(FibocomUbus, object));
}

static void
add_common(struct blob_buf *buffer)
{
	blobmsg_add_u32(buffer, "schema", FIBOCOM_API_SCHEMA);
	blobmsg_add_u8(buffer, "shadow_mode", TRUE);
}

static const FibocomPort *
first_tty_for_role(const FibocomDevice *device, const gchar *role)
{
	guint i;

	for (i = 0; i < device->interfaces->len; i++) {
		const FibocomInterface *interface = g_ptr_array_index(device->interfaces, i);

		if (g_str_equal(interface->role, role) && interface->ttys->len > 0)
			return g_ptr_array_index(interface->ttys, 0);
	}
	return NULL;
}

static const FibocomPort *
first_port(const FibocomDevice *device, gboolean wdm)
{
	guint i;

	for (i = 0; i < device->interfaces->len; i++) {
		const FibocomInterface *interface = g_ptr_array_index(device->interfaces, i);
		GPtrArray *ports = wdm ? interface->wdms : interface->netdevs;

		if (ports->len > 0)
			return g_ptr_array_index(ports, 0);
	}
	return NULL;
}

static void
add_ports(struct blob_buf *buffer, const FibocomDevice *device)
{
	const FibocomPort *primary = first_tty_for_role(device, "at-primary");
	const FibocomPort *secondary = first_tty_for_role(device, "at-secondary");
	const FibocomPort *wdm = first_port(device, TRUE);
	void *ports = blobmsg_open_table(buffer, "ports");
	void *ignored;
	void *netdevs;
	guint i;

	blobmsg_add_string(buffer, "at_primary", primary != NULL ? primary->name : "");
	blobmsg_add_string(buffer, "at_secondary", secondary != NULL ? secondary->name : "");
	ignored = blobmsg_open_array(buffer, "ignored");
	for (i = 0; i < device->interfaces->len; i++) {
		const FibocomInterface *interface = g_ptr_array_index(device->interfaces, i);
		guint p;

		if (!g_str_equal(interface->role, "ignored"))
			continue;
		for (p = 0; p < interface->ttys->len; p++) {
			const FibocomPort *port = g_ptr_array_index(interface->ttys, p);

			blobmsg_add_string(buffer, NULL, port->name);
		}
	}
	blobmsg_close_array(buffer, ignored);
	blobmsg_add_string(buffer, "wdm", wdm != NULL ? wdm->name : "");
	netdevs = blobmsg_open_array(buffer, "netdevs");
	for (i = 0; i < device->interfaces->len; i++) {
		const FibocomInterface *interface = g_ptr_array_index(device->interfaces, i);
		guint p;

		for (p = 0; p < interface->netdevs->len; p++) {
			const FibocomPort *port = g_ptr_array_index(interface->netdevs, p);

			blobmsg_add_string(buffer, NULL, port->name);
		}
	}
	blobmsg_close_array(buffer, netdevs);
	blobmsg_close_table(buffer, ports);
}

static void
add_device_summary(struct blob_buf *buffer, const FibocomDevice *device)
{
	blobmsg_add_string(buffer, "device_id", device->device_id);
	blobmsg_add_u64(buffer, "generation", device->generation);
	blobmsg_add_u8(buffer, "present", device->present);
	blobmsg_add_string(buffer, "profile", FIBOCOM_PROFILE_ID);
	blobmsg_add_string(buffer, "model", "Fibocom L850-GL");
	blobmsg_add_string(buffer, "model_confidence", "usb_id_only");
	blobmsg_add_string(buffer, "identity_scope", device->identity_scope);
	blobmsg_add_string(buffer, "composition", device->composition);
	blobmsg_add_string(buffer, "vid", device->vid);
	blobmsg_add_string(buffer, "pid", device->pid);
	blobmsg_add_string(buffer, "physical_path", device->physical_path);
	blobmsg_add_string(buffer, "topology_status",
			   fibocom_topology_status_name(device->topology_status));
	blobmsg_add_string(buffer, "topology_reason", device->topology_reason);
	add_ports(buffer, device);
}

static void
add_reconcile(struct blob_buf *buffer, const FibocomDiscovery *discovery)
{
	const FibocomReconcile *reconcile = fibocom_discovery_reconcile(discovery);
	void *scan = blobmsg_open_table(buffer, "reconcile");

	blobmsg_add_u64(buffer, "scan_id", reconcile->scan_id);
	blobmsg_add_u64(buffer, "completed_at", reconcile->completed_at > 0 ?
			reconcile->completed_at : 0);
	blobmsg_add_u32(buffer, "device_count", reconcile->device_count);
	blobmsg_add_u32(buffer, "added", reconcile->added);
	blobmsg_add_u32(buffer, "removed", reconcile->removed);
	blobmsg_add_u32(buffer, "changed", reconcile->changed);
	blobmsg_add_u32(buffer, "unchanged", reconcile->unchanged);
	blobmsg_add_u8(buffer, "scan_in_progress",
		       fibocom_discovery_scan_in_progress(discovery));
	blobmsg_add_u8(buffer, "scan_pending",
		       fibocom_discovery_scan_pending(discovery));
	blobmsg_add_u8(buffer, "initialized", reconcile->completed_at > 0);
	blobmsg_add_u8(buffer, "ok", reconcile->completed_at > 0 &&
		       reconcile->last_error == NULL);
	blobmsg_add_string(buffer, "error", reconcile->last_error != NULL ?
			   reconcile->last_error : "");
	blobmsg_close_table(buffer, scan);
}

static void
add_port_details(struct blob_buf *buffer, const gchar *name, GPtrArray *ports)
{
	void *array = blobmsg_open_array(buffer, name);
	guint i;

	for (i = 0; i < ports->len; i++) {
		const FibocomPort *port = g_ptr_array_index(ports, i);
		void *entry = blobmsg_open_table(buffer, NULL);

		blobmsg_add_string(buffer, "name", port->name);
		blobmsg_add_string(buffer, "interface_number", port->interface_number);
		blobmsg_add_u32(buffer, "interface_index", port->interface_index);
		blobmsg_add_string(buffer, "driver", port->driver);
		blobmsg_close_table(buffer, entry);
	}
	blobmsg_close_array(buffer, array);
}

static void
add_device_diagnostics(struct blob_buf *buffer, const FibocomDevice *device)
{
	void *entry = blobmsg_open_table(buffer, NULL);
	void *interfaces;
	guint i;

	add_device_summary(buffer, device);
	interfaces = blobmsg_open_array(buffer, "interfaces");
	for (i = 0; i < device->interfaces->len; i++) {
		const FibocomInterface *interface = g_ptr_array_index(device->interfaces, i);
		void *item = blobmsg_open_table(buffer, NULL);

		blobmsg_add_string(buffer, "number", interface->number);
		blobmsg_add_u32(buffer, "index", interface->index);
		blobmsg_add_string(buffer, "driver", interface->driver);
		blobmsg_add_string(buffer, "role", interface->role);
		add_port_details(buffer, "ttys", interface->ttys);
		add_port_details(buffer, "wdms", interface->wdms);
		add_port_details(buffer, "netdevs", interface->netdevs);
		blobmsg_close_table(buffer, item);
	}
	blobmsg_close_array(buffer, interfaces);
	blobmsg_close_table(buffer, entry);
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
	   const gchar *code, const gchar *message)
{
	struct blob_buf buffer = {};
	void *error;

	blob_buf_init(&buffer, 0);
	add_common(&buffer);
	error = blobmsg_open_table(&buffer, "error");
	blobmsg_add_string(&buffer, "code", code);
	blobmsg_add_string(&buffer, "message", message);
	blobmsg_close_table(&buffer, error);
	return send_buffer(context, request, &buffer);
}

static gboolean
valid_device_id(const gchar *device_id)
{
	gsize length;
	gsize i;

	if (device_id == NULL)
		return FALSE;
	length = strlen(device_id);
	if (length < 16 || length > DEVICE_ID_MAX ||
	    !g_str_has_prefix(device_id, "l850-"))
		return FALSE;
	for (i = 0; i < length; i++) {
		if (!g_ascii_islower(device_id[i]) && !g_ascii_isdigit(device_id[i]) &&
		    device_id[i] != '-')
			return FALSE;
	}
	return TRUE;
}

static gboolean
valid_hint(const gchar *value)
{
	gsize length;
	gsize i;

	if (value == NULL)
		return TRUE;
	length = strlen(value);
	if (length == 0 || length > HINT_MAX)
		return FALSE;
	for (i = 0; i < length; i++) {
		if (!g_ascii_isalnum(value[i]) && value[i] != '-' && value[i] != '_' &&
		    value[i] != '.' && value[i] != ':')
			return FALSE;
	}
	return TRUE;
}

static gboolean
value_in_set(const gchar *value, const gchar *const *values)
{
	guint i;

	if (value == NULL)
		return TRUE;
	for (i = 0; values[i] != NULL; i++) {
		if (g_str_equal(value, values[i]))
			return TRUE;
	}
	return FALSE;
}

static gboolean
message_has_only(struct blob_attr *message, const gchar *const *names)
{
	struct blob_attr *attribute;
	unsigned int remaining;

	if (message == NULL)
		return TRUE;
	blobmsg_for_each_attr(attribute, message, remaining) {
		const gchar *name = blobmsg_name(attribute);
		gboolean known = FALSE;
		guint i;

		for (i = 0; names[i] != NULL; i++) {
			if (g_str_equal(name, names[i])) {
				known = TRUE;
				break;
			}
		}
		if (!known)
			return FALSE;
	}
	return TRUE;
}

static const FibocomDevice *
parse_requested_device(FibocomUbus *ubus, struct blob_attr *message,
		       struct blob_attr **parsed)
{
	static const gchar *const names[] = { "device_id", NULL };
	const gchar *device_id;

	if (message == NULL || !message_has_only(message, names))
		return NULL;
	blobmsg_parse(device_policy, __DEVICE_MAX, parsed, blob_data(message),
		      blob_len(message));
	if (parsed[DEVICE_DEVICE_ID] == NULL)
		return NULL;
	device_id = blobmsg_get_string(parsed[DEVICE_DEVICE_ID]);
	if (!valid_device_id(device_id))
		return NULL;
	return fibocom_discovery_find(ubus->discovery, device_id);
}

static int
method_list(struct ubus_context *context, struct ubus_object *object,
	    struct ubus_request_data *request, const char *method,
	    struct blob_attr *message)
{
	static const gchar *const names[] = { NULL };
	FibocomUbus *ubus = from_object(object);
	const GPtrArray *devices = fibocom_discovery_devices(ubus->discovery);
	struct blob_buf buffer = {};
	void *array;
	guint i;

	(void)method;
	if (!message_has_only(message, names))
		return send_error(context, request, "invalid_argument",
				  "list does not accept arguments");
	blob_buf_init(&buffer, 0);
	add_common(&buffer);
	array = blobmsg_open_array(&buffer, "devices");
	for (i = 0; i < devices->len; i++) {
		const FibocomDevice *device = g_ptr_array_index(devices, i);
		void *entry = blobmsg_open_table(&buffer, NULL);

		add_device_summary(&buffer, device);
		blobmsg_close_table(&buffer, entry);
	}
	blobmsg_close_array(&buffer, array);
	add_reconcile(&buffer, ubus->discovery);
	return send_buffer(context, request, &buffer);
}

static int
method_status(struct ubus_context *context, struct ubus_object *object,
	      struct ubus_request_data *request, const char *method,
	      struct blob_attr *message)
{
	FibocomUbus *ubus = from_object(object);
	struct blob_attr *parsed[__DEVICE_MAX] = {};
	const FibocomDevice *device;
	const gchar *requested = NULL;
	struct blob_buf buffer = {};
	void *entry;

	(void)method;
	device = parse_requested_device(ubus, message, parsed);
	if (parsed[DEVICE_DEVICE_ID] != NULL)
		requested = blobmsg_get_string(parsed[DEVICE_DEVICE_ID]);
	if (!valid_device_id(requested))
		return send_error(context, request, "invalid_argument",
				  "a valid device_id is required");
	if (device == NULL)
		return send_error(context, request, "not_found",
				  "device_id is not present in the cached inventory");
	blob_buf_init(&buffer, 0);
	add_common(&buffer);
	blobmsg_add_string(&buffer, "state", "shadow");
	entry = blobmsg_open_table(&buffer, "device");
	add_device_summary(&buffer, device);
	blobmsg_close_table(&buffer, entry);
	add_reconcile(&buffer, ubus->discovery);
	return send_buffer(context, request, &buffer);
}

static void
add_feature(struct blob_buf *buffer, const gchar *name, const gchar *state,
	    const gchar *reason)
{
	void *feature = blobmsg_open_table(buffer, name);

	blobmsg_add_string(buffer, "state", state);
	blobmsg_add_u8(buffer, "available", g_str_equal(state, "available"));
	blobmsg_add_string(buffer, "reason", reason);
	blobmsg_close_table(buffer, feature);
}

static int
method_capabilities(struct ubus_context *context, struct ubus_object *object,
		    struct ubus_request_data *request, const char *method,
		    struct blob_attr *message)
{
	FibocomUbus *ubus = from_object(object);
	struct blob_attr *parsed[__DEVICE_MAX] = {};
	const FibocomDevice *device;
	const gchar *requested = NULL;
	struct blob_buf buffer = {};
	void *features;
	gboolean complete;

	(void)method;
	device = parse_requested_device(ubus, message, parsed);
	if (parsed[DEVICE_DEVICE_ID] != NULL)
		requested = blobmsg_get_string(parsed[DEVICE_DEVICE_ID]);
	if (!valid_device_id(requested))
		return send_error(context, request, "invalid_argument",
				  "a valid device_id is required");
	if (device == NULL)
		return send_error(context, request, "not_found",
				  "device_id is not present in the cached inventory");
	complete = device->topology_status == FIBOCOM_TOPOLOGY_COMPLETE;
	blob_buf_init(&buffer, 0);
	add_common(&buffer);
	blobmsg_add_string(&buffer, "device_id", device->device_id);
	blobmsg_add_u64(&buffer, "generation", device->generation);
	features = blobmsg_open_table(&buffer, "capabilities");
	add_feature(&buffer, "shadow_inventory", "available", "exact_usb_profile");
	add_feature(&buffer, "mbim_topology",
		    complete && g_str_equal(device->composition, "mbim") ?
		    "available" : "unavailable",
		    !g_str_equal(device->composition, "mbim") ?
		    "wrong_composition" : (complete ? "exact_parent_pair" :
		    "topology_incomplete"));
	add_feature(&buffer, "ncm_topology",
		    complete && g_str_equal(device->composition, "ncm") ?
		    "available" : "unavailable",
		    !g_str_equal(device->composition, "ncm") ?
		    "wrong_composition" : (complete ? "candidate_inventory_only" :
		    "topology_incomplete"));
	add_feature(&buffer, "bearer_connect", "unavailable", "shadow_mode");
	add_feature(&buffer, "esim", "unavailable",
		    g_str_equal(device->composition, "mbim") ?
		    "shadow_mode" : "mbim_composition_required");
	add_feature(&buffer, "usb_mode_switch", "unavailable", "shadow_mode");
	add_feature(&buffer, "band_lock", "unavailable", "firmware_unverified");
	add_feature(&buffer, "sim_switch", "unavailable", "probe_required");
	add_feature(&buffer, "cell_lock", "unavailable", "experimental_disabled");
	blobmsg_close_table(&buffer, features);
	return send_buffer(context, request, &buffer);
}

static int
method_diagnostics(struct ubus_context *context, struct ubus_object *object,
		   struct ubus_request_data *request, const char *method,
		   struct blob_attr *message)
{
	static const gchar *const names[] = { "device_id", NULL };
	FibocomUbus *ubus = from_object(object);
	struct blob_attr *parsed[__DEVICE_MAX] = {};
	const GPtrArray *devices = fibocom_discovery_devices(ubus->discovery);
	const FibocomDevice *device = NULL;
	const gchar *requested = NULL;
	struct blob_buf buffer = {};
	void *daemon;
	void *profile;
	void *array;
	guint i;

	(void)method;
	if (message != NULL) {
		if (!message_has_only(message, names))
			return send_error(context, request, "invalid_argument",
					  "unexpected diagnostics argument");
		blobmsg_parse(device_policy, __DEVICE_MAX, parsed, blob_data(message),
			      blob_len(message));
		if (parsed[DEVICE_DEVICE_ID] != NULL) {
			requested = blobmsg_get_string(parsed[DEVICE_DEVICE_ID]);
			if (!valid_device_id(requested))
				return send_error(context, request, "invalid_argument",
						  "device_id is invalid");
			device = fibocom_discovery_find(ubus->discovery, requested);
			if (device == NULL)
				return send_error(context, request, "not_found",
						  "device_id is not present in the cached inventory");
		}
	}
	blob_buf_init(&buffer, 0);
	add_common(&buffer);
	daemon = blobmsg_open_table(&buffer, "daemon");
	blobmsg_add_string(&buffer, "mode", "shadow");
	blobmsg_add_u8(&buffer, "ubus_connected", ubus->connected);
	blobmsg_add_string(&buffer, "ownership", "not-probed");
	blobmsg_add_u8(&buffer, "fibocomd_claims_device", FALSE);
	blobmsg_add_u8(&buffer, "opens_tty", FALSE);
	blobmsg_add_u8(&buffer, "opens_wdm", FALSE);
	blobmsg_add_u8(&buffer, "changes_network", FALSE);
	blobmsg_close_table(&buffer, daemon);
	profile = blobmsg_open_table(&buffer, "profile");
	blobmsg_add_string(&buffer, "id", fibocom_profile_id(
		fibocom_discovery_profile(ubus->discovery)));
	blobmsg_add_string(&buffer, "display_name", fibocom_profile_display_name(
		fibocom_discovery_profile(ubus->discovery)));
	blobmsg_add_u8(&buffer, "loaded", TRUE);
	blobmsg_add_u8(&buffer, "schema_validated", TRUE);
	blobmsg_add_u8(&buffer, "hardware_validated", FALSE);
	blobmsg_add_string(&buffer, "match_confidence", "usb_id_only");
	blobmsg_close_table(&buffer, profile);
	array = blobmsg_open_array(&buffer, "devices");
	if (device != NULL) {
		add_device_diagnostics(&buffer, device);
	} else {
		for (i = 0; i < devices->len; i++)
			add_device_diagnostics(&buffer, g_ptr_array_index(devices, i));
	}
	blobmsg_close_array(&buffer, array);
	add_reconcile(&buffer, ubus->discovery);
	return send_buffer(context, request, &buffer);
}

static int
method_rescan(struct ubus_context *context, struct ubus_object *object,
	      struct ubus_request_data *request, const char *method,
	      struct blob_attr *message)
{
	static const gchar *const names[] = { "reason", "subsystem", "action", NULL };
	static const gchar *const subsystems[] = { "usb", "tty", "net", "manual", NULL };
	static const gchar *const actions[] = {
		"add", "remove", "change", "move", "bind", "unbind",
		"online", "offline", NULL
	};
	FibocomUbus *ubus = from_object(object);
	struct blob_attr *parsed[__RESCAN_MAX] = {};
	const gchar *reason = NULL;
	const gchar *subsystem = NULL;
	const gchar *action = NULL;
	gchar *scan_reason;
	guint64 scan_id;
	struct blob_buf buffer = {};

	(void)method;
	if (!message_has_only(message, names))
		return send_error(context, request, "invalid_argument",
				  "unexpected rescan argument");
	if (message != NULL)
		blobmsg_parse(rescan_policy, __RESCAN_MAX, parsed, blob_data(message),
			      blob_len(message));
	if (parsed[RESCAN_REASON] != NULL)
		reason = blobmsg_get_string(parsed[RESCAN_REASON]);
	if (parsed[RESCAN_SUBSYSTEM] != NULL)
		subsystem = blobmsg_get_string(parsed[RESCAN_SUBSYSTEM]);
	if (parsed[RESCAN_ACTION] != NULL)
		action = blobmsg_get_string(parsed[RESCAN_ACTION]);
	if (!valid_hint(reason) || !value_in_set(subsystem, subsystems) ||
	    !value_in_set(action, actions))
		return send_error(context, request, "invalid_argument",
				  "rescan hint is invalid or exceeds its bound");
	scan_reason = g_strdup_printf("ubus:%s:%s:%s", reason != NULL ? reason : "manual",
				      subsystem != NULL ? subsystem : "manual",
				      action != NULL ? action : "change");
	scan_id = fibocom_discovery_request_scan(ubus->discovery, scan_reason);
	g_free(scan_reason);
	if (scan_id == 0)
		return send_error(context, request, "internal_error",
				  "daemon is stopping");
	blob_buf_init(&buffer, 0);
	add_common(&buffer);
	blobmsg_add_u8(&buffer, "accepted", TRUE);
	blobmsg_add_u64(&buffer, "scan_id", scan_id);
	blobmsg_add_u8(&buffer, "pending", TRUE);
	add_reconcile(&buffer, ubus->discovery);
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
	/* Small bounded jitter prevents a group of services reconnecting in lockstep. */
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
	/* Safe both during source dispatch and for future asynchronous send paths. */
	if (old_source != 0)
		g_source_remove(old_source);
	g_warning("ubus connection lost; scheduling reconnect");
	schedule_reconnect(ubus);
}

static gboolean
ubus_fd_ready(gint fd, GIOCondition condition, gpointer user_data)
{
	FibocomUbus *ubus = user_data;

	(void)fd;
	if (ubus->stopping || !ubus->connected)
		return G_SOURCE_REMOVE;
	if (condition & (G_IO_IN | G_IO_HUP | G_IO_ERR))
		ubus_handle_event(&ubus->context);
	if (ubus->connected && (condition & (G_IO_HUP | G_IO_ERR)))
		connection_lost(&ubus->context);
	if (!ubus->connected || (condition & G_IO_NVAL)) {
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
	ubus->fd_source = g_unix_fd_add_full(
		G_PRIORITY_DEFAULT, ubus->context.sock.fd,
		G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
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
			g_warning("cannot publish fibocom ubus object: %s",
				  ubus_strerror(status));
			ubus_shutdown(&ubus->context);
			ubus->context_initialized = FALSE;
			return FALSE;
		}
	} else {
		status = ubus_reconnect(&ubus->context, ubus->socket_path);
		if (status != 0)
			return FALSE;
		/*
		 * libubus' public reconnect path calls ubus_refresh_state(): it
		 * clears stale type/object IDs and republishes every cached object.
		 * Calling ubus_add_object() again here would double-register it.
		 */
		if (ubus->object.id == 0) {
			g_warning("ubus reconnected but fibocom object was not republished");
			ubus_shutdown(&ubus->context);
			ubus->context_initialized = FALSE;
			ubus->connected = FALSE;
			fibocom_object_type.id = 0;
			return FALSE;
		}
	}
	ubus->connected = TRUE;
	ubus->reconnect_delay_ms = RECONNECT_INITIAL_MS;
	install_fd_source(ubus);
	g_message("fibocom shadow API published on ubus");
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
fibocom_ubus_new(FibocomDiscovery *discovery, const gchar *socket_path)
{
	FibocomUbus *ubus;

	g_return_val_if_fail(discovery != NULL, NULL);
	ubus = g_new0(FibocomUbus, 1);
	ubus->discovery = fibocom_discovery_ref(discovery);
	ubus->socket_path = g_strdup(socket_path);
	ubus->reconnect_delay_ms = RECONNECT_INITIAL_MS;
	ubus->object.name = "fibocom";
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
		g_warning("ubus unavailable; shadow discovery continues while reconnecting");
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
	fibocom_discovery_unref(ubus->discovery);
	g_free(ubus->socket_path);
	g_free(ubus);
}

gboolean
fibocom_ubus_is_connected(const FibocomUbus *ubus)
{
	return ubus != NULL && ubus->connected;
}
