/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef FIBOCOMD_H
#define FIBOCOMD_H

#include <gio/gio.h>
#include <glib.h>

#define FIBOCOM_API_SCHEMA 1U
#define FIBOCOM_PROFILE_ID "fibocom-l850-gl"
#define FIBOCOM_PROFILE_PATH "/usr/share/fibocom/profiles/l850-gl.json"
#define FIBOCOM_SYSFS_ROOT "/sys"

typedef enum {
	FIBOCOM_COMPOSITION_MBIM,
	FIBOCOM_COMPOSITION_NCM,
} FibocomComposition;

typedef enum {
	FIBOCOM_TOPOLOGY_COMPLETE,
	FIBOCOM_TOPOLOGY_PARTIAL,
	FIBOCOM_TOPOLOGY_AMBIGUOUS,
} FibocomTopologyStatus;

typedef struct {
	gchar *name;
	gchar *interface_number;
	guint interface_index;
	gchar *driver;
} FibocomPort;

typedef struct {
	gchar *number;
	guint index;
	gchar *driver;
	gchar *role;
	gchar *sysfs_path; /* private canonical path; never serialized */
	GPtrArray *ttys;     /* FibocomPort */
	GPtrArray *wdms;     /* FibocomPort */
	GPtrArray *netdevs;  /* FibocomPort */
} FibocomInterface;

typedef struct {
	gchar *device_id;
	gchar *physical_path;
	gchar *identity_scope;
	gchar *vid;
	gchar *pid;
	gchar *composition;
	gchar *fingerprint;
	guint64 generation;
	gboolean present;
	FibocomTopologyStatus topology_status;
	gchar *topology_reason;
	GPtrArray *interfaces; /* FibocomInterface */
} FibocomDevice;

typedef struct {
	guint64 scan_id;
	gint64 completed_at;
	guint device_count;
	guint added;
	guint removed;
	guint changed;
	guint unchanged;
	gchar *last_error;
} FibocomReconcile;

const gchar *fibocom_topology_status_name(FibocomTopologyStatus status);
void fibocom_port_free(FibocomPort *port);
void fibocom_interface_free(FibocomInterface *interface);
void fibocom_device_free(FibocomDevice *device);

#endif /* FIBOCOMD_H */
