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

function filesUnder(relativePath) {
	const absolute = path.join(root, relativePath);

	if (!fs.existsSync(absolute))
		return [];

	return fs.readdirSync(absolute, { withFileTypes: true }).flatMap(function(entry) {
		const child = path.join(relativePath, entry.name);

		return entry.isDirectory() ? filesUnder(child) : [ child ];
	});
}

for (const legacy of [ 'fibocomd', 'fibocom-netifd', 'luci-proto-fibocom', 'schemas' ])
	assert.deepStrictEqual(filesUnder(legacy), [], `${legacy} must remain retired to Git history`);

const bridgeMakefile = read('fibocom-mm-bridge/Makefile');
for (const dependency of [ 'modemmanager', 'glib2', 'libubox', 'libubus' ])
	assert.match(bridgeMakefile, new RegExp(`\\+${dependency.replaceAll('-', '\\-')}\\b`));
for (const forbidden of [
	'ubus', 'libjson-c', 'jshn', 'libmbim', 'mbim-utils', 'sms-tool', 'lpac',
	'kmod-usb-net-cdc-ncm'
]) {
	assert.doesNotMatch(bridgeMakefile,
		new RegExp(`\\+${forbidden.replaceAll('-', '\\-')}\\b`),
		`bridge must not depend on ${forbidden}`);
}
assert.match(bridgeMakefile, /PKG_BUILD_DEPENDS:=modemmanager\b/);
assert.match(bridgeMakefile, /PKG_LICENSE:=GPL-2\.0-or-later/);

const sourceFiles = filesUnder('fibocom-mm-bridge/src').filter(function(file) {
	return /\.[ch]$/.test(file);
});
const bridgeSource = sourceFiles.map(read).join('\n');
for (const method of [ 'list_modems', 'get_overview', 'get_status', 'get_capabilities' ])
	assert.match(bridgeSource, new RegExp(`UBUS_METHOD(?:_NOARG)?\\("${method}"`));
assert.strictEqual((bridgeSource.match(/UBUS_METHOD(?:_NOARG)?\(/g) || []).length, 4,
	'P0 bridge must expose exactly four read methods');

for (const forbidden of [
	/mm_modem_simple_connect\s*\(/,
	/mm_modem_create_bearer\s*\(/,
	/mm_bearer_(?:connect|disconnect)\s*\(/,
	/mm_modem_delete_bearer\s*\(/,
	/mm_modem_command\s*\(/,
	/mm_modem_reset\s*\(/,
	/mm_manager_(?:scan_devices|report_kernel_event|inhibit_device)\s*\(/,
	/\b(?:system|popen|fork|execv|execl)\s*\(/,
	/['"]\/dev\//
])
	assert.doesNotMatch(bridgeSource, forbidden, `forbidden P0 operation found: ${forbidden}`);

assert.match(bridgeSource, /SYS_getrandom/);
assert.match(bridgeSource, /FIBOCOM_ID_RANDOM_LEN\s+16U/);
assert.match(bridgeSource, /g_cancellable_cancel\(modem->cancellable\)/);
assert.match(bridgeSource, /G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_DO_NOT_AUTO_START/);
assert.doesNotMatch(bridgeSource, /mm_modem_get_device_identifier\s*\(/);
assert.doesNotMatch(bridgeSource, /mm_modem_get_physdev\s*\(/);

const init = read('fibocom-mm-bridge/files/etc/init.d/fibocom-mm-bridge');
assert.match(init, /^USE_PROCD=1$/m);
assert.match(init, /^START=75$/m);
assert.match(init, /command "\$PROG" --foreground/);

const luciMakefile = read('luci-app-fibocom/Makefile');
for (const dependency of [
	'@MODEMMANAGER_WITH_MBIM',
	'@MODEMMANAGER_WITH_NETIFD',
	'+luci-base',
	'+fibocom-mm-bridge',
	'+modemmanager',
	'+luci-proto-modemmanager',
	'+kmod-usb-acm',
	'+kmod-usb-net-cdc-mbim',
	'+kmod-usb-wdm'
])
	assert.ok(luciMakefile.includes(dependency), `LuCI Makefile must include ${dependency}`);
for (const forbidden of [
	'fibocomd', 'luci-proto-fibocom', 'modemmanager-plugin-fibocom',
	'sms-tool', '+lpac'
])
	assert.ok(!luciMakefile.includes(forbidden), `base LuCI must not include ${forbidden}`);

const acl = JSON.parse(read(
	'luci-app-fibocom/root/usr/share/rpcd/acl.d/luci-app-fibocom.json'
));
assert.deepStrictEqual(acl['luci-app-fibocom'].read.ubus['fibocom.mm'].slice().sort(), [
	'get_capabilities', 'get_overview', 'get_status', 'list_modems'
]);
assert.strictEqual(acl['luci-app-fibocom'].write, undefined);

const esimMakefile = read('luci-app-fibocom-esim/Makefile');
for (const dependency of [
	'@LPAC_WITH_MBIM', '@MODEMMANAGER_WITH_MBIM',
	'+luci-app-fibocom', '+luci-app-lpac'
])
	assert.ok(esimMakefile.includes(dependency), `optional eSIM package needs ${dependency}`);

const esimMenu = JSON.parse(read(
	'luci-app-fibocom-esim/root/usr/share/luci/menu.d/luci-app-fibocom-esim.json'
));
assert.deepStrictEqual(esimMenu['admin/modem/fibocom/esim'], {
	title: 'eSIM',
	order: 60,
	action: { type: 'alias', path: 'admin/modem/lpac' },
	depends: { acl: [ 'luci-app-lpac' ] },
	wildcard: true,
	firstchild_ineligible: true
});

const liveFixture = JSON.parse(read('tests/fixtures/live/l850-mbim-connected.json'));
assert.strictEqual(liveFixture.hardware.model, 'L850-GL');
assert.strictEqual(liveFixture.hardware.composition, 'mbim');
assert.strictEqual(liveFixture.modemmanager.state, 'connected');
assert.strictEqual(liveFixture.openwrt.protocol, 'modemmanager');
assert.ok(Object.values(liveFixture.privacy).every(function(value) {
	return value === 'redacted';
}), 'live fixture privacy fields must remain redacted');

console.log('package contract validation passed');
