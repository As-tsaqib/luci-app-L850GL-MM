// SPDX-FileCopyrightText: 2026 As Tsaqib
// SPDX-License-Identifier: Apache-2.0

'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');

function absolute(relativePath) {
	return path.join(root, relativePath);
}

function read(relativePath) {
	return fs.readFileSync(absolute(relativePath), 'utf8');
}

function filesUnder(relativePath) {
	const directory = absolute(relativePath);

	if (!fs.existsSync(directory))
		return [];

	return fs.readdirSync(directory, { withFileTypes: true }).flatMap(function(entry) {
		const child = path.join(relativePath, entry.name);

		return entry.isDirectory() ? filesUnder(child) : [ child ];
	});
}

for (const retired of [
	'fibocomd', 'fibocom-netifd', 'luci-proto-fibocom', 'schemas',
	'luci-app-fibocom-esim'
]) {
	assert.deepStrictEqual(filesUnder(retired), [],
		`${retired} must remain retired from the 0.4 product tree`);
}

const bridgeMakefile = read('fibocom-mm-bridge/Makefile');
assert.match(bridgeMakefile, /^PKG_VERSION:=0\.4\.0$/m);
assert.match(bridgeMakefile, /^PKG_RELEASE:=1$/m);
assert.match(bridgeMakefile, /PKG_BUILD_DEPENDS:=modemmanager\b/);
assert.match(bridgeMakefile, /PKG_LICENSE:=GPL-2\.0-or-later/);
for (const dependency of [ 'modemmanager', 'glib2', 'libubox', 'libubus', 'libuci' ])
	assert.match(bridgeMakefile, new RegExp(`\\+${dependency.replaceAll('-', '\\-')}\\b`));
for (const forbidden of [
	'libmbim', 'mbim-utils', 'sms-tool', 'lpac', 'kmod-usb-net-cdc-ncm'
]) {
	assert.doesNotMatch(bridgeMakefile,
		new RegExp(`\\+${forbidden.replaceAll('-', '\\-')}\\b`),
		`bridge must not depend on ${forbidden}`);
}
assert.match(bridgeMakefile, /FIBOCOM_MM_L850_EXPERT/,
	'expert PCI support must require an explicit build option');
assert.match(bridgeMakefile, /default n/,
	'expert PCI support must be disabled by default');

const sourceFiles = filesUnder('fibocom-mm-bridge/src').filter(function(file) {
	return /\.[ch]$/.test(file);
});
const bridgeSource = sourceFiles.map(read).join('\n');
const sourceWithoutNetworkBinding = sourceFiles.filter(function(file) {
	return path.basename(file) !== 'network_binding.c';
}).map(read).join('\n');
const l850CellSource = read('fibocom-mm-bridge/src/l850_cell.c');
const sourceWithoutL850Grammar = sourceFiles.filter(function(file) {
	return path.basename(file) !== 'l850_cell.c';
}).map(read).join('\n');
const bridgeHeader = read('fibocom-mm-bridge/src/bridge.h');
const bridgeInventorySource = read('fibocom-mm-bridge/src/bridge.c');
const ubusSource = read('fibocom-mm-bridge/src/ubus_glib.c');
const requestSource = read('fibocom-mm-bridge/src/ubus_request.c');
const hostBlobSource = read('tests/host-ubus-blob.c');
const dedupeSource = read('fibocom-mm-bridge/src/sms_dedupe_policy.c');
const dedupeHeader = read('fibocom-mm-bridge/src/sms_dedupe_policy.h');
const sourceMakefile = read('fibocom-mm-bridge/src/Makefile');

assert.match(bridgeHeader, /#define FIBOCOM_MM_API_SCHEMA 3U/);
assert.match(bridgeHeader, /#define FIBOCOM_MM_BRIDGE_VERSION "0\.4\.0"/);
assert.match(sourceMakefile, /FIBOCOM_MM_L850_EXPERT/);

const baseTable = ubusSource.match(
	/static const struct ubus_method fibocom_methods\[\][\s\S]*?\n\};/);
assert.ok(baseTable, 'the base fibocom.mm method table must exist');
for (const method of [
	'list_modems', 'get_overview', 'get_lock_status', 'set_bands', 'set_modes',
	'list_sms', 'send_sms', 'delete_sms'
]) {
	assert.match(baseTable[0], new RegExp(`UBUS_METHOD(?:_NOARG)?\\("${method}"`));
}
assert.strictEqual((baseTable[0].match(/UBUS_METHOD(?:_NOARG)?\(/g) || []).length, 8,
	'fibocom.mm must expose exactly the eight schema-3 base methods');
for (const retired of [
	'get_status', 'get_capabilities', 'set_radio', 'reset', 'set_primary_sim_slot'
]) {
	assert.doesNotMatch(ubusSource,
		new RegExp(`UBUS_METHOD(?:_NOARG)?\\("${retired}"`),
		`${retired} must not remain public in schema 3`);
}

const expertTable = ubusSource.match(
	/static const struct ubus_method l850_methods\[\][\s\S]*?\n\};/);
assert.ok(expertTable, 'the build-gated fibocom.mm.l850 method table must exist');
for (const method of [
	'cell_scan', 'cell_lock_status', 'set_cell_lock', 'clear_cell_lock'
]) {
	assert.match(expertTable[0], new RegExp(`UBUS_METHOD\\("${method}"`));
}
assert.strictEqual((expertTable[0].match(/UBUS_METHOD\(/g) || []).length, 4,
	'fibocom.mm.l850 must expose exactly four expert methods');
assert.match(ubusSource,
	/#ifdef FIBOCOM_MM_L850_EXPERT[\s\S]*?static const struct ubus_method l850_methods/,
	'the expert object must not be compiled into the base build');
assert.match(ubusSource,
	/#ifdef FIBOCOM_MM_L850_EXPERT[\s\S]*?\.name\s*=\s*"fibocom\.mm\.l850"/,
	'the expert object name must only exist inside the explicit build gate');
const overviewMethod = ubusSource.match(
	/static int\s+method_get_overview\([^;]*?\)\s*\{[\s\S]*?\n\}/);
assert.ok(overviewMethod, 'the compact Overview method must exist');
assert.doesNotMatch(overviewMethod[0], /"not-validated"/,
	'Overview serving-cell status must not remain hard-coded unavailable');
assert.doesNotMatch(overviewMethod[0], /mm_modem_command|l850-xmci|l850_scan_command/,
	'Overview must never start the vendor/XMCI scan path');
assert.match(ubusSource, /mm_modem_get_cell_info\s*\(/,
	'Serving Cell must use asynchronous standard ModemManager CellInfo');
assert.match(ubusSource, /SERVING_CELL_FRESH_SECONDS/);
assert.match(ubusSource, /serving_cell_generation/);
assert.match(ubusSource, /serving_cell_cache_store[\s\S]*pci\s*>\s*503U/);

for (const forbidden of [
	/mm_modem_simple_connect\s*\(/,
	/mm_modem_create_bearer\s*\(/,
	/mm_bearer_(?:connect|disconnect)\s*\(/,
	/mm_modem_delete_bearer\s*\(/,
	/mm_modem_set_current_modes\s*\(/,
	/mm_manager_(?:scan_devices|report_kernel_event|inhibit_device)\s*\(/,
	/\b(?:system|popen|fork|execv|execl)\s*\(/,
	/['"]\/dev\//
]) {
	assert.doesNotMatch(bridgeSource, forbidden,
		`forbidden bridge operation found: ${forbidden}`);
}
assert.doesNotMatch(sourceWithoutNetworkBinding,
	/\buci_(?:add|commit|delete|rename|reorder|save|set)\s*\(/,
	'only the internally resolved netifd mode-policy writer may mutate UCI');
const networkBindingSource = read('fibocom-mm-bridge/src/network_binding.c');
assert.match(networkBindingSource, /"allowedmode"/);
assert.match(networkBindingSource, /"preferredmode"/);
assert.match(networkBindingSource, /uci_commit\s*\(/);
assert.match(networkBindingSource, /fibocom_network_modes_are_valid/);
assert.match(networkBindingSource, /find_exact_section/);
assert.doesNotMatch(networkBindingSource,
	/["'](?:apn|pincode|username|password)["']\s*,\s*[^)]*uci_set/,
	'the mode-policy writer must not write connection credentials');
assert.doesNotMatch(sourceWithoutL850Grammar, /['"](?:AT|at)[+@]/,
	'fixed vendor command literals must stay isolated in the reviewed L850 grammar');
for (const exactCommand of [
	'AT@SIC:FREQ_LOCK(0,3,255,0,65535,65535)',
	'AT+CFUN=15', 'AT+XMCI=1',
	'AT@NVM:DYN_CPS.NAS_ASM.FREQ_LOCK_PARAMS.*??'
]) {
	assert.ok(l850CellSource.includes(exactCommand),
		`missing live-validated fixed command: ${exactCommand}`);
}
assert.match(l850CellSource,
	/AT@SIC:FREQ_LOCK\(0,3,%u,1,%u,%u\)/,
	'the set tuple must interpolate typed integers only');
assert.doesNotMatch(l850CellSource, /%s/,
	'the reviewed command builder must never interpolate browser strings');
const expertImplementation = ubusSource.match(
	/#ifdef FIBOCOM_MM_L850_EXPERT\s+static gboolean\s+l850_modem_has_active_mutation[\s\S]*?\n#endif\s+\nstatic gboolean reconnect_cb/);
assert.ok(expertImplementation,
	'the command transport implementation must be enclosed by the expert build gate');
assert.doesNotMatch(ubusSource.replace(expertImplementation[0], ''),
	/mm_modem_command\s*\(/,
	'the base build path must not call generic Modem.Command');
assert.match(expertImplementation[0], /mm_modem_command\s*\(/,
	'the reviewed expert path must use ModemManager command serialization');

assert.match(bridgeSource, /SYS_getrandom/);
assert.match(bridgeSource, /FIBOCOM_ID_RANDOM_LEN\s+16U/);
assert.match(bridgeSource, /g_cancellable_cancel\(modem->cancellable\)/);
assert.match(bridgeSource, /G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_DO_NOT_AUTO_START/);
assert.doesNotMatch(bridgeSource, /mm_modem_get_device_identifier\s*\(/);
assert.match(bridgeInventorySource,
	/messages\s*=\s*g_list_sort\s*\(\s*messages\s*,\s*sms_proxy_newest_first\s*\)/,
	'SMS truncation must retain the newest ModemManager entries first');
assert.match(bridgeInventorySource, /G_CALLBACK\(connection_closed\)/,
	'the bridge must recover when its system D-Bus connection closes');
assert.match(bridgeInventorySource, /connect->serial\s*!=\s*bridge->connect_serial/,
	'stale asynchronous reconnect attempts must be rejected');
assert.match(bridgeInventorySource, /fibocom_hardware_attest_l850_mbim\s*\(/,
	'mutations must fail closed on exact live USB hardware');

assert.match(ubusSource, /fibocom_modem_attest_mutation_target\s*\(/);
assert.match(ubusSource, /mm_modem_set_current_bands\s*\(/,
	'band lock must use the standard asynchronous ModemManager method');
assert.match(ubusSource, /mm_modem_get_cell_info\s*\(/,
	'expert scan must try the standard asynchronous ModemManager method first');
assert.match(ubusSource, /mm_modem_get_cell_info_finish\s*\(/);
assert.match(ubusSource, /l850_scan_stale_code\s*\(/,
	'expert scan callbacks must reject a removed or stale modem generation');
assert.match(ubusSource, /L850_SCAN_OPERATION_TIMEOUT_SECONDS/);
assert.match(ubusSource, /standard-modemmanager-get-cell-info/);
assert.match(ubusSource, /fibocom_l850_nvm_parse\s*\(/);
assert.match(ubusSource, /FIBOCOM_L850_STATE_APPLIED_VERIFIED|applied_verified/);
assert.match(ubusSource, /cleared_verified/);
assert.match(ubusSource, /reprobe_timeout/);
assert.match(ubusSource, /registration_timeout/);
assert.match(ubusSource, /verification_mismatch/);
assert.match(ubusSource, /output->rsrp\s*!=\s*-G_MAXDOUBLE/,
	'standard cell-info sentinels must not be exported as measurements');
const scanMethodStart = ubusSource.lastIndexOf('\nmethod_cell_scan(');
const scanMethodEnd = ubusSource.indexOf(
	'\nstatic FibocomModem *\nl850_mutation_response_modem', scanMethodStart);
assert.ok(scanMethodStart > 0 && scanMethodEnd > scanMethodStart,
	'the concrete cell_scan implementation must be discoverable');
const scanMethod = ubusSource.slice(scanMethodStart, scanMethodEnd);
assert.match(scanMethod, /mm_modem_get_cell_info\s*\(/);
assert.doesNotMatch(scanMethod, /l850_firmware_allowed\s*\(/,
	'standard GetCellInfo must be attempted before any unavailable vendor fallback');
assert.match(ubusSource, /operation->dispatched\s*=\s*TRUE/,
	'mutations must distinguish uncertainty after dispatch');
assert.match(ubusSource, /advanced_operation_outcome_is_unknown\s*\(/);
assert.match(ubusSource, /advanced_operation_stale_code\s*\(/);
assert.match(ubusSource, /FIBOCOM_MUTATION_ADVANCED/,
	'band and SMS mutations must share the per-modem lock');

assert.match(requestSource, /blobmsg_check_attr_len\s*\(/,
	'every untrusted blob attribute must be structurally validated');
for (const parser of [
	'fibocom_ubus_message_is_empty', 'fibocom_ubus_parse_exact'
]) {
	const body = requestSource.match(new RegExp(
		`${parser}\\s*\\([\\s\\S]*?\\n\\}`));
	assert.ok(body && body[0].includes('fibocom_ubus_blob_attr_is_valid'),
		`${parser} must structurally validate each attribute before reading it`);
	assert.ok(body && body[0].includes('fibocom_ubus_rpc_session_is_valid'),
		`${parser} must support one canonical rpcd session transport field`);
}
assert.match(requestSource,
	/session_seen\s*\|\|[\s\S]*?!fibocom_ubus_rpc_session_is_valid/,
	'duplicate or malformed rpcd session fields must fail closed');
assert.doesNotMatch(requestSource, /blobmsg_for_each_attr/,
	'malformed trailing request attributes must not be hidden by an iterator stop');
assert.doesNotMatch(ubusSource, /blobmsg_for_each_attr/,
	'malformed trailing array items must not be hidden by an iterator stop');
assert.match(hostBlobSource, /malformed trailing attribute/);

assert.match(dedupeHeader, /#define FIBOCOM_SMS_DEDUPE_SECONDS 300U/);
assert.match(dedupeHeader, /#define FIBOCOM_SMS_DEDUPE_MAX 64U/);
assert.match(ubusSource, /sms_dedupe_prune\s*\(/);
assert.match(dedupeSource, /fibocom_sms_dedupe_is_expired\s*\(/);
assert.match(dedupeSource, /fibocom_sms_dedupe_evictions_required\s*\(/);
assert.match(ubusSource, /g_queue_pop_head\s*\(modem->sms_dedupe\)/,
	'the documented capacity contract must match oldest-entry eviction');
assert.match(ubusSource, /dedupe_capacity/);
assert.match(ubusSource, /dedupe_window_seconds/);
assert.match(ubusSource, /\.Message\.MemoryFull/);
assert.match(ubusSource, /MM_MESSAGE_ERROR_MEMORY_FULL/);
assert.match(ubusSource, /\.Message\.NetworkTimeout/);
assert.match(ubusSource, /MM_MESSAGE_ERROR_NETWORK_TIMEOUT/);

for (const requiredTest of [
	'tests/host-ubus-blob.c', 'tests/run-host-ubus-blob.sh',
	'tests/host-sms-dedupe.c', 'tests/run-host-sms-dedupe.sh',
	'tests/host-cell-parser.c', 'tests/run-host-cell-parser.sh'
]) {
	assert.ok(fs.existsSync(absolute(requiredTest)), `${requiredTest} must exist`);
}

const cellSource = read('fibocom-mm-bridge/src/l850_cell.c');
for (const state of [
	'available', 'unsupported_build', 'unsupported_firmware', 'scan_ready',
	'lock_applied_reset_required', 'resetting', 'applied_verified',
	'cleared_verified', 'reprobe_timeout', 'registration_timeout',
	'verification_mismatch', 'outcome_unknown'
]) {
	assert.ok(cellSource.includes(`"${state}"`), `PCI state ${state} must be modeled`);
}
assert.match(cellSource, /pci\s*>\s*503/);
assert.match(cellSource, /type\s*!=\s*4[Uu]?\s*&&\s*type\s*!=\s*5[Uu]?/,
	'cell parsing must accept LTE records of type 4 and 5 only');
assert.match(cellSource, /FIBOCOM_L850_CELL_MAX_RESULTS/,
	'cell scan output must be bounded');
assert.match(cellSource, /fibocom_l850_earfcn_to_band/,
	'EARFCN must be mapped to a supported LTE band before mutation');

const init = read('fibocom-mm-bridge/files/etc/init.d/fibocom-mm-bridge');
assert.match(init, /^USE_PROCD=1$/m);
assert.match(init, /^START=75$/m);
assert.match(init, /command "\$PROG" --foreground/);

const luciMakefile = read('luci-app-fibocom/Makefile');
assert.match(luciMakefile, /^PKG_VERSION:=0\.4\.0$/m);
assert.match(luciMakefile, /^PKG_RELEASE:=1$/m);
assert.match(luciMakefile, /^LUCI_URL:=https:\/\/github\.com\/As-tsaqib\/luci-app-fibocom$/m);
assert.match(luciMakefile, /^LUCI_MAINTAINER:=As Tsaqib <[^>]+>$/m);
for (const dependency of [
	'+luci-base', '+fibocom-mm-bridge', '+modemmanager', '+luci-proto-modemmanager',
	'+kmod-usb-acm', '+kmod-usb-net-cdc-mbim', '+kmod-usb-wdm'
]) {
	assert.ok(luciMakefile.includes(dependency), `LuCI Makefile must include ${dependency}`);
}
assert.doesNotMatch(luciMakefile, /@MODEMMANAGER_WITH_(?:MBIM|NETIFD)/,
	'LuCI must not create a recursive PACKAGE_modemmanager Kconfig dependency');
for (const forbidden of [
	'fibocomd', 'luci-proto-fibocom', 'modemmanager-plugin-fibocom',
	'sms-tool', '+lpac'
]) {
	assert.ok(!luciMakefile.includes(forbidden), `base LuCI must not include ${forbidden}`);
}

const acl = JSON.parse(read(
	'luci-app-fibocom/root/usr/share/rpcd/acl.d/luci-app-fibocom.json'
));
assert.deepStrictEqual(Object.keys(acl).sort(), [
	'luci-app-fibocom-lock-band',
	'luci-app-fibocom-lock-pci-expert',
	'luci-app-fibocom-overview',
	'luci-app-fibocom-sms-read',
	'luci-app-fibocom-sms-write'
]);
assert.deepStrictEqual(acl['luci-app-fibocom-overview'].read.ubus['fibocom.mm'], [
	'list_modems', 'get_overview', 'get_lock_status'
]);
assert.deepStrictEqual(acl['luci-app-fibocom-sms-read'].read.ubus['fibocom.mm'], [
	'list_sms'
]);
assert.deepStrictEqual(acl['luci-app-fibocom-sms-write'].write.ubus['fibocom.mm'], [
	'send_sms', 'delete_sms'
]);
assert.deepStrictEqual(acl['luci-app-fibocom-lock-band'].write.ubus['fibocom.mm'], [
	'set_bands', 'set_modes'
]);
assert.deepStrictEqual(
	acl['luci-app-fibocom-lock-pci-expert'].read.ubus['fibocom.mm.l850'],
	[ 'cell_scan', 'cell_lock_status' ]);
assert.deepStrictEqual(
	acl['luci-app-fibocom-lock-pci-expert'].write.ubus['fibocom.mm.l850'],
	[ 'set_cell_lock', 'clear_cell_lock' ]);
const aclSource = read(
	'luci-app-fibocom/root/usr/share/rpcd/acl.d/luci-app-fibocom.json');
for (const forbidden of [ '"*"', '"cgi-io"', '"file"', '"uci"', '"exec"' ])
	assert.ok(!aclSource.includes(forbidden), `ACL must not contain ${forbidden}`);

const staticWorkflow = read('.github/workflows/static.yml');
assert.match(staticWorkflow, /run-host-ubus-blob\.sh/);
assert.match(staticWorkflow, /run-host-sms-dedupe\.sh/);
assert.match(staticWorkflow, /run-host-cell-parser\.sh/);
assert.match(staticWorkflow, /make check/);
const sdkWorkflow = read('.github/workflows/openwrt-sdk.yml');
assert.ok(sdkWorkflow.includes("grep -Fq -- '-Dbuiltin_plugins=true'"));
assert.ok(sdkWorkflow.includes('CONFIG_FIBOCOM_MM_BRIDGE_L850_EXPERT=y'),
	'SDK CI must exercise the explicit expert build separately');
assert.ok(sdkWorkflow.includes('CONFIG_MODEMMANAGER_WITH_AT_COMMAND_VIA_DBUS=y'));
assert.ok(sdkWorkflow.includes('package/feeds/packages/modemmanager/compile'),
	'expert CI must rebuild the matching ModemManager package');
assert.ok(sdkWorkflow.includes("grep -Fq -- '-Dat_command_via_dbus=true'"));
assert.ok(sdkWorkflow.includes('Firmware_Allowlist=18500.5001.00.05.27.30'));
assert.ok(!sdkWorkflow.includes('luci-app-fibocom-esim'));
assert.ok(!sdkWorkflow.includes('luci-app-lpac'));
assert.ok(!sdkWorkflow.includes('LPAC_'));

const liveFixture = JSON.parse(read('tests/fixtures/live/l850-mbim-connected.json'));
assert.strictEqual(liveFixture.hardware.model, 'L850-GL');
assert.strictEqual(liveFixture.hardware.composition, 'mbim');
assert.strictEqual(liveFixture.modemmanager.state, 'connected');
assert.strictEqual(liveFixture.openwrt.protocol, 'modemmanager');
assert.ok(Object.values(liveFixture.privacy).every(function(value) {
	return value === 'redacted';
}), 'live fixture privacy fields must remain redacted');

console.log('package contract validation passed');
