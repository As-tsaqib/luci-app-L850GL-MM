#!/bin/sh
# SPDX-FileCopyrightText: 2026 As Tsaqib
# SPDX-License-Identifier: GPL-2.0-or-later
# shellcheck disable=SC1091,SC2034,SC3043

[ -n "$INCLUDE_ONLY" ] || {
	. /lib/functions.sh
	. ../netifd-proto.sh
	init_proto "$@"
}

proto_fibocom_init_config() {
	available=1
	no_device=1

	proto_config_add_string device_id
	proto_config_add_string apn
	proto_config_add_string auth
	proto_config_add_string username
	proto_config_add_string password
	proto_config_add_string ip_family
	proto_config_add_boolean roaming
	proto_config_add_int mtu
	proto_config_add_defaults
}

proto_fibocom_setup() {
	local interface="$1"

	# P0 is inventory-only. Do not acquire a session, open a modem port,
	# configure a netdev, or attempt a fallback dial path here.
	proto_notify_error "$interface" SHADOW_MODE
	proto_block_restart "$interface"
	return 1
}

proto_fibocom_teardown() {
	local interface="$1"

	# No bearer can exist in P0 shadow mode. Still publish an explicit down
	# update so teardown is deterministic and idempotent.
	proto_init_update "*" 0
	proto_send_update "$interface"
	return 0
}

[ -n "$INCLUDE_ONLY" ] || add_protocol fibocom
