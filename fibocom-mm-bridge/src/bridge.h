/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef FIBOCOM_MM_BRIDGE_H
#define FIBOCOM_MM_BRIDGE_H

#include <glib.h>
#include <libmm-glib.h>

#define FIBOCOM_MM_API_SCHEMA 1U
#define FIBOCOM_MM_BRIDGE_VERSION "0.2.0"
#define FIBOCOM_SMS_REQUEST_DIGEST_LEN 32U
typedef struct _FibocomBridge FibocomBridge;
typedef struct _FibocomModem FibocomModem;
typedef struct _FibocomSms FibocomSms;
typedef struct _FibocomSmsDedupe FibocomSmsDedupe;

typedef enum {
	FIBOCOM_MUTATION_NONE = 0,
	FIBOCOM_MUTATION_SMS,
	FIBOCOM_MUTATION_ADVANCED,
} FibocomMutationKind;

struct _FibocomSms {
	gint refs;
	FibocomModem *owner;
	MMSms *sms;
	gchar *sms_id;
	guint32 modem_generation;
	guint32 messaging_generation;
	gulong properties_handler;
};

struct _FibocomSmsDedupe {
	gchar *client_token;
	gchar *sms_id;
	gchar *state;
	gchar *error_code;
	gboolean ok;
	gboolean retryable;
	gboolean has_request_digest;
	guint8 request_digest[FIBOCOM_SMS_REQUEST_DIGEST_LEN];
	gint64 expires_at;
};

struct _FibocomModem {
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
	gboolean mutation_busy;
	FibocomMutationKind mutation_kind;
	GCancellable *mutation_cancellable;
	gint64 advanced_cooldown_until;
	gulong messaging_added_handler;
	gulong messaging_deleted_handler;
	const gchar *sim_cache_state;
	const gchar *bearer_cache_state;
	const gchar *sms_cache_state;
};

struct _FibocomBridge {
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
gboolean fibocom_modem_attest_mutation_target(FibocomModem *modem);

FibocomSms *fibocom_sms_ref(FibocomSms *sms);
void fibocom_sms_unref(FibocomSms *sms);
GPtrArray *fibocom_modem_snapshot_sms(FibocomModem *modem);
FibocomSms *fibocom_modem_find_sms(FibocomModem *modem,
				    const gchar *sms_id);
FibocomSms *fibocom_modem_admit_sms(FibocomModem *modem, MMSms *sms);
FibocomSms *fibocom_modem_admit_reserved_sms(FibocomModem *modem,
					      MMSms *sms,
					      const gchar *sms_id);
gchar *fibocom_modem_reserve_sms_id(FibocomModem *modem);
void fibocom_modem_release_sms_id(FibocomModem *modem,
				  const gchar *sms_id);
void fibocom_modem_refresh_sms(FibocomModem *modem);

#endif /* FIBOCOM_MM_BRIDGE_H */
