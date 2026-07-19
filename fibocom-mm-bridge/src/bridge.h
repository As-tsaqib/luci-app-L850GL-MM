/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef FIBOCOM_MM_BRIDGE_H
#define FIBOCOM_MM_BRIDGE_H

#include <glib.h>
#include <libmm-glib.h>

#define FIBOCOM_MM_API_SCHEMA 1U
#define FIBOCOM_MM_BRIDGE_VERSION "0.1.0-p0"
typedef struct _FibocomBridge FibocomBridge;
typedef struct _FibocomModem FibocomModem;

struct _FibocomModem {
	gint refs;
	gboolean live;
	MMObject *object;
	MMModem *modem;
	GCancellable *cancellable;
	MMSim *sim;
	GList *bearers;
	gchar *modem_id;
	guint32 generation;
	gint64 admitted_at;
	gint64 last_changed_at;
	gboolean sim_query_pending;
	gboolean bearer_query_pending;
	gboolean sim_query_dirty;
	gboolean bearer_query_dirty;
	const gchar *sim_cache_state;
	const gchar *bearer_cache_state;
};

struct _FibocomBridge {
	GCancellable *cancellable;
	GDBusConnection *connection;
	MMManager *manager;
	GHashTable *modems_by_path;
	guint32 next_generation;
	guint retry_source;
	gboolean started;
	gboolean stopping;
	gboolean random_failed;
};

FibocomBridge *fibocom_bridge_new(void);
void fibocom_bridge_start(FibocomBridge *bridge);
void fibocom_bridge_stop(FibocomBridge *bridge);
void fibocom_bridge_free(FibocomBridge *bridge);

FibocomModem *fibocom_bridge_find_modem(FibocomBridge *bridge,
					const gchar *modem_id);
GPtrArray *fibocom_bridge_snapshot_modems(FibocomBridge *bridge);
gboolean fibocom_bridge_manager_available(FibocomBridge *bridge);
const gchar *fibocom_bridge_manager_version(FibocomBridge *bridge);

FibocomModem *fibocom_modem_ref(FibocomModem *modem);
void fibocom_modem_unref(FibocomModem *modem);
const gchar *fibocom_modem_composition(FibocomModem *modem);
gboolean fibocom_modem_is_l850(FibocomModem *modem);
gboolean fibocom_modem_is_supported(FibocomModem *modem);
const gchar *fibocom_modem_support_reason(FibocomModem *modem);

#endif /* FIBOCOM_MM_BRIDGE_H */
