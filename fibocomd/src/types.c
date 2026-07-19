/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "fibocomd.h"

const gchar *
fibocom_topology_status_name(FibocomTopologyStatus status)
{
	switch (status) {
	case FIBOCOM_TOPOLOGY_COMPLETE:
		return "complete";
	case FIBOCOM_TOPOLOGY_AMBIGUOUS:
		return "ambiguous";
	case FIBOCOM_TOPOLOGY_PARTIAL:
	default:
		return "partial";
	}
}

void
fibocom_port_free(FibocomPort *port)
{
	if (port == NULL)
		return;
	g_free(port->name);
	g_free(port->interface_number);
	g_free(port->driver);
	g_free(port);
}

void
fibocom_interface_free(FibocomInterface *interface)
{
	if (interface == NULL)
		return;
	g_free(interface->number);
	g_free(interface->driver);
	g_free(interface->role);
	g_free(interface->sysfs_path);
	g_ptr_array_unref(interface->ttys);
	g_ptr_array_unref(interface->wdms);
	g_ptr_array_unref(interface->netdevs);
	g_free(interface);
}

void
fibocom_device_free(FibocomDevice *device)
{
	if (device == NULL)
		return;
	g_free(device->device_id);
	g_free(device->physical_path);
	g_free(device->identity_scope);
	g_free(device->vid);
	g_free(device->pid);
	g_free(device->composition);
	g_free(device->fingerprint);
	g_free(device->topology_reason);
	g_ptr_array_unref(device->interfaces);
	g_free(device);
}
