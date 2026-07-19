/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef FIBOCOM_PROFILE_H
#define FIBOCOM_PROFILE_H

#include "fibocomd.h"

typedef struct {
	gchar vid[5];
	gchar pid[5];
	FibocomComposition composition;
} FibocomUsbMatch;

typedef struct {
	guint at_primary;
	gboolean has_at_secondary;
	guint at_secondary;
	GArray *ignored;         /* guint */
	GArray *data_candidates; /* guint */
} FibocomPortMap;

typedef struct _FibocomProfile FibocomProfile;

FibocomProfile *fibocom_profile_load(const gchar *path, GError **error);
FibocomProfile *fibocom_profile_ref(FibocomProfile *profile);
void fibocom_profile_unref(FibocomProfile *profile);

const gchar *fibocom_profile_id(const FibocomProfile *profile);
const gchar *fibocom_profile_display_name(const FibocomProfile *profile);
const FibocomUsbMatch *fibocom_profile_match_usb(const FibocomProfile *profile,
						 const gchar *vid,
						 const gchar *pid);
const FibocomPortMap *fibocom_profile_port_map(const FibocomProfile *profile,
					       FibocomComposition composition);

#endif /* FIBOCOM_PROFILE_H */
