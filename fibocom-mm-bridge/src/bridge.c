/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "bridge.h"
#include "identity.h"

#include <string.h>

#define MANAGER_RETRY_SECONDS 5U
#define RANDOM_RETRIES 8U
#define MAX_ADMITTED_MODEMS 32U
#define MAX_PORTS_OR_DRIVERS 32U

static void bridge_connect(FibocomBridge *bridge);
static void query_sim(FibocomModem *modem);
static void query_bearers(FibocomModem *modem);

FibocomModem *
fibocom_modem_ref(FibocomModem *modem)
{
	g_return_val_if_fail(modem != NULL, NULL);
	g_atomic_int_inc(&modem->refs);
	return modem;
}

static void
modem_mark_removed(FibocomModem *modem)
{
	modem->live = FALSE;
	g_cancellable_cancel(modem->cancellable);
	modem->last_changed_at = g_get_real_time() / G_USEC_PER_SEC;
}

void
fibocom_modem_unref(FibocomModem *modem)
{
	if (modem == NULL || !g_atomic_int_dec_and_test(&modem->refs))
		return;
	g_clear_object(&modem->sim);
	g_list_free_full(modem->bearers, g_object_unref);
	g_clear_object(&modem->cancellable);
	g_clear_object(&modem->modem);
	g_clear_object(&modem->object);
	g_free(modem->modem_id);
	g_free(modem);
}

static gchar *
normalize_alnum(const gchar *value)
{
	GString *normalized;
	const guchar *cursor;

	if (value == NULL || !g_utf8_validate(value, -1, NULL))
		return g_strdup("");
	normalized = g_string_sized_new(strlen(value));
	for (cursor = (const guchar *)value; *cursor != '\0'; cursor++) {
		if (g_ascii_isalnum(*cursor))
			g_string_append_c(normalized, g_ascii_tolower(*cursor));
	}
	return g_string_free(normalized, FALSE);
}

static gboolean
is_exact_fibocom(MMModem *modem)
{
	const gchar *plugin = mm_modem_get_plugin(modem);
	g_autofree gchar *manufacturer = NULL;

	if (plugin == NULL || g_ascii_strcasecmp(plugin, "fibocom") != 0)
		return FALSE;
	manufacturer = normalize_alnum(mm_modem_get_manufacturer(modem));
	return g_str_equal(manufacturer, "fibocom") ||
		g_str_equal(manufacturer, "fibocomwireless") ||
		g_str_equal(manufacturer, "fibocomwirelessinc");
}

gboolean
fibocom_modem_is_l850(FibocomModem *modem)
{
	g_autofree gchar *model = NULL;

	g_return_val_if_fail(modem != NULL, FALSE);
	model = normalize_alnum(mm_modem_get_model(modem->modem));
	return g_str_equal(model, "l850gl");
}

static gboolean
string_contains_ascii_ci(const gchar *value, const gchar *needle)
{
	g_autofree gchar *lower = NULL;
	gboolean found;

	if (value == NULL)
		return FALSE;
	lower = g_ascii_strdown(value, -1);
	found = strstr(lower, needle) != NULL;
	return found;
}

const gchar *
fibocom_modem_composition(FibocomModem *modem)
{
	const gchar *const *drivers;
	const MMModemPortInfo *ports = NULL;
	guint n_ports = 0;
	guint i;

	g_return_val_if_fail(modem != NULL, "unknown");
	drivers = mm_modem_get_drivers(modem->modem);
	if (drivers != NULL) {
		for (i = 0; drivers[i] != NULL && i < MAX_PORTS_OR_DRIVERS; i++) {
			if (string_contains_ascii_ci(drivers[i], "cdc_mbim") ||
			    string_contains_ascii_ci(drivers[i], "mbim"))
				return "mbim";
		}
	}
	if (mm_modem_peek_ports(modem->modem, &ports, &n_ports)) {
		for (i = 0; i < n_ports && i < MAX_PORTS_OR_DRIVERS; i++) {
			if (ports[i].type == MM_MODEM_PORT_TYPE_MBIM)
				return "mbim";
		}
	}
	if (drivers != NULL) {
		for (i = 0; drivers[i] != NULL && i < MAX_PORTS_OR_DRIVERS; i++) {
			if (string_contains_ascii_ci(drivers[i], "cdc_ncm") ||
			    string_contains_ascii_ci(drivers[i], "ncm"))
				return "ncm";
		}
	}
	return "unknown";
}

gboolean
fibocom_modem_is_supported(FibocomModem *modem)
{
	return fibocom_modem_is_l850(modem) &&
		g_str_equal(fibocom_modem_composition(modem), "mbim");
}

const gchar *
fibocom_modem_support_reason(FibocomModem *modem)
{
	const gchar *composition;

	if (!fibocom_modem_is_l850(modem))
		return "unsupported-fibocom-model";
	composition = fibocom_modem_composition(modem);
	if (g_str_equal(composition, "mbim"))
		return "l850-mbim";
	if (g_str_equal(composition, "ncm"))
		return "l850-ncm-data-unsupported";
	return "l850-composition-unknown";
}

static gboolean
modem_id_exists(FibocomBridge *bridge, const gchar *modem_id)
{
	GHashTableIter iter;
	gpointer value;

	g_hash_table_iter_init(&iter, bridge->modems_by_path);
	while (g_hash_table_iter_next(&iter, NULL, &value)) {
		FibocomModem *modem = value;

		if (g_str_equal(modem->modem_id, modem_id))
			return TRUE;
	}
	return FALSE;
}

static gchar *
new_modem_id(FibocomBridge *bridge)
{
	guint attempt;

	for (attempt = 0; attempt < RANDOM_RETRIES; attempt++) {
		gchar candidate[FIBOCOM_ID_BUFSIZE];

		if (!fibocom_identity_generate(candidate)) {
			bridge->random_failed = TRUE;
			return NULL;
		}
		if (!modem_id_exists(bridge, candidate))
			return g_strdup(candidate);
	}
	bridge->random_failed = TRUE;
	return NULL;
}

static void
sim_ready(GObject *source, GAsyncResult *result, gpointer user_data)
{
	FibocomModem *modem = user_data;
	g_autoptr(GError) error = NULL;
	MMSim *sim;
	gboolean requery;

	sim = mm_modem_get_sim_finish(MM_MODEM(source), result, &error);
	modem->sim_query_pending = FALSE;
	requery = modem->sim_query_dirty;
	modem->sim_query_dirty = FALSE;
	if (modem->live) {
		g_clear_object(&modem->sim);
		if (sim != NULL) {
			modem->sim = sim;
			sim = NULL;
			modem->sim_cache_state = "ready";
		} else {
			modem->sim_cache_state = g_error_matches(error, G_IO_ERROR,
				G_IO_ERROR_CANCELLED) ? "cancelled" : "unavailable";
		}
		modem->last_changed_at = g_get_real_time() / G_USEC_PER_SEC;
	}
	g_clear_object(&sim);
	if (modem->live && requery)
		query_sim(modem);
	fibocom_modem_unref(modem);
}

static void
query_sim(FibocomModem *modem)
{
	const gchar *sim_path;

	if (!modem->live)
		return;
	if (modem->sim_query_pending) {
		modem->sim_query_dirty = TRUE;
		return;
	}
	sim_path = mm_modem_get_sim_path(modem->modem);
	if (sim_path == NULL || g_str_equal(sim_path, "/")) {
		g_clear_object(&modem->sim);
		modem->sim_cache_state = "absent";
		return;
	}
	modem->sim_query_pending = TRUE;
	modem->sim_cache_state = "loading";
	mm_modem_get_sim(modem->modem, modem->cancellable, sim_ready,
			 fibocom_modem_ref(modem));
}

static void
bearers_ready(GObject *source, GAsyncResult *result, gpointer user_data)
{
	FibocomModem *modem = user_data;
	g_autoptr(GError) error = NULL;
	GList *bearers;
	gboolean requery;

	bearers = mm_modem_list_bearers_finish(MM_MODEM(source), result, &error);
	modem->bearer_query_pending = FALSE;
	requery = modem->bearer_query_dirty;
	modem->bearer_query_dirty = FALSE;
	if (modem->live) {
		g_list_free_full(modem->bearers, g_object_unref);
		modem->bearers = NULL;
		if (bearers != NULL) {
			modem->bearers = bearers;
			bearers = NULL;
			modem->bearer_cache_state = "ready";
		} else if (error == NULL) {
			modem->bearer_cache_state = "ready";
		} else {
			modem->bearer_cache_state = g_error_matches(error, G_IO_ERROR,
				G_IO_ERROR_CANCELLED) ? "cancelled" : "unavailable";
		}
		modem->last_changed_at = g_get_real_time() / G_USEC_PER_SEC;
	}
	g_list_free_full(bearers, g_object_unref);
	if (modem->live && requery)
		query_bearers(modem);
	fibocom_modem_unref(modem);
}

static void
query_bearers(FibocomModem *modem)
{
	if (!modem->live)
		return;
	if (modem->bearer_query_pending) {
		modem->bearer_query_dirty = TRUE;
		return;
	}
	modem->bearer_query_pending = TRUE;
	modem->bearer_cache_state = "loading";
	mm_modem_list_bearers(modem->modem, modem->cancellable, bearers_ready,
			      fibocom_modem_ref(modem));
}

static void
remove_path(FibocomBridge *bridge, const gchar *path)
{
	FibocomModem *modem = g_hash_table_lookup(bridge->modems_by_path, path);

	if (modem == NULL)
		return;
	modem_mark_removed(modem);
	g_hash_table_remove(bridge->modems_by_path, path);
}

static void
clear_modems(FibocomBridge *bridge)
{
	GHashTableIter iter;
	gpointer value;

	g_hash_table_iter_init(&iter, bridge->modems_by_path);
	while (g_hash_table_iter_next(&iter, NULL, &value))
		modem_mark_removed(value);
	g_hash_table_remove_all(bridge->modems_by_path);
}

static void
reconcile_object(FibocomBridge *bridge, GDBusObject *object)
{
	const gchar *path = g_dbus_object_get_object_path(object);
	FibocomModem *existing;
	MMModem *mm_modem;
	FibocomModem *modem;
	g_autofree gchar *modem_id = NULL;

	if (!MM_IS_OBJECT(object)) {
		remove_path(bridge, path);
		return;
	}
	mm_modem = mm_object_get_modem(MM_OBJECT(object));
	if (mm_modem == NULL || !is_exact_fibocom(mm_modem)) {
		g_clear_object(&mm_modem);
		remove_path(bridge, path);
		return;
	}
	existing = g_hash_table_lookup(bridge->modems_by_path, path);
	if (existing != NULL) {
		existing->last_changed_at = g_get_real_time() / G_USEC_PER_SEC;
		g_object_unref(mm_modem);
		return;
	}
	if (g_hash_table_size(bridge->modems_by_path) >= MAX_ADMITTED_MODEMS) {
		g_object_unref(mm_modem);
		return;
	}
	if (bridge->next_generation == G_MAXUINT32) {
		g_object_unref(mm_modem);
		return;
	}
	modem_id = new_modem_id(bridge);
	if (modem_id == NULL) {
		g_warning("secure random unavailable; Fibocom object not admitted");
		g_object_unref(mm_modem);
		return;
	}
	modem = g_new0(FibocomModem, 1);
	modem->refs = 1;
	modem->live = TRUE;
	modem->object = g_object_ref(MM_OBJECT(object));
	modem->modem = mm_modem;
	modem->cancellable = g_cancellable_new();
	modem->modem_id = g_steal_pointer(&modem_id);
	modem->generation = ++bridge->next_generation;
	modem->admitted_at = g_get_real_time() / G_USEC_PER_SEC;
	modem->last_changed_at = modem->admitted_at;
	modem->sim_cache_state = "loading";
	modem->bearer_cache_state = "loading";
	g_hash_table_insert(bridge->modems_by_path, g_strdup(path), modem);
	query_sim(modem);
	query_bearers(modem);
}

static void
object_added(GDBusObjectManager *manager, GDBusObject *object,
	     gpointer user_data)
{
	(void)manager;
	reconcile_object(user_data, object);
}

static void
object_removed(GDBusObjectManager *manager, GDBusObject *object,
	       gpointer user_data)
{
	FibocomBridge *bridge = user_data;

	(void)manager;
	remove_path(bridge, g_dbus_object_get_object_path(object));
}

static void
interface_added(GDBusObjectManager *manager, GDBusObject *object,
		GDBusInterface *interface, gpointer user_data)
{
	(void)manager;
	(void)interface;
	reconcile_object(user_data, object);
}

static void
interface_removed(GDBusObjectManager *manager, GDBusObject *object,
		  GDBusInterface *interface, gpointer user_data)
{
	(void)manager;
	(void)interface;
	reconcile_object(user_data, object);
}

static gboolean
variant_has_key(GVariant *dictionary, const gchar *key)
{
	GVariant *value;

	if (dictionary == NULL)
		return FALSE;
	value = g_variant_lookup_value(dictionary, key, NULL);
	if (value == NULL)
		return FALSE;
	g_variant_unref(value);
	return TRUE;
}

static void
properties_changed(GDBusObjectManagerClient *manager,
		   GDBusObjectProxy *object_proxy,
		   GDBusProxy *interface_proxy,
		   GVariant *changed_properties,
		   const gchar *const *invalidated_properties,
		   gpointer user_data)
{
	FibocomBridge *bridge = user_data;
	const gchar *path;
	const gchar *interface_name;
	FibocomModem *modem;

	(void)manager;
	(void)invalidated_properties;
	reconcile_object(bridge, G_DBUS_OBJECT(object_proxy));
	path = g_dbus_object_get_object_path(G_DBUS_OBJECT(object_proxy));
	modem = g_hash_table_lookup(bridge->modems_by_path, path);
	if (modem == NULL)
		return;
	modem->last_changed_at = g_get_real_time() / G_USEC_PER_SEC;
	interface_name = g_dbus_proxy_get_interface_name(interface_proxy);
	if (!g_str_equal(interface_name, MM_DBUS_INTERFACE_MODEM))
		return;
	if (variant_has_key(changed_properties, "Sim"))
		query_sim(modem);
	if (variant_has_key(changed_properties, "Bearers"))
		query_bearers(modem);
}

static gboolean
manager_has_owner(FibocomBridge *bridge)
{
	g_autofree gchar *owner = NULL;

	if (bridge->manager == NULL)
		return FALSE;
	owner = g_dbus_object_manager_client_get_name_owner(
		G_DBUS_OBJECT_MANAGER_CLIENT(bridge->manager));
	return owner != NULL;
}

static void
reconcile_all(FibocomBridge *bridge)
{
	GList *objects;
	GList *cursor;

	if (!manager_has_owner(bridge))
		return;
	objects = g_dbus_object_manager_get_objects(G_DBUS_OBJECT_MANAGER(
		bridge->manager));
	for (cursor = objects; cursor != NULL; cursor = cursor->next)
		reconcile_object(bridge, G_DBUS_OBJECT(cursor->data));
	g_list_free_full(objects, g_object_unref);
}

static void
name_owner_changed(GObject *object, GParamSpec *spec, gpointer user_data)
{
	FibocomBridge *bridge = user_data;

	(void)object;
	(void)spec;
	if (!manager_has_owner(bridge)) {
		clear_modems(bridge);
		return;
	}
	reconcile_all(bridge);
}

static gboolean
retry_connect_cb(gpointer user_data)
{
	FibocomBridge *bridge = user_data;

	bridge->retry_source = 0;
	if (!bridge->stopping)
		bridge_connect(bridge);
	return G_SOURCE_REMOVE;
}

static void
schedule_retry(FibocomBridge *bridge)
{
	if (bridge->stopping || bridge->retry_source != 0)
		return;
	bridge->retry_source = g_timeout_add_seconds(MANAGER_RETRY_SECONDS,
						    retry_connect_cb, bridge);
}

static void
manager_ready(GObject *source, GAsyncResult *result, gpointer user_data)
{
	FibocomBridge *bridge = user_data;
	g_autoptr(GError) error = NULL;
	MMManager *manager;

	(void)source;
	manager = mm_manager_new_finish(result, &error);
	if (bridge->stopping) {
		g_clear_object(&manager);
		return;
	}
	if (manager == NULL) {
		g_clear_object(&bridge->connection);
		g_warning("ModemManager object manager unavailable; retry scheduled");
		schedule_retry(bridge);
		return;
	}
	bridge->manager = manager;
	g_signal_connect(manager, "object-added", G_CALLBACK(object_added), bridge);
	g_signal_connect(manager, "object-removed", G_CALLBACK(object_removed), bridge);
	g_signal_connect(manager, "interface-added", G_CALLBACK(interface_added), bridge);
	g_signal_connect(manager, "interface-removed", G_CALLBACK(interface_removed), bridge);
	g_signal_connect(manager, "interface-proxy-properties-changed",
			 G_CALLBACK(properties_changed), bridge);
	g_signal_connect(manager, "notify::name-owner",
			 G_CALLBACK(name_owner_changed), bridge);
	reconcile_all(bridge);
}

static void
bus_ready(GObject *source, GAsyncResult *result, gpointer user_data)
{
	FibocomBridge *bridge = user_data;
	g_autoptr(GError) error = NULL;
	GDBusConnection *connection;

	(void)source;
	connection = g_bus_get_finish(result, &error);
	if (bridge->stopping) {
		g_clear_object(&connection);
		return;
	}
	if (connection == NULL) {
		g_warning("system D-Bus unavailable; retry scheduled");
		schedule_retry(bridge);
		return;
	}
	bridge->connection = connection;
	mm_manager_new(connection,
		       G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_DO_NOT_AUTO_START,
		       bridge->cancellable, manager_ready, bridge);
}

static void
bridge_connect(FibocomBridge *bridge)
{
	if (bridge->stopping || bridge->manager != NULL ||
	    bridge->connection != NULL)
		return;
	g_bus_get(G_BUS_TYPE_SYSTEM, bridge->cancellable, bus_ready, bridge);
}

FibocomBridge *
fibocom_bridge_new(void)
{
	FibocomBridge *bridge = g_new0(FibocomBridge, 1);

	bridge->cancellable = g_cancellable_new();
	bridge->modems_by_path = g_hash_table_new_full(g_str_hash, g_str_equal,
						      g_free,
						      (GDestroyNotify)fibocom_modem_unref);
	return bridge;
}

void
fibocom_bridge_start(FibocomBridge *bridge)
{
	g_return_if_fail(bridge != NULL);
	if (bridge->started || bridge->stopping)
		return;
	bridge->started = TRUE;
	bridge_connect(bridge);
}

void
fibocom_bridge_stop(FibocomBridge *bridge)
{
	if (bridge == NULL || bridge->stopping)
		return;
	bridge->stopping = TRUE;
	if (bridge->retry_source != 0) {
		g_source_remove(bridge->retry_source);
		bridge->retry_source = 0;
	}
	g_cancellable_cancel(bridge->cancellable);
	clear_modems(bridge);
	g_clear_object(&bridge->manager);
	g_clear_object(&bridge->connection);
}

void
fibocom_bridge_free(FibocomBridge *bridge)
{
	if (bridge == NULL)
		return;
	fibocom_bridge_stop(bridge);
	g_hash_table_unref(bridge->modems_by_path);
	g_object_unref(bridge->cancellable);
	g_free(bridge);
}

FibocomModem *
fibocom_bridge_find_modem(FibocomBridge *bridge, const gchar *modem_id)
{
	GHashTableIter iter;
	gpointer value;

	if (bridge == NULL || modem_id == NULL)
		return NULL;
	g_hash_table_iter_init(&iter, bridge->modems_by_path);
	while (g_hash_table_iter_next(&iter, NULL, &value)) {
		FibocomModem *modem = value;

		if (modem->live && g_str_equal(modem->modem_id, modem_id))
			return modem;
	}
	return NULL;
}

GPtrArray *
fibocom_bridge_snapshot_modems(FibocomBridge *bridge)
{
	GHashTableIter iter;
	gpointer value;
	GPtrArray *snapshot;

	g_return_val_if_fail(bridge != NULL, NULL);
	snapshot = g_ptr_array_new_with_free_func((GDestroyNotify)fibocom_modem_unref);
	g_hash_table_iter_init(&iter, bridge->modems_by_path);
	while (g_hash_table_iter_next(&iter, NULL, &value)) {
		FibocomModem *modem = value;

		if (modem->live)
			g_ptr_array_add(snapshot, fibocom_modem_ref(modem));
	}
	return snapshot;
}

gboolean
fibocom_bridge_manager_available(FibocomBridge *bridge)
{
	return bridge != NULL && manager_has_owner(bridge);
}

const gchar *
fibocom_bridge_manager_version(FibocomBridge *bridge)
{
	const gchar *version;

	if (!fibocom_bridge_manager_available(bridge))
		return "";
	version = mm_manager_get_version(bridge->manager);
	return version != NULL ? version : "";
}
