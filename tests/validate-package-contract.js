// SPDX-FileCopyrightText: 2026 As Tsaqib
// SPDX-License-Identifier: Apache-2.0

'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');

function read(relativePath) {
	return fs.readFileSync(path.join(root, relativePath), 'utf8');
}

const daemonMakefile = read('fibocomd/Makefile');
for (const dependency of [
	'glib2',
	'libjson-c',
	'libubox',
	'libubus',
	'jshn',
	'ubus',
	'kmod-usb-acm',
	'kmod-usb-net-cdc-mbim',
	'kmod-usb-net-cdc-ncm',
	'kmod-usb-wdm'
])
	assert.match(daemonMakefile, new RegExp(`\\+${dependency.replaceAll('-', '\\-')}\\b`));

for (const prematureDependency of [ 'libmbim', 'mbim-utils', 'libuci' ])
	assert.doesNotMatch(daemonMakefile,
		new RegExp(`\\+${prematureDependency.replaceAll('-', '\\-')}\\b`));

const init = read('fibocomd/files/etc/init.d/fibocomd');
assert.match(init, /command "\$PROG" --foreground --shadow/);

const ubusSource = read('fibocomd/src/ubus_glib.c');
assert.match(ubusSource, /list does not accept arguments/);
assert.match(ubusSource, /ubus_shutdown\(&ubus->context\)/);
assert.match(ubusSource, /ubus->context_initialized = FALSE/);

const deviceConfig = read('fibocomd/files/etc/config/fibocom');
assert.doesNotMatch(deviceConfig, /^\s*option\s+/m,
	'P0 must not expose configuration options that the daemon does not consume');

const netifd = read('fibocom-netifd/files/lib/netifd/proto/fibocom.sh');
assert.match(netifd, /proto_config_add_string device_id/);
assert.doesNotMatch(netifd, /proto_config_add_string modem/);
assert.match(netifd, /proto_notify_error "\$interface" SHADOW_MODE/);
assert.match(netifd, /proto_block_restart "\$interface"/);
assert.doesNotMatch(netifd, /\b(?:ubus call|mbimcli|uqmi|atinout|chat|ifconfig)\b/);

const protocol = read(
	'luci-proto-fibocom/htdocs/luci-static/resources/protocol/fibocom.js'
);
assert.match(protocol, /option\.ucioption = 'device_id'/);
assert.match(protocol, /uci\.get\('network', sectionId, 'device_id'\)/);
assert.doesNotMatch(protocol, /ucioption = 'modem'/);

const protoAcl = JSON.parse(read(
	'luci-proto-fibocom/root/usr/share/rpcd/acl.d/luci-proto-fibocom.json'
));
assert.deepStrictEqual(protoAcl, {
	'luci-proto-fibocom': {
		description: 'Allow read-only Fibocom modem selection',
		read: { ubus: { fibocom: [ 'list' ] } }
	}
});

for (const subsystem of [ 'usb', 'tty', 'net' ]) {
	const hotplug = read(`fibocomd/files/etc/hotplug.d/${subsystem}/25-fibocom`);

	assert.match(hotplug, new RegExp(`json_add_string subsystem ${subsystem}`));
	assert.match(hotplug, /ubus call fibocom rescan/);
	assert.doesNotMatch(hotplug, /\b(?:sleep|kill|reset|mbimcli|uqmi|atinout)\b/);
}

console.log('package contract validation passed');
