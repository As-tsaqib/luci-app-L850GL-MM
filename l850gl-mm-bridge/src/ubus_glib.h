/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef L850GL_MM_UBUS_GLIB_H
#define L850GL_MM_UBUS_GLIB_H

#include "bridge.h"

typedef struct _L850GLUbus L850GLUbus;

L850GLUbus *l850gl_ubus_new(L850GLBridge *bridge, const gchar *socket_path);
void l850gl_ubus_start(L850GLUbus *ubus);
void l850gl_ubus_stop(L850GLUbus *ubus);
void l850gl_ubus_free(L850GLUbus *ubus);

#endif /* L850GL_MM_UBUS_GLIB_H */
