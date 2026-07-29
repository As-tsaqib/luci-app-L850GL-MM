/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef L850GL_MM_BRIDGE_H
#define L850GL_MM_BRIDGE_H

#include <glib.h>
#include <libmm-glib.h>

#define L850GL_MM_API_SCHEMA 4U
#define L850GL_MM_BRIDGE_VERSION "1.0.0-alpha"
#define L850GL_SMS_REQUEST_DIGEST_LEN 32U
typedef struct _L850GLBridge L850GLBridge;
typedef struct _L850GLModem L850GLModem;
typedef struct _L850GLSms L850GLSms;
typedef struct _L850GLSmsDedupe L850GLSmsDedupe;

typedef enum {
	L850GL_MUTATION_NONE = 0,
	L850GL_MUTATION_SMS,
	L850GL_MUTATION_ADVANCED,
	L850GL_MUTATION_L850,
} L850GLMutationKind;

struct _L850GLSms {
	gint refs;
	L850GLModem *owner;
	MMSms *sms;
	gchar *sms_id;
	guint32 modem_generation;
	guint32 messaging_generation;
	gulong properties_handler;
};

struct _L850GLSmsDedupe {
	gchar *client_token;
	gchar *sms_id;
	gchar *state;
	gchar *error_code;
	gboolean ok;
	gboolean retryable;
	gboolean has_request_digest;
	guint8 request_digest[L850GL_SMS_REQUEST_DIGEST_LEN];
	gint64 expires_at;
};

struct _L850GLModem {
	gint refs;
	gboolean live;
	MMObject *object;
	MMModem *modem;
	GCancellable *cancellable;
	MMSim *sim;
	GList *bearers;
	MMModemMessaging *messaging;
	GHashTable *sms_by_path;
	GHashTable *sms_reserved_ids;
	GHashTable *sms_deleted_paths;
	GQueue *sms_dedupe;
	gchar *modem_id;
	guint32 generation;
	guint32 messaging_generation;
	guint64 sms_revision;
	gint64 admitted_at;
	gint64 last_changed_at;
	gboolean sim_query_pending;
	gboolean bearer_query_pending;
	gboolean sim_query_dirty;
	gboolean bearer_query_dirty;
	gboolean sms_query_pending;
	gboolean sms_query_dirty;
	gboolean sms_cache_truncated;
	gboolean serving_cell_refresh_pending;
	gboolean serving_cell_valid;
	gboolean serving_cell_has_rsrp;
	gboolean serving_cell_has_rsrq;
	guint32 serving_cell_generation;
	guint32 serving_cell_earfcn;
	guint16 serving_cell_pci;
	guint16 serving_cell_band;
	gdouble serving_cell_rsrp;
	gdouble serving_cell_rsrq;
	gint64 serving_cell_updated_at;
	gint64 serving_cell_last_attempt_at;
	const gchar *serving_cell_reason;
	gboolean mutation_busy;
	L850GLMutationKind mutation_kind;
	GCancellable *mutation_cancellable;
	gint64 advanced_cooldown_until;
#ifdef L850GL_MM_EXPERT
	gboolean l850_voltage_refresh_pending;
	gboolean l850_voltage_valid;
	guint32 l850_voltage_generation;
	guint32 l850_voltage_mv;
	gint64 l850_voltage_updated_at;
	gint64 l850_voltage_last_attempt_at;
	gint64 l850_last_scan_completed_at;
	gint64 l850_last_carrier_query_completed_at;
#endif
	gulong messaging_added_handler;
	gulong messaging_deleted_handler;
	const gchar *sim_cache_state;
	const gchar *bearer_cache_state;
	const gchar *sms_cache_state;
};

struct _L850GLBridge {
	gint refs;
	GCancellable *cancellable;
	GDBusConnection *connection;
	MMManager *manager;
	GHashTable *modems_by_path;
	guint32 next_generation;
	guint64 connect_serial;
	guint retry_source;
	guint sms_reconcile_source;
	gulong connection_closed_handler;
	gboolean connect_pending;
	gboolean started;
	gboolean stopping;
	gboolean random_failed;
};

L850GLBridge *l850gl_bridge_new(void);
void l850gl_bridge_start(L850GLBridge *bridge);
void l850gl_bridge_stop(L850GLBridge *bridge);
void l850gl_bridge_free(L850GLBridge *bridge);

L850GLModem *l850gl_bridge_find_modem(L850GLBridge *bridge,
					const gchar *modem_id);
GPtrArray *l850gl_bridge_snapshot_modems(L850GLBridge *bridge);
gboolean l850gl_bridge_manager_available(L850GLBridge *bridge);
const gchar *l850gl_bridge_manager_version(L850GLBridge *bridge);

L850GLModem *l850gl_modem_ref(L850GLModem *modem);
void l850gl_modem_unref(L850GLModem *modem);
const gchar *l850gl_modem_composition(L850GLModem *modem);
gboolean l850gl_modem_is_l850(L850GLModem *modem);
gboolean l850gl_modem_is_supported(L850GLModem *modem);
const gchar *l850gl_modem_support_reason(L850GLModem *modem);
gboolean l850gl_modem_attest_mutation_target(L850GLModem *modem);

L850GLSms *l850gl_sms_ref(L850GLSms *sms);
void l850gl_sms_unref(L850GLSms *sms);
GPtrArray *l850gl_modem_snapshot_sms(L850GLModem *modem);
L850GLSms *l850gl_modem_find_sms(L850GLModem *modem,
				    const gchar *sms_id);
L850GLSms *l850gl_modem_admit_sms(L850GLModem *modem, MMSms *sms);
L850GLSms *l850gl_modem_admit_reserved_sms(L850GLModem *modem,
					      MMSms *sms,
					      const gchar *sms_id);
gchar *l850gl_modem_reserve_sms_id(L850GLModem *modem);
void l850gl_modem_release_sms_id(L850GLModem *modem,
				  const gchar *sms_id);
void l850gl_modem_refresh_sms(L850GLModem *modem);

#endif /* L850GL_MM_BRIDGE_H */
