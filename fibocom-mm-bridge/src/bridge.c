/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "bridge.h"
#include "hardware.h"
#include "identity.h"

#include <string.h>

#define MANAGER_RETRY_SECONDS 5U
#define RANDOM_RETRIES 8U
#define MAX_ADMITTED_MODEMS 32U
#define MAX_PORTS_OR_DRIVERS 32U
#define MAX_SMS_CACHE 1024U
#define SMS_RECONCILE_SECONDS 30U
#define MM_LONG_PROXY_TIMEOUT_MS 310000

static void bridge_connect(FibocomBridge *bridge);
static void schedule_retry(FibocomBridge *bridge);
static void query_sim(FibocomModem *modem);
static void query_bearers(FibocomModem *modem);
static void query_sms(FibocomModem *modem);
static void reconcile_messaging(FibocomModem *modem, gboolean force_epoch);

typedef struct {
	FibocomModem *modem;
	guint32 messaging_generation;
} SmsQueryContext;

typedef struct {
	FibocomBridge *bridge;
	guint64 serial;
} BusConnectContext;

typedef struct {
	FibocomBridge *bridge;
	GDBusConnection *connection;
	guint64 serial;
} ManagerConnectContext;

static FibocomBridge *
bridge_ref(FibocomBridge *bridge)
{
	g_return_val_if_fail(bridge != NULL, NULL);
	g_atomic_int_inc(&bridge->refs);
	return bridge;
}

static void
bridge_unref(FibocomBridge *bridge)
{
	if (bridge == NULL || !g_atomic_int_dec_and_test(&bridge->refs))
		return;
	g_hash_table_unref(bridge->modems_by_path);
	g_object_unref(bridge->cancellable);
	g_free(bridge);
}

static ManagerConnectContext *
manager_connect_context_new(FibocomBridge *bridge,
			    GDBusConnection *connection, guint64 serial)
{
	ManagerConnectContext *connect = g_new0(ManagerConnectContext, 1);

	connect->bridge = bridge_ref(bridge);
	connect->connection = g_object_ref(connection);
	connect->serial = serial;
	return connect;
}

static void
manager_connect_context_free(ManagerConnectContext *connect)
{
	if (connect == NULL)
		return;
	g_object_unref(connect->connection);
	bridge_unref(connect->bridge);
	g_free(connect);
}

static BusConnectContext *
bus_connect_context_new(FibocomBridge *bridge, guint64 serial)
{
	BusConnectContext *connect = g_new0(BusConnectContext, 1);

	connect->bridge = bridge_ref(bridge);
	connect->serial = serial;
	return connect;
}

static void
bus_connect_context_free(BusConnectContext *connect)
{
	if (connect == NULL)
		return;
	bridge_unref(connect->bridge);
	g_free(connect);
}

static guint64
bridge_next_connect_serial(FibocomBridge *bridge)
{
	bridge->connect_serial++;
	if (bridge->connect_serial == 0)
		bridge->connect_serial++;
	return bridge->connect_serial;
}

FibocomModem *
fibocom_modem_ref(FibocomModem *modem)
{
	g_return_val_if_fail(modem != NULL, NULL);
	g_atomic_int_inc(&modem->refs);
	return modem;
}

FibocomSms *
fibocom_sms_ref(FibocomSms *sms)
{
	g_return_val_if_fail(sms != NULL, NULL);
	g_atomic_int_inc(&sms->refs);
	return sms;
}

static void
sms_detach(FibocomSms *sms)
{
	if (sms->properties_handler != 0 && sms->sms != NULL) {
		g_signal_handler_disconnect(sms->sms, sms->properties_handler);
		sms->properties_handler = 0;
	}
	sms->owner = NULL;
}

void
fibocom_sms_unref(FibocomSms *sms)
{
	if (sms == NULL || !g_atomic_int_dec_and_test(&sms->refs))
		return;
	sms_detach(sms);
	g_clear_object(&sms->sms);
	g_free(sms->sms_id);
	g_free(sms);
}

static void
sms_dedupe_free(FibocomSmsDedupe *entry)
{
	if (entry == NULL)
		return;
	g_free(entry->client_token);
	g_free(entry->sms_id);
	g_free(entry->state);
	g_free(entry->error_code);
	g_free(entry);
}

static void
clear_sms_cache(FibocomModem *modem)
{
	GHashTableIter iter;
	gpointer value;

	if (modem->sms_by_path == NULL)
		return;
	g_hash_table_iter_init(&iter, modem->sms_by_path);
	while (g_hash_table_iter_next(&iter, NULL, &value))
		sms_detach(value);
	g_hash_table_remove_all(modem->sms_by_path);
}

static void
disconnect_messaging_signals(FibocomModem *modem)
{
	if (modem->messaging == NULL)
		return;
	if (modem->messaging_added_handler != 0) {
		g_signal_handler_disconnect(modem->messaging,
			modem->messaging_added_handler);
		modem->messaging_added_handler = 0;
	}
	if (modem->messaging_deleted_handler != 0) {
		g_signal_handler_disconnect(modem->messaging,
			modem->messaging_deleted_handler);
		modem->messaging_deleted_handler = 0;
	}
}

static void
modem_mark_removed(FibocomModem *modem)
{
	modem->live = FALSE;
	modem->serving_cell_valid = FALSE;
	modem->serving_cell_refresh_pending = FALSE;
	modem->serving_cell_reason = "device-gone";
	g_cancellable_cancel(modem->cancellable);
	if (modem->mutation_cancellable != NULL)
		g_cancellable_cancel(modem->mutation_cancellable);
	disconnect_messaging_signals(modem);
	clear_sms_cache(modem);
	modem->last_changed_at = g_get_real_time() / G_USEC_PER_SEC;
}

void
fibocom_modem_unref(FibocomModem *modem)
{
	if (modem == NULL || !g_atomic_int_dec_and_test(&modem->refs))
		return;
	g_clear_object(&modem->sim);
	g_list_free_full(modem->bearers, g_object_unref);
	disconnect_messaging_signals(modem);
	clear_sms_cache(modem);
	g_clear_pointer(&modem->sms_by_path, g_hash_table_unref);
	g_clear_pointer(&modem->sms_reserved_ids, g_hash_table_unref);
	g_clear_pointer(&modem->sms_deleted_paths, g_hash_table_unref);
	if (modem->sms_dedupe != NULL) {
		g_queue_free_full(modem->sms_dedupe,
			(GDestroyNotify)sms_dedupe_free);
		modem->sms_dedupe = NULL;
	}
	g_clear_object(&modem->mutation_cancellable);
	g_clear_object(&modem->messaging);
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

gboolean
fibocom_modem_attest_mutation_target(FibocomModem *modem)
{
	const gchar *physdev;

	if (modem == NULL || !modem->live || !fibocom_modem_is_supported(modem))
		return FALSE;
	physdev = mm_modem_get_physdev(modem->modem);
	return fibocom_hardware_attest_l850_mbim(physdev);
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
sms_revision_bump(FibocomModem *modem)
{
	if (modem->sms_revision < G_MAXUINT64)
		modem->sms_revision++;
	modem->last_changed_at = g_get_real_time() / G_USEC_PER_SEC;
}

static gint
sms_proxy_newest_first(gconstpointer left, gconstpointer right)
{
	MMSms *left_sms = MM_SMS((gpointer)left);
	MMSms *right_sms = MM_SMS((gpointer)right);
	gint compared;

	compared = g_strcmp0(mm_sms_get_timestamp(right_sms),
			     mm_sms_get_timestamp(left_sms));
	if (compared != 0)
		return compared;
	return g_strcmp0(mm_sms_get_path(left_sms), mm_sms_get_path(right_sms));
}

static void
sms_properties_changed(GDBusProxy *proxy, GVariant *changed_properties,
		       const gchar *const *invalidated_properties,
		       gpointer user_data)
{
	FibocomSms *entry = user_data;

	(void)proxy;
	(void)changed_properties;
	(void)invalidated_properties;
	if (entry->owner != NULL && entry->owner->live &&
	    entry->modem_generation == entry->owner->generation &&
	    entry->messaging_generation ==
		entry->owner->messaging_generation)
		sms_revision_bump(entry->owner);
}

static void
sms_set_proxy(FibocomSms *entry, FibocomModem *modem, MMSms *sms)
{
	if (entry->sms == sms && entry->owner == modem)
		return;
	sms_detach(entry);
	g_set_object(&entry->sms, sms);
	g_dbus_proxy_set_default_timeout(G_DBUS_PROXY(entry->sms),
		MM_LONG_PROXY_TIMEOUT_MS);
	entry->owner = modem;
	entry->modem_generation = modem->generation;
	entry->messaging_generation = modem->messaging_generation;
	entry->properties_handler = g_signal_connect(entry->sms,
		"g-properties-changed", G_CALLBACK(sms_properties_changed), entry);
}

static gboolean
sms_id_exists_in_hash(GHashTable *cache, const gchar *sms_id)
{
	GHashTableIter iter;
	gpointer value;

	if (cache == NULL)
		return FALSE;
	g_hash_table_iter_init(&iter, cache);
	while (g_hash_table_iter_next(&iter, NULL, &value)) {
		FibocomSms *entry = value;

		if (g_str_equal(entry->sms_id, sms_id))
			return TRUE;
	}
	return FALSE;
}

static gchar *
new_sms_id(FibocomModem *modem, GHashTable *additional_cache)
{
	guint attempt;

	for (attempt = 0; attempt < RANDOM_RETRIES; attempt++) {
		gchar candidate[FIBOCOM_SMS_ID_BUFSIZE];

		if (!fibocom_sms_identity_generate(candidate))
			return NULL;
		if (!sms_id_exists_in_hash(modem->sms_by_path, candidate) &&
		    !sms_id_exists_in_hash(additional_cache, candidate) &&
		    !g_hash_table_contains(modem->sms_reserved_ids, candidate))
			return g_strdup(candidate);
	}
	return NULL;
}

gchar *
fibocom_modem_reserve_sms_id(FibocomModem *modem)
{
	gchar *sms_id;

	g_return_val_if_fail(modem != NULL, NULL);
	if (!modem->live || modem->sms_reserved_ids == NULL)
		return NULL;
	sms_id = new_sms_id(modem, NULL);
	if (sms_id == NULL)
		return NULL;
	g_hash_table_add(modem->sms_reserved_ids, g_strdup(sms_id));
	return sms_id;
}

void
fibocom_modem_release_sms_id(FibocomModem *modem, const gchar *sms_id)
{
	if (modem != NULL && modem->sms_reserved_ids != NULL && sms_id != NULL)
		g_hash_table_remove(modem->sms_reserved_ids, sms_id);
}

static FibocomSms *
sms_entry_new(FibocomModem *modem, MMSms *sms,
	      GHashTable *additional_cache, const gchar *reserved_id)
{
	g_autofree gchar *sms_id = reserved_id != NULL ?
		g_strdup(reserved_id) : new_sms_id(modem, additional_cache);
	FibocomSms *entry;

	if (sms_id == NULL)
		return NULL;
	entry = g_new0(FibocomSms, 1);
	entry->refs = 1;
	entry->sms_id = g_steal_pointer(&sms_id);
	sms_set_proxy(entry, modem, sms);
	return entry;
}

static FibocomSms *
modem_admit_sms(FibocomModem *modem, MMSms *sms,
		const gchar *reserved_id)
{
	const gchar *path;
	FibocomSms *entry;

	g_return_val_if_fail(modem != NULL, NULL);
	g_return_val_if_fail(MM_IS_SMS(sms), NULL);
	if (!modem->live || modem->messaging == NULL ||
	    modem->sms_by_path == NULL)
		return NULL;
	if (reserved_id != NULL &&
	    (!fibocom_sms_identity_is_valid(reserved_id) ||
	     modem->sms_reserved_ids == NULL ||
	     !g_hash_table_contains(modem->sms_reserved_ids, reserved_id)))
		return NULL;
	path = mm_sms_get_path(sms);
	if (path == NULL || !g_variant_is_object_path(path))
		return NULL;
	entry = g_hash_table_lookup(modem->sms_by_path, path);
	if (entry != NULL) {
		if (reserved_id != NULL &&
		    !g_str_equal(entry->sms_id, reserved_id)) {
			if (sms_id_exists_in_hash(modem->sms_by_path, reserved_id))
				return NULL;
			g_free(entry->sms_id);
			entry->sms_id = g_strdup(reserved_id);
			sms_revision_bump(modem);
		}
		sms_set_proxy(entry, modem, sms);
		fibocom_modem_release_sms_id(modem, reserved_id);
		return fibocom_sms_ref(entry);
	}
	if (g_hash_table_size(modem->sms_by_path) >= MAX_SMS_CACHE) {
		modem->sms_cache_truncated = TRUE;
		return NULL;
	}
	entry = sms_entry_new(modem, sms, NULL, reserved_id);
	if (entry == NULL) {
		modem->sms_cache_state = "unavailable";
		clear_sms_cache(modem);
		return NULL;
	}
	g_hash_table_insert(modem->sms_by_path, g_strdup(path), entry);
	fibocom_modem_release_sms_id(modem, reserved_id);
	sms_revision_bump(modem);
	return fibocom_sms_ref(entry);
}

FibocomSms *
fibocom_modem_admit_sms(FibocomModem *modem, MMSms *sms)
{
	return modem_admit_sms(modem, sms, NULL);
}

FibocomSms *
fibocom_modem_admit_reserved_sms(FibocomModem *modem, MMSms *sms,
				  const gchar *sms_id)
{
	return modem_admit_sms(modem, sms, sms_id);
}

FibocomSms *
fibocom_modem_find_sms(FibocomModem *modem, const gchar *sms_id)
{
	GHashTableIter iter;
	gpointer value;

	if (modem == NULL || !fibocom_sms_identity_is_valid(sms_id) ||
	    modem->sms_by_path == NULL)
		return NULL;
	g_hash_table_iter_init(&iter, modem->sms_by_path);
	while (g_hash_table_iter_next(&iter, NULL, &value)) {
		FibocomSms *entry = value;

		if (entry->modem_generation == modem->generation &&
		    entry->messaging_generation == modem->messaging_generation &&
		    g_str_equal(entry->sms_id, sms_id))
			return fibocom_sms_ref(entry);
	}
	return NULL;
}

GPtrArray *
fibocom_modem_snapshot_sms(FibocomModem *modem)
{
	GHashTableIter iter;
	gpointer value;
	GPtrArray *snapshot;

	g_return_val_if_fail(modem != NULL, NULL);
	snapshot = g_ptr_array_new_with_free_func(
		(GDestroyNotify)fibocom_sms_unref);
	if (modem->sms_by_path == NULL)
		return snapshot;
	g_hash_table_iter_init(&iter, modem->sms_by_path);
	while (g_hash_table_iter_next(&iter, NULL, &value))
		g_ptr_array_add(snapshot, fibocom_sms_ref(value));
	return snapshot;
}

static void
sms_list_ready(GObject *source, GAsyncResult *result, gpointer user_data)
{
	SmsQueryContext *query = user_data;
	FibocomModem *modem = query->modem;
	g_autoptr(GError) error = NULL;
	GHashTable *old_cache;
	GHashTable *new_cache;
	GHashTable *seen_deleted;
	GHashTableIter deleted_iter;
	gpointer deleted_path;
	GList *messages;
	GList *cursor;
	gboolean requery = FALSE;
	gboolean truncated = FALSE;
	guint count = 0;

	messages = mm_modem_messaging_list_finish(MM_MODEM_MESSAGING(source),
		result, &error);
	if (!modem->live || modem->messaging != MM_MODEM_MESSAGING(source) ||
	    modem->messaging_generation != query->messaging_generation)
		goto out;
	modem->sms_query_pending = FALSE;
	requery = modem->sms_query_dirty;
	modem->sms_query_dirty = FALSE;
	if (messages == NULL && error != NULL) {
		modem->sms_cache_state = g_error_matches(error, G_IO_ERROR,
			G_IO_ERROR_CANCELLED) ? "cancelled" : "unavailable";
		clear_sms_cache(modem);
		sms_revision_bump(modem);
		goto out;
	}
	messages = g_list_sort(messages, sms_proxy_newest_first);
	old_cache = modem->sms_by_path;
	new_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
		(GDestroyNotify)fibocom_sms_unref);
	seen_deleted = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
		NULL);
	for (cursor = messages; cursor != NULL; cursor = cursor->next) {
		MMSms *sms = MM_SMS(cursor->data);
		const gchar *path = mm_sms_get_path(sms);
		FibocomSms *entry;

		if (path == NULL || !g_variant_is_object_path(path))
			continue;
		if (g_hash_table_contains(modem->sms_deleted_paths, path)) {
			g_hash_table_add(seen_deleted, g_strdup(path));
			continue;
		}
		if (count >= MAX_SMS_CACHE) {
			truncated = TRUE;
			continue;
		}
		entry = g_hash_table_lookup(old_cache, path);
		if (entry != NULL) {
			gpointer old_key = NULL;

			(void)g_hash_table_lookup_extended(old_cache, path,
				&old_key, NULL);
			g_hash_table_steal(old_cache, path);
			g_free(old_key);
			sms_set_proxy(entry, modem, sms);
		} else {
			entry = sms_entry_new(modem, sms, new_cache, NULL);
			if (entry == NULL) {
				g_hash_table_unref(seen_deleted);
				g_hash_table_unref(new_cache);
				clear_sms_cache(modem);
				modem->sms_cache_state = "unavailable";
				sms_revision_bump(modem);
				goto out;
			}
		}
		g_hash_table_insert(new_cache, g_strdup(path), entry);
		count++;
	}
	clear_sms_cache(modem);
	g_hash_table_unref(old_cache);
	modem->sms_by_path = new_cache;
	modem->sms_cache_truncated = truncated;
	modem->sms_cache_state = truncated ? "ready-truncated" : "ready";
	g_hash_table_iter_init(&deleted_iter, modem->sms_deleted_paths);
	while (g_hash_table_iter_next(&deleted_iter, &deleted_path, NULL)) {
		if (!g_hash_table_contains(seen_deleted, deleted_path))
			g_hash_table_iter_remove(&deleted_iter);
	}
	g_hash_table_unref(seen_deleted);
	sms_revision_bump(modem);

out:
	g_list_free_full(messages, g_object_unref);
	if (modem->live && requery)
		query_sms(modem);
	fibocom_modem_unref(modem);
	g_free(query);
}

static void
query_sms(FibocomModem *modem)
{
	SmsQueryContext *query;

	if (!modem->live || modem->messaging == NULL)
		return;
	if (modem->sms_query_pending) {
		modem->sms_query_dirty = TRUE;
		return;
	}
	modem->sms_query_pending = TRUE;
	if (!g_str_equal(modem->sms_cache_state, "ready") &&
	    !g_str_equal(modem->sms_cache_state, "ready-truncated"))
		modem->sms_cache_state = "loading";
	query = g_new0(SmsQueryContext, 1);
	query->modem = fibocom_modem_ref(modem);
	query->messaging_generation = modem->messaging_generation;
	mm_modem_messaging_list(modem->messaging, modem->cancellable,
		sms_list_ready, query);
}

void
fibocom_modem_refresh_sms(FibocomModem *modem)
{
	if (modem != NULL)
		query_sms(modem);
}

static void
sms_added(MMModemMessaging *messaging, const gchar *path,
	  gboolean received, gpointer user_data)
{
	FibocomModem *modem = user_data;

	(void)messaging;
	(void)received;
	if (!modem->live)
		return;
	if (path != NULL && g_variant_is_object_path(path))
		g_hash_table_remove(modem->sms_deleted_paths, path);
	sms_revision_bump(modem);
	query_sms(modem);
}

static void
sms_deleted(MMModemMessaging *messaging, const gchar *path,
	    gpointer user_data)
{
	FibocomModem *modem = user_data;
	FibocomSms *entry;

	(void)messaging;
	if (!modem->live || path == NULL || !g_variant_is_object_path(path) ||
	    modem->sms_by_path == NULL || modem->sms_deleted_paths == NULL)
		return;
	if (g_hash_table_size(modem->sms_deleted_paths) < MAX_SMS_CACHE)
		g_hash_table_add(modem->sms_deleted_paths, g_strdup(path));
	entry = g_hash_table_lookup(modem->sms_by_path, path);
	if (entry != NULL) {
		sms_detach(entry);
		g_hash_table_remove(modem->sms_by_path, path);
		sms_revision_bump(modem);
	}
	query_sms(modem);
}

static void
reconcile_messaging(FibocomModem *modem, gboolean force_epoch)
{
	MMModemMessaging *messaging;

	messaging = mm_object_get_modem_messaging(modem->object);
	if (!force_epoch && messaging == modem->messaging) {
		g_clear_object(&messaging);
		return;
	}
	if (modem->mutation_kind == FIBOCOM_MUTATION_SMS &&
	    modem->mutation_cancellable != NULL)
		g_cancellable_cancel(modem->mutation_cancellable);
	disconnect_messaging_signals(modem);
	g_clear_object(&modem->messaging);
	clear_sms_cache(modem);
	if (modem->sms_reserved_ids != NULL)
		g_hash_table_remove_all(modem->sms_reserved_ids);
	if (modem->sms_deleted_paths != NULL)
		g_hash_table_remove_all(modem->sms_deleted_paths);
	modem->sms_query_pending = FALSE;
	modem->sms_query_dirty = FALSE;
	modem->sms_cache_truncated = FALSE;
	if (modem->sms_dedupe != NULL)
		g_queue_clear_full(modem->sms_dedupe,
			(GDestroyNotify)sms_dedupe_free);
	if (modem->messaging_generation == G_MAXUINT32) {
		modem->sms_cache_state = "unavailable";
		g_clear_object(&messaging);
		return;
	}
	modem->messaging_generation++;
	sms_revision_bump(modem);
	if (messaging == NULL) {
		modem->sms_cache_state = "absent";
		return;
	}
	modem->messaging = messaging;
	g_dbus_proxy_set_default_timeout(G_DBUS_PROXY(modem->messaging),
		MM_LONG_PROXY_TIMEOUT_MS);
	modem->messaging_added_handler = g_signal_connect(modem->messaging,
		"added", G_CALLBACK(sms_added), modem);
	modem->messaging_deleted_handler = g_signal_connect(modem->messaging,
		"deleted", G_CALLBACK(sms_deleted), modem);
	query_sms(modem);
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
		reconcile_messaging(existing, FALSE);
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
	modem->sms_by_path = g_hash_table_new_full(g_str_hash, g_str_equal,
		g_free, (GDestroyNotify)fibocom_sms_unref);
	modem->sms_reserved_ids = g_hash_table_new_full(g_str_hash, g_str_equal,
		g_free, NULL);
	modem->sms_deleted_paths = g_hash_table_new_full(g_str_hash, g_str_equal,
		g_free, NULL);
	modem->sms_dedupe = g_queue_new();
	modem->modem_id = g_steal_pointer(&modem_id);
	modem->generation = ++bridge->next_generation;
	modem->admitted_at = g_get_real_time() / G_USEC_PER_SEC;
	modem->last_changed_at = modem->admitted_at;
	modem->sim_cache_state = "loading";
	modem->bearer_cache_state = "loading";
	modem->sms_cache_state = "absent";
	modem->serving_cell_reason = "refresh-pending";
	g_hash_table_insert(bridge->modems_by_path, g_strdup(path), modem);
	query_sim(modem);
	query_bearers(modem);
	reconcile_messaging(modem, FALSE);
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

static gboolean
string_vector_has(const gchar *const *values, const gchar *key)
{
	guint index;

	if (values == NULL)
		return FALSE;
	for (index = 0; values[index] != NULL; index++) {
		if (g_str_equal(values[index], key))
			return TRUE;
	}
	return FALSE;
}

static gboolean
property_was_updated(GVariant *changed_properties,
		     const gchar *const *invalidated_properties,
		     const gchar *key)
{
	return variant_has_key(changed_properties, key) ||
		string_vector_has(invalidated_properties, key);
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
	gboolean sim_updated;
	gboolean primary_slot_updated;
	gboolean bearers_updated;

	(void)manager;
	reconcile_object(bridge, G_DBUS_OBJECT(object_proxy));
	path = g_dbus_object_get_object_path(G_DBUS_OBJECT(object_proxy));
	modem = g_hash_table_lookup(bridge->modems_by_path, path);
	if (modem == NULL)
		return;
	modem->last_changed_at = g_get_real_time() / G_USEC_PER_SEC;
	interface_name = g_dbus_proxy_get_interface_name(interface_proxy);
	if (!g_str_equal(interface_name, MM_DBUS_INTERFACE_MODEM))
		return;
	sim_updated = property_was_updated(changed_properties,
		invalidated_properties, "Sim");
	primary_slot_updated = property_was_updated(changed_properties,
		invalidated_properties, "PrimarySimSlot");
	bearers_updated = property_was_updated(changed_properties,
		invalidated_properties, "Bearers");
	if (sim_updated || primary_slot_updated)
		reconcile_messaging(modem, TRUE);
	if (sim_updated)
		query_sim(modem);
	if (bearers_updated)
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

static gboolean
sms_reconcile_cb(gpointer user_data)
{
	FibocomBridge *bridge = user_data;
	GHashTableIter iter;
	gpointer value;

	if (bridge->stopping) {
		bridge->sms_reconcile_source = 0;
		return G_SOURCE_REMOVE;
	}
	if (!manager_has_owner(bridge))
		return G_SOURCE_CONTINUE;
	reconcile_all(bridge);
	g_hash_table_iter_init(&iter, bridge->modems_by_path);
	while (g_hash_table_iter_next(&iter, NULL, &value)) {
		FibocomModem *modem = value;

		if (modem->live && modem->messaging != NULL)
			query_sms(modem);
	}
	return G_SOURCE_CONTINUE;
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

static void
clear_manager_connection(FibocomBridge *bridge)
{
	gulong closed_handler = bridge->connection_closed_handler;

	if (bridge->manager != NULL)
		g_signal_handlers_disconnect_by_data(bridge->manager, bridge);
	bridge->connection_closed_handler = 0;
	if (closed_handler != 0 && bridge->connection != NULL)
		g_signal_handler_disconnect(bridge->connection, closed_handler);
	clear_modems(bridge);
	g_clear_object(&bridge->manager);
	g_clear_object(&bridge->connection);
}

static void
connection_closed(GDBusConnection *connection, gboolean remote_peer_vanished,
		  GError *error, gpointer user_data)
{
	FibocomBridge *bridge = user_data;

	(void)remote_peer_vanished;
	(void)error;
	if (bridge->stopping || bridge->connection != connection)
		return;
	bridge_next_connect_serial(bridge);
	bridge->connect_pending = FALSE;
	clear_manager_connection(bridge);
	g_warning("system D-Bus connection closed; retry scheduled");
	schedule_retry(bridge);
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
	ManagerConnectContext *connect = user_data;
	FibocomBridge *bridge = connect->bridge;
	g_autoptr(GError) error = NULL;
	MMManager *manager;

	(void)source;
	manager = mm_manager_new_finish(result, &error);
	if (bridge->stopping || connect->serial != bridge->connect_serial ||
	    bridge->connection != connect->connection ||
	    g_dbus_connection_is_closed(connect->connection))
		goto out;
	if (manager == NULL) {
		bridge_next_connect_serial(bridge);
		clear_manager_connection(bridge);
		g_warning("ModemManager object manager unavailable; retry scheduled");
		schedule_retry(bridge);
		goto out;
	}
	bridge->manager = manager;
	manager = NULL;
	g_signal_connect(bridge->manager, "object-added", G_CALLBACK(object_added), bridge);
	g_signal_connect(bridge->manager, "object-removed", G_CALLBACK(object_removed), bridge);
	g_signal_connect(bridge->manager, "interface-added", G_CALLBACK(interface_added), bridge);
	g_signal_connect(bridge->manager, "interface-removed", G_CALLBACK(interface_removed), bridge);
	g_signal_connect(bridge->manager, "interface-proxy-properties-changed",
			 G_CALLBACK(properties_changed), bridge);
	g_signal_connect(bridge->manager, "notify::name-owner",
			 G_CALLBACK(name_owner_changed), bridge);
	reconcile_all(bridge);

out:
	g_clear_object(&manager);
	manager_connect_context_free(connect);
}

static void
bus_ready(GObject *source, GAsyncResult *result, gpointer user_data)
{
	BusConnectContext *connect = user_data;
	FibocomBridge *bridge = connect->bridge;
	g_autoptr(GError) error = NULL;
	GDBusConnection *connection;
	ManagerConnectContext *manager_connect;

	(void)source;
	connection = g_bus_get_finish(result, &error);
	if (connect->serial != bridge->connect_serial)
		goto out;
	bridge->connect_pending = FALSE;
	if (bridge->stopping)
		goto out;
	if (connection == NULL || g_dbus_connection_is_closed(connection)) {
		g_warning("system D-Bus unavailable; retry scheduled");
		schedule_retry(bridge);
		goto out;
	}
	bridge->connection = connection;
	connection = NULL;
	bridge->connection_closed_handler = g_signal_connect(bridge->connection,
		"closed", G_CALLBACK(connection_closed), bridge);
	manager_connect = manager_connect_context_new(bridge, bridge->connection,
		connect->serial);
	mm_manager_new(bridge->connection,
		       G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_DO_NOT_AUTO_START,
		       bridge->cancellable, manager_ready, manager_connect);

out:
	g_clear_object(&connection);
	bus_connect_context_free(connect);
}

static void
bridge_connect(FibocomBridge *bridge)
{
	guint64 serial;

	if (bridge->stopping || bridge->manager != NULL ||
	    bridge->connection != NULL || bridge->connect_pending)
		return;
	serial = bridge_next_connect_serial(bridge);
	bridge->connect_pending = TRUE;
	g_bus_get(G_BUS_TYPE_SYSTEM, bridge->cancellable, bus_ready,
		  bus_connect_context_new(bridge, serial));
}

FibocomBridge *
fibocom_bridge_new(void)
{
	FibocomBridge *bridge = g_new0(FibocomBridge, 1);

	bridge->refs = 1;
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
	bridge->sms_reconcile_source = g_timeout_add_seconds(
		SMS_RECONCILE_SECONDS, sms_reconcile_cb, bridge);
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
	if (bridge->sms_reconcile_source != 0) {
		g_source_remove(bridge->sms_reconcile_source);
		bridge->sms_reconcile_source = 0;
	}
	g_cancellable_cancel(bridge->cancellable);
	bridge_next_connect_serial(bridge);
	bridge->connect_pending = FALSE;
	clear_manager_connection(bridge);
}

void
fibocom_bridge_free(FibocomBridge *bridge)
{
	if (bridge == NULL)
		return;
	fibocom_bridge_stop(bridge);
	bridge_unref(bridge);
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
