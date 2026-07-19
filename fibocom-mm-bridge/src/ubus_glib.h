/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef FIBOCOM_MM_UBUS_GLIB_H
#define FIBOCOM_MM_UBUS_GLIB_H

#include "bridge.h"

typedef struct _FibocomUbus FibocomUbus;

FibocomUbus *fibocom_ubus_new(FibocomBridge *bridge, const gchar *socket_path);
void fibocom_ubus_start(FibocomUbus *ubus);
void fibocom_ubus_stop(FibocomUbus *ubus);
void fibocom_ubus_free(FibocomUbus *ubus);

#endif /* FIBOCOM_MM_UBUS_GLIB_H */
