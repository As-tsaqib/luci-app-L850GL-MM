/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef FIBOCOM_DISCOVERY_H
#define FIBOCOM_DISCOVERY_H

#include "profile.h"

typedef struct _FibocomDiscovery FibocomDiscovery;

typedef void (*FibocomDiscoveryChangedFunc)(FibocomDiscovery *discovery,
					     const FibocomReconcile *reconcile,
					     gpointer user_data);

FibocomDiscovery *fibocom_discovery_new(const gchar *sysfs_root,
					FibocomProfile *profile);
FibocomDiscovery *fibocom_discovery_ref(FibocomDiscovery *discovery);
void fibocom_discovery_unref(FibocomDiscovery *discovery);

void fibocom_discovery_set_changed_callback(FibocomDiscovery *discovery,
					    FibocomDiscoveryChangedFunc callback,
					    gpointer user_data);
void fibocom_discovery_start(FibocomDiscovery *discovery);
void fibocom_discovery_stop(FibocomDiscovery *discovery);

/* Schedules a debounced, worker-thread sysfs scan and returns its request ID. */
guint64 fibocom_discovery_request_scan(FibocomDiscovery *discovery,
				       const gchar *reason);

/* The following cached views are main-context only and remain owned by discovery. */
const GPtrArray *fibocom_discovery_devices(const FibocomDiscovery *discovery);
const FibocomDevice *fibocom_discovery_find(const FibocomDiscovery *discovery,
					    const gchar *device_id);
const FibocomReconcile *fibocom_discovery_reconcile(const FibocomDiscovery *discovery);
gboolean fibocom_discovery_scan_in_progress(const FibocomDiscovery *discovery);
gboolean fibocom_discovery_scan_pending(const FibocomDiscovery *discovery);
const FibocomProfile *fibocom_discovery_profile(const FibocomDiscovery *discovery);

#endif /* FIBOCOM_DISCOVERY_H */
