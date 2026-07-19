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
for (const dependency of [ 'modemmanager', 'glib2', 'libubox', 'libubus', 'libuci' ])
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
const packageVersion = bridgeMakefile.match(/^PKG_VERSION:=(\S+)$/m);
assert.ok(packageVersion, 'bridge package version must be declared');

const sourceFiles = filesUnder('fibocom-mm-bridge/src').filter(function(file) {
	return /\.[ch]$/.test(file);
});
const bridgeSource = sourceFiles.map(read).join('\n');
const bridgeInventorySource = read('fibocom-mm-bridge/src/bridge.c');
const ubusSource = read('fibocom-mm-bridge/src/ubus_glib.c');
assert.match(bridgeSource, new RegExp(
	`FIBOCOM_MM_BRIDGE_VERSION "${packageVersion[1].replaceAll('.', '\\.')}"`
), 'binary and package versions must match exactly');
for (const method of [
	'list_modems', 'get_overview', 'get_status', 'get_capabilities',
	'list_sms', 'send_sms', 'delete_sms',
	'set_bands', 'set_radio', 'reset', 'set_primary_sim_slot'
])
	assert.match(bridgeSource, new RegExp(`UBUS_METHOD(?:_NOARG)?\\("${method}"`));
assert.strictEqual((bridgeSource.match(/UBUS_METHOD(?:_NOARG)?\(/g) || []).length, 11,
	'bridge must expose only the reviewed read, SMS, and standard-radio methods');

for (const forbidden of [
	/mm_modem_simple_connect\s*\(/,
	/mm_modem_create_bearer\s*\(/,
	/mm_bearer_(?:connect|disconnect)\s*\(/,
	/mm_modem_delete_bearer\s*\(/,
	/mm_modem_command\s*\(/,
	/mm_modem_set_current_modes\s*\(/,
	/mm_manager_(?:scan_devices|report_kernel_event|inhibit_device)\s*\(/,
	/\buci_(?:add|commit|delete|rename|reorder|save|set)\s*\(/,
	/\b(?:system|popen|fork|execv|execl)\s*\(/,
	/['"]\/dev\//
])
	assert.doesNotMatch(bridgeSource, forbidden, `forbidden bridge operation found: ${forbidden}`);

assert.match(bridgeSource, /SYS_getrandom/);
assert.match(bridgeSource, /FIBOCOM_ID_RANDOM_LEN\s+16U/);
assert.match(bridgeSource, /g_cancellable_cancel\(modem->cancellable\)/);
assert.match(bridgeSource, /G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_DO_NOT_AUTO_START/);
assert.doesNotMatch(bridgeSource, /mm_modem_get_device_identifier\s*\(/);
assert.match(bridgeInventorySource,
	/messages\s*=\s*g_list_sort\s*\(\s*messages\s*,\s*sms_proxy_newest_first\s*\)/,
	'SMS truncation must retain the newest ModemManager entries first');
assert.match(bridgeInventorySource,
	/property_was_updated\s*\(\s*changed_properties\s*,\s*invalidated_properties/,
	'SIM and bearer cache epochs must handle invalidated D-Bus properties');
assert.match(bridgeInventorySource, /G_CALLBACK\(connection_closed\)/,
	'the bridge must recover when its system D-Bus connection closes');
assert.match(bridgeInventorySource, /connect->serial\s*!=\s*bridge->connect_serial/,
	'stale asynchronous reconnect attempts must be rejected');
assert.match(bridgeInventorySource, /physdev\s*=\s*mm_modem_get_physdev\s*\(/);
assert.match(bridgeInventorySource,
	/fibocom_hardware_attest_l850_mbim\s*\(\s*physdev\s*\)/,
	'standard mutations must fail closed on the exact live USB hardware');
assert.match(ubusSource, /fibocom_modem_attest_mutation_target\s*\(/,
	'ubus mutations must invoke the reviewed hardware-attestation gate');
assert.match(ubusSource, /fibocom_network_binding_lookup\s*\(/,
	'status and direct-radio ownership must use the reviewed libuci lookup');
assert.match(ubusSource,
	/mutation_kind\s*=\s*FIBOCOM_MUTATION_ADVANCED/,
	'Advanced must use the same per-modem mutation lock as SMS');
assert.match(ubusSource, /advanced_operation_stale_code\s*\(/,
	'Advanced callbacks must revalidate liveness and generation');
assert.match(ubusSource, /advanced_operation_outcome_is_unknown\s*\(/,
	'Advanced callbacks must prioritize unknown outcomes after dispatch');
assert.match(ubusSource, /operation->dispatched\s*=\s*TRUE/,
	'Advanced mutations must record that their side effect was dispatched');
assert.match(ubusSource, /advanced_cooldown_until/,
	'Advanced mutations must enforce a bounded cooldown');
assert.match(ubusSource, /\.Message\.MemoryFull/,
	'SMS storage-full errors must use the ModemManager Message namespace');
assert.match(ubusSource, /MM_MESSAGE_ERROR_MEMORY_FULL/,
	'SMS storage-full handling must cover registered libmm-glib error domains');
assert.match(ubusSource, /\.Message\.NetworkTimeout/,
	'SMS network timeouts must participate in outcome-unknown handling');
assert.match(ubusSource, /MM_MESSAGE_ERROR_NETWORK_TIMEOUT/,
	'SMS timeout handling must cover registered libmm-glib error domains');
assert.match(ubusSource, /#define SMS_DEDUPE_SECONDS 300U/,
	'the documented in-memory SMS retry window must remain five minutes');
assert.match(ubusSource,
	/sms_operation_complete_send\s*\(\s*operation\s*,\s*FALSE\s*\)/,
	'a known successful SMS send must remain successful without polluting a replacement epoch');
assert.match(ubusSource,
	/if\s*\(stale\s*==\s*NULL\)\s*fibocom_modem_refresh_sms\s*\(\s*operation->modem\s*\);[\s\S]*?sms_operation_complete_delete\s*\(\s*operation\s*\)/,
	'a known successful SMS delete must remain successful across an epoch change');
assert.doesNotMatch(bridgeSource, /['"](?:AT|at)[+@]/,
	'base bridge must not contain raw AT commands');

const init = read('fibocom-mm-bridge/files/etc/init.d/fibocom-mm-bridge');
assert.match(init, /^USE_PROCD=1$/m);
assert.match(init, /^START=75$/m);
assert.match(init, /command "\$PROG" --foreground/);

const luciMakefile = read('luci-app-fibocom/Makefile');
assert.match(luciMakefile,
	/^LUCI_URL:=https:\/\/github\.com\/As-tsaqib\/luci-app-fibocom$/m);
assert.match(luciMakefile, /^LUCI_MAINTAINER:=As Tsaqib <[^>]+>$/m);
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
assert.deepStrictEqual(acl['luci-app-fibocom-status'].read.ubus['fibocom.mm'].slice().sort(), [
	'get_capabilities', 'get_overview', 'get_status', 'list_modems'
]);
assert.deepStrictEqual(acl['luci-app-fibocom-sms-read'].read.ubus['fibocom.mm'], [
	'list_sms'
]);
assert.deepStrictEqual(acl['luci-app-fibocom-sms-write'].write.ubus['fibocom.mm'].slice().sort(), [
	'delete_sms', 'send_sms'
]);
assert.deepStrictEqual(acl['luci-app-fibocom-radio'].write.ubus['fibocom.mm'].slice().sort(), [
	'reset', 'set_bands', 'set_primary_sim_slot', 'set_radio'
]);

const esimMakefile = read('luci-app-fibocom-esim/Makefile');
assert.match(esimMakefile,
	/^LUCI_URL:=https:\/\/github\.com\/As-tsaqib\/luci-app-fibocom$/m);
assert.match(esimMakefile, /^LUCI_MAINTAINER:=As Tsaqib <[^>]+>$/m);
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

const staticWorkflow = read('.github/workflows/static.yml');
assert.match(staticWorkflow, /74f6277aabffc943d026f406df57c22595134c42/,
	'CI must pin the libuci source used by the behavioral host test');
const sdkWorkflow = read('.github/workflows/openwrt-sdk.yml');
assert.ok(sdkWorkflow.includes("grep -Fq -- '-Dbuiltin_plugins=true'"),
	'SDK CI must reject a ModemManager recipe without builtin plugins');
for (const archive of [ 'libmm-plugin-fibocom.a', 'libmm-shared-xmm.a' ])
	assert.ok(sdkWorkflow.includes(archive), `SDK CI must prove ${archive} was built`);

const liveFixture = JSON.parse(read('tests/fixtures/live/l850-mbim-connected.json'));
assert.strictEqual(liveFixture.hardware.model, 'L850-GL');
assert.strictEqual(liveFixture.hardware.composition, 'mbim');
assert.strictEqual(liveFixture.modemmanager.state, 'connected');
assert.strictEqual(liveFixture.openwrt.protocol, 'modemmanager');
assert.ok(Object.values(liveFixture.privacy).every(function(value) {
	return value === 'redacted';
}), 'live fixture privacy fields must remain redacted');

console.log('package contract validation passed');
