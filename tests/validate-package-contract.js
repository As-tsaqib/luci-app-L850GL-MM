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
		`${retired} must remain retired from the 0.6 product tree`);
}

const bridgeMakefile = read('l850gl-mm-bridge/Makefile');
assert.match(bridgeMakefile, /^PKG_NAME:=l850gl-mm-bridge$/m);
assert.match(bridgeMakefile, /^PKG_VERSION:=0\.6\.0$/m);
assert.match(bridgeMakefile, /^PKG_RELEASE:=2$/m);
assert.match(bridgeMakefile,
	/^\s*URL:=https:\/\/github\.com\/As-tsaqib\/luci-app-L850GL-MM$/m);
assert.match(bridgeMakefile, /^\s*CONFLICTS:=fibocom-mm-bridge$/m,
	'the renamed backend package must conflict with the retired bridge');
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
assert.match(bridgeMakefile, /^\s*config L850GL_MM_BRIDGE_EXPERT$/m,
	'expert PCI support must require an explicit build option');
assert.match(bridgeMakefile,
	/L850GL_MM_EXPERT="\$\(if \$\(CONFIG_L850GL_MM_BRIDGE_EXPERT\),1,\)"/,
	'the package Kconfig symbol must enable only the reviewed source macro');
assert.match(bridgeMakefile, /default n/,
	'expert PCI support must be disabled by default');

const sourceFiles = filesUnder('l850gl-mm-bridge/src').filter(function(file) {
	return /\.[ch]$/.test(file);
});
const bridgeSource = sourceFiles.map(read).join('\n');
const sourceWithoutNetworkBinding = sourceFiles.filter(function(file) {
	return path.basename(file) !== 'network_binding.c';
}).map(read).join('\n');
const l850CellSource = read('l850gl-mm-bridge/src/l850_cell.c');
const l850CellHeader = read('l850gl-mm-bridge/src/l850_cell.h');
const l850CaSource = read('l850gl-mm-bridge/src/l850_ca.c');
const l850CaHeader = read('l850gl-mm-bridge/src/l850_ca.h');
const l850VoltageSource = read('l850gl-mm-bridge/src/l850_voltage.c');
const l850VoltageHeader = read('l850gl-mm-bridge/src/l850_voltage.h');
const sourceWithoutL850Grammar = sourceFiles.filter(function(file) {
	return ![ 'l850_cell.c', 'l850_ca.c', 'l850_voltage.c' ]
		.includes(path.basename(file));
}).map(read).join('\n');
const bridgeHeader = read('l850gl-mm-bridge/src/bridge.h');
const bridgeInventorySource = read('l850gl-mm-bridge/src/bridge.c');
const ubusSource = read('l850gl-mm-bridge/src/ubus_glib.c');
const requestSource = read('l850gl-mm-bridge/src/ubus_request.c');
const hostBlobSource = read('tests/host-ubus-blob.c');
const identityHeader = read('l850gl-mm-bridge/src/identity.h');
const smsPolicySource = read('l850gl-mm-bridge/src/sms_policy.c');
const dedupeSource = read('l850gl-mm-bridge/src/sms_dedupe_policy.c');
const dedupeHeader = read('l850gl-mm-bridge/src/sms_dedupe_policy.h');
const sourceMakefile = read('l850gl-mm-bridge/src/Makefile');
const widgetsSource = read(
	'luci-app-l850gl-mm/htdocs/luci-static/resources/l850gl-mm/widgets.js');

assert.match(bridgeHeader, /#define L850GL_MM_API_SCHEMA 4U/);
assert.match(bridgeHeader, /#define L850GL_MM_BRIDGE_VERSION "0\.6\.0"/);
assert.match(sourceMakefile, /L850GL_MM_EXPERT/);
assert.match(sourceMakefile, /^TARGET := l850gl-mm-bridge$/m);
assert.match(identityHeader, /#define L850GL_ID_PREFIX "l850gl-"/,
	'opaque modem IDs must use the rebranded namespace');
assert.match(smsPolicySource,
	/static const gchar domain\[\] = "l850gl-mm-send-sms-v1"/,
	'SMS request digests must use the rebranded domain separator');
const sourceWithoutRequiredUpstreamBrand = bridgeSource
	.replaceAll('"fibocom"', '"upstream-plugin-or-manufacturer"')
	.replaceAll('"fibocomwireless"', '"upstream-manufacturer"')
	.replaceAll('"fibocomwirelessinc"', '"upstream-manufacturer-inc"');
assert.doesNotMatch(sourceWithoutRequiredUpstreamBrand, /fibocom/i,
	'legacy branding may remain only in required upstream plugin/manufacturer literals');

const baseTable = ubusSource.match(
	/static const struct ubus_method l850gl_methods\[\][\s\S]*?\n\};/);
assert.ok(baseTable, 'the base l850gl.mm method table must exist');
for (const method of [
	'list_modems', 'get_overview', 'get_lock_status', 'set_bands', 'set_modes',
	'list_sms', 'send_sms', 'delete_sms'
]) {
	assert.match(baseTable[0], new RegExp(`UBUS_METHOD(?:_NOARG)?\\("${method}"`));
}
assert.strictEqual((baseTable[0].match(/UBUS_METHOD(?:_NOARG)?\(/g) || []).length, 8,
	'l850gl.mm must expose exactly the eight schema-4 base methods');
for (const retired of [
	'get_status', 'get_capabilities', 'set_radio', 'reset', 'set_primary_sim_slot'
]) {
	assert.doesNotMatch(ubusSource,
		new RegExp(`UBUS_METHOD(?:_NOARG)?\\("${retired}"`),
		`${retired} must not remain public in schema 4`);
}

const expertTable = ubusSource.match(
	/static const struct ubus_method l850_methods\[\][\s\S]*?\n\};/);
assert.ok(expertTable, 'the build-gated l850gl.mm.l850 method table must exist');
for (const method of [
	'cell_scan', 'get_carrier_info', 'cell_lock_status', 'set_cell_lock',
	'clear_cell_lock'
]) {
	assert.match(expertTable[0], new RegExp(`UBUS_METHOD\\("${method}"`));
}
assert.strictEqual((expertTable[0].match(/UBUS_METHOD\(/g) || []).length, 5,
	'l850gl.mm.l850 must expose exactly five expert methods');
assert.match(ubusSource,
	/#ifdef L850GL_MM_EXPERT[\s\S]*?static const struct ubus_method l850_methods/,
	'the expert object must not be compiled into the base build');
assert.match(ubusSource,
	/#ifdef L850GL_MM_EXPERT[\s\S]*?\.name\s*=\s*"l850gl\.mm\.l850"/,
	'the expert object name must only exist inside the explicit build gate');
assert.doesNotMatch(ubusSource, /fibocom\.mm/i,
	'the retired ubus object namespace must not be published or logged');
const overviewMethod = ubusSource.match(
	/static int\s+method_get_overview\([^;]*?\)\s*\{[\s\S]*?\n\}/);
assert.ok(overviewMethod, 'the compact Overview method must exist');
const listModemsMethod = ubusSource.match(
	/static int\s+method_list_modems\([^;]*?\)\s*\{[\s\S]*?\n\}/);
assert.ok(listModemsMethod, 'the bounded modem inventory method must exist');
assert.doesNotMatch(listModemsMethod[0], /"(?:imei|imsi|iccid|number)"/,
	'list_modems must never disclose Overview or SIM identifiers');
assert.doesNotMatch(overviewMethod[0], /"not-validated"/,
	'Overview serving-cell status must not remain hard-coded unavailable');
assert.doesNotMatch(overviewMethod[0], /mm_modem_command|l850-xmci|l850_scan_command/,
	'Overview must never start the vendor/XMCI scan path');
assert.match(ubusSource, /mm_modem_get_cell_info\s*\(/,
	'Serving Cell must use asynchronous standard ModemManager CellInfo');
assert.match(ubusSource, /SERVING_CELL_FRESH_SECONDS/);
assert.match(ubusSource, /serving_cell_generation/);
assert.match(ubusSource, /serving_cell_cache_store[\s\S]*pci\s*>\s*503U/);
assert.match(overviewMethod[0], /add_modem_voltage\(&buffer, ubus, modem\)/,
	'Overview must serialize the generation-bound voltage cache');
assert.match(ubusSource,
	/blobmsg_add_field\(buffer, BLOBMSG_TYPE_UNSPEC, "voltage_mv",[\s\S]*?NULL, 0U\)/,
	'Unavailable voltage must remain an explicit typed null');
assert.match(ubusSource,
	/add_safe_string\(&buffer, "imei",[\s\S]*?mm_modem_get_equipment_identifier\(modem->modem\)/,
	'Overview must source IMEI from the bounded ModemManager equipment identifier');
assert.match(ubusSource, /blobmsg_add_string\(&buffer, "usb_mode",[\s\S]*?l850gl_modem_composition\(modem\)/,
	'Overview must expose the bounded MBIM, NCM, or unknown composition enum');
assert.match(ubusSource, /add_safe_string\(buffer, "imsi",[\s\S]*?mm_sim_get_imsi\(cached_sim\)/,
	'Overview must source IMSI only from the asynchronously cached MMSim');
assert.match(ubusSource, /add_safe_string\(buffer, "iccid",[\s\S]*?mm_sim_get_identifier\(cached_sim\)/,
	'Overview must source ICCID only from the asynchronously cached MMSim');
assert.match(ubusSource,
	/cached_sim\s*=\s*present\s*&&[\s\S]*?g_str_equal\(modem->sim_cache_state, "ready"\)/,
	'Overview must fail closed instead of exporting a stale SIM snapshot');
assert.match(ubusSource, /preferred_own_number[\s\S]*?mm_modem_get_own_numbers\(modem\)/,
	'Overview must select its SIM number from ModemManager OwnNumbers');
assert.match(ubusSource,
	/g_autofree gchar \*number\s*=\s*cached_sim\s*!=\s*NULL\s*\?\s*[\s\S]*?preferred_own_number/,
	'Overview must not export a possibly stale OwnNumbers value while SIM refresh is pending');
assert.match(ubusSource, /index\s*<\s*MAX_OWN_NUMBERS/,
	'OwnNumbers traversal must stay bounded');
assert.match(ubusSource, /g_strcmp0\(candidate, selected\)\s*<\s*0/,
	'OwnNumbers selection must be deterministic even if D-Bus ordering changes');
assert.doesNotMatch(ubusSource,
	/mm_sim_get_path|mm_modem_get_device_identifier\s*\(/,
	'Overview must not export raw object paths or stable hashed device identifiers');
const loggingCalls = bridgeSource.match(
	/\b(?:g_(?:debug|info|message|warning|critical|error|log)|syslog|printf|fprintf)\s*\([^;]*\);/g) || [];
assert.ok(loggingCalls.length > 0, 'backend logging calls must remain discoverable');
loggingCalls.forEach(function(call) {
	assert.doesNotMatch(call,
		/mm_modem_get_(?:equipment_identifier|own_numbers)\s*\(|mm_sim_get_(?:imsi|identifier)\s*\(|\b(?:imei|imsi|iccid|own_numbers?|number)\b/i,
		'backend logging calls must never receive modem or SIM identifier values');
});

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
const networkBindingSource = read('l850gl-mm-bridge/src/network_binding.c');
assert.match(networkBindingSource, /"allowedmode"/);
assert.match(networkBindingSource, /"preferredmode"/);
assert.match(networkBindingSource, /uci_commit\s*\(/);
assert.match(networkBindingSource, /l850gl_network_modes_are_valid/);
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
assert.ok(l850CaSource.includes('AT+GTCAINFO?'),
	'the carrier query must remain one reviewed fixed L850 command');
assert.match(l850CaSource,
	/if \(ul_bandwidth == CA_BANDWIDTH_SENTINEL\)[\s\S]*?carrier->uplink_active = false/,
	'the live secondary uplink-bandwidth sentinel must be represented explicitly');
assert.match(l850CaSource,
	/if \(carrier->uplink_active\)\s*return L850GL_L850_CA_PARSE_RANGE/,
	'unverified secondary uplink aggregation must remain fail-closed');
assert.match(l850CaSource,
	/!slots\[index\]\.carrier\.uplink_active[\s\S]*?slots\[0\]\.carrier\.ul_earfcn/,
	'a downlink-only secondary must repeat the validated primary uplink EARFCN');
assert.doesNotMatch(l850CaSource,
	/left->ul_earfcn == right->ul_earfcn/,
	'duplicate public carriers must not be distinguished by an unexported uplink field');
assert.match(ubusSource,
	/BLOBMSG_TYPE_UNSPEC,\s*"ul_bandwidth_mhz", NULL, 0U/,
	'an active downlink-only secondary uplink bandwidth must serialize as explicit null');
assert.match(widgetsSource,
	/secondary \?\s*carrier\.ul_bandwidth_mhz === null/,
	'the frontend must accept explicit null uplink bandwidth only for secondaries');
for (const fixture of [
	'valid-live-downlink-only-secondaries.txt',
	'valid-live-secondaries-before-primary.txt',
	'valid-inactive-before-primary.txt',
	'invalid-secondary-copied-ul-mismatch.txt',
	'invalid-inactive-copied-ul-mismatch.txt',
	'invalid-unverified-secondary-uplink.txt',
	'invalid-primary-ul-sentinel.txt'
]) {
	assert.ok(fs.existsSync(absolute(`tests/fixtures/ca/${fixture}`)),
		`missing reviewed carrier fixture: ${fixture}`);
}
assert.ok(l850VoltageSource.includes('AT+CBC'),
	'the voltage query must remain one reviewed fixed L850 command');
assert.match(sourceMakefile, /\bl850_ca\.c\b/,
	'the bounded L850 carrier parser must be built into the bridge');
assert.match(sourceMakefile, /\bl850_voltage\.c\b/,
	'the bounded L850 voltage parser must be built into the bridge');
assert.match(l850VoltageHeader,
	/#define L850GL_L850_VOLTAGE_MAX_RESPONSE 128U/,
	'the voltage response must remain tightly bounded');
assert.match(l850VoltageHeader,
	/#define L850GL_L850_VOLTAGE_MIN_MV 2500U/);
assert.match(l850VoltageHeader,
	/#define L850GL_L850_VOLTAGE_MAX_MV 5000U/);
const backendCaRangeTable = l850CaSource.match(
	/static const struct CaBandRange ca_band_ranges\[\]\s*=\s*\{([\s\S]*?)\n\};/);
const frontendCaRangeTable = widgetsSource.match(
	/const lteDownlinkRanges\s*=\s*\[([\s\S]*?)\n\];/);
assert.ok(backendCaRangeTable && frontendCaRangeTable,
	'backend and frontend LTE carrier range tables must remain discoverable');
const backendCaDownlinkRanges = Array.from(backendCaRangeTable[1].matchAll(
	/\{\s*(\d+)U,\s*(\d+)U,\s*(\d+)U,\s*\d+U,\s*\d+U\s*\}/g),
	function(match) { return match.slice(1, 4).map(Number); });
const frontendCaDownlinkRanges = Array.from(frontendCaRangeTable[1].matchAll(
	/\[\s*(\d+),\s*(\d+),\s*(\d+)\s*\]/g),
	function(match) { return match.slice(1, 4).map(Number); });
assert.ok(backendCaDownlinkRanges.length > 0 && frontendCaDownlinkRanges.length > 0,
	'LTE carrier range tables must contain typed numeric entries');
assert.deepStrictEqual(frontendCaDownlinkRanges, backendCaDownlinkRanges,
	'frontend carrier validation must match the exact backend L850 band subset');
assert.match(l850CellSource,
	/AT@SIC:FREQ_LOCK\(0,3,%u,1,%u,%u\)/,
	'the set tuple must interpolate typed integers only');
assert.doesNotMatch(l850CellSource, /%s/,
	'the reviewed command builder must never interpolate browser strings');
const expertImplementation = ubusSource.match(
	/#ifdef L850GL_MM_EXPERT\s+static gboolean\s+l850_modem_has_active_mutation[\s\S]*?\n#endif\s+\nstatic gboolean reconnect_cb/);
assert.ok(expertImplementation,
	'the command transport implementation must be enclosed by the expert build gate');
assert.doesNotMatch(ubusSource.replace(expertImplementation[0], ''),
	/mm_modem_command\s*\(/,
	'the base build path must not call generic Modem.Command');
assert.match(expertImplementation[0], /mm_modem_command\s*\(/,
	'the reviewed expert path must use ModemManager command serialization');

assert.match(bridgeSource, /SYS_getrandom/);
assert.match(bridgeSource, /L850GL_ID_RANDOM_LEN\s+16U/);
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
assert.match(bridgeInventorySource, /l850gl_hardware_attest_l850_mbim\s*\(/,
	'mutations must fail closed on exact live USB hardware');

assert.match(ubusSource, /l850gl_modem_attest_mutation_target\s*\(/);
assert.match(ubusSource, /mm_modem_set_current_bands\s*\(/,
	'band lock must use the standard asynchronous ModemManager method');
assert.match(ubusSource, /mm_modem_get_cell_info\s*\(/,
	'expert scan must try the standard asynchronous ModemManager method first');
assert.match(ubusSource, /mm_modem_get_cell_info_finish\s*\(/);
assert.match(ubusSource, /l850_scan_stale_code\s*\(/,
	'expert scan callbacks must reject a removed or stale modem generation');
assert.match(ubusSource, /L850_SCAN_OPERATION_TIMEOUT_SECONDS/);
assert.match(l850CellHeader,
	/#define L850GL_L850_CELL_SCAN_COOLDOWN_SECONDS 5U/,
	'expert cell scan cooldown must be exactly five seconds');
assert.match(ubusSource,
	/operation->modem->l850_last_scan_completed_at\s*=\s*\n?\s*g_get_monotonic_time\(\)/,
	'expert cell scan cooldown must start only from common completion');
assert.match(ubusSource, /l850_modem_has_active_scan\s*\(/,
	'expert cell scan must enforce one active scan per modem');
assert.match(ubusSource, /standard-modemmanager-get-cell-info/);
assert.match(ubusSource, /l850gl_l850_nvm_parse\s*\(/);
assert.match(ubusSource, /L850GL_L850_STATE_APPLIED_VERIFIED|applied_verified/);
assert.match(ubusSource, /cleared_verified/);
assert.match(ubusSource, /reprobe_timeout/);
assert.match(ubusSource, /registration_timeout/);
assert.match(ubusSource, /verification_mismatch/);
assert.match(ubusSource, /output->rsrp\s*!=\s*-G_MAXDOUBLE/,
	'standard cell-info sentinels must not be exported as measurements');
const scanMethodStart = ubusSource.lastIndexOf('\nmethod_cell_scan(');
const scanMethodEnd = ubusSource.indexOf(
	'\nstatic const gchar *\nl850_carrier_stale_code', scanMethodStart);
assert.ok(scanMethodStart > 0 && scanMethodEnd > scanMethodStart,
	'the concrete cell_scan implementation must be discoverable');
const scanMethod = ubusSource.slice(scanMethodStart, scanMethodEnd);
assert.match(scanMethod, /mm_modem_get_cell_info\s*\(/);
assert.match(scanMethod,
	/if \(l850_modem_has_active_scan\(ubus, modem\)\)[\s\S]*?"busy"/,
	'an overlapping per-modem scan must fail as busy before dispatch');
assert.doesNotMatch(scanMethod,
	/l850_last_scan_completed_at\s*=\s*now/,
	'the cell scan cooldown must not start when a scan is dispatched');
assert.doesNotMatch(scanMethod, /l850_firmware_allowed\s*\(/,
	'standard GetCellInfo must be attempted before any unavailable vendor fallback');
assert.match(l850CaHeader,
	/#define L850GL_L850_CA_QUERY_COOLDOWN_SECONDS 5U/,
	'expert carrier reads must have an exact five-second cooldown');
assert.match(ubusSource,
	/operation->modem->l850_last_carrier_query_completed_at\s*=\s*\n?\s*g_get_monotonic_time\(\)/,
	'carrier cooldown must start from common async completion');
assert.match(ubusSource,
	/mm_modem_command\(operation->modem->modem,\s*\n?\s*l850gl_l850_ca_query_command\(\)/,
	'carrier info must dispatch only the compiled-in command via ModemManager');
assert.match(ubusSource, /l850gl_l850_ca_parse\s*\(/,
	'carrier info must pass the bounded response through its typed parser');
assert.match(ubusSource,
	/mm_modem_command\(modem->modem, l850gl_l850_voltage_query_command\(\)/,
	'Overview voltage refresh must dispatch only the compiled-in command');
assert.match(ubusSource, /l850gl_l850_voltage_parse\s*\(/,
	'Overview voltage refresh must pass the bounded response through its parser');
assert.match(ubusSource,
	/l850_voltage_generation\s*==\s*modem->generation/,
	'cached voltage must be bound to the current modem generation');
assert.match(ubusSource, /L850_VOLTAGE_RETRY_SECONDS 10U/,
	'failed or pending Overview polls must not create an AT command storm');
assert.match(ubusSource,
	/l850_voltage_refresh[\s\S]*?l850_modem_has_active_scan\(ubus, modem\)[\s\S]*?l850_modem_has_active_carrier_query\(ubus, modem\)/,
	'voltage refresh must not overlap a PCI scan or carrier command');
assert.match(ubusSource,
	/L850_CARRIER_OPERATION_TIMEOUT_MS 20000U/,
	'carrier info async work must be bounded to twenty seconds');
assert.match(ubusSource,
	/l850_modem_has_active_carrier_query[\s\S]*?l850_modem_has_active_scan\(ubus, modem\)/,
	'carrier reads must reject overlap with another carrier read or PCI scan');
for (const resolver of [
	'sms_mutation_modem', 'advanced_mutation_modem', 'l850_requested_modem'
]) {
	const body = ubusSource.match(new RegExp(
		`static L850GLModem\\s*\\*\\s*${resolver}\\s*\\([^;]*?\\)\\s*\\{[\\s\\S]*?\\n\\}`));

	assert.ok(body, `${resolver} must remain structurally discoverable`);
	assert.match(body[0], /l850_modem_has_active_scan\(ubus, modem\)/,
		`${resolver} must reject mutation admission during an expert scan`);
	assert.match(body[0], /l850_modem_has_active_carrier_query\(ubus, modem\)/,
		`${resolver} must reject mutation admission during a carrier query`);
	assert.match(body[0], /l850_voltage_refresh_pending/,
		`${resolver} must reject admission during a voltage query`);
}
assert.match(ubusSource,
	/for \(index = 0U; index < info\.length; index\+\+\)[\s\S]*?l850_band_is_supported/,
	'every active carrier band must match live ModemManager SupportedBands');
for (const field of [
	'active_bands', 'primary', 'secondary', 'active_carriers',
	'dl_bandwidth_mhz', 'ul_bandwidth_mhz'
]) {
	assert.ok(ubusSource.includes(`"${field}"`),
		`carrier response must contain ${field}`);
}
assert.match(ubusSource, /operation->dispatched\s*=\s*TRUE/,
	'mutations must distinguish uncertainty after dispatch');
assert.match(ubusSource, /advanced_operation_outcome_is_unknown\s*\(/);
assert.match(ubusSource, /advanced_operation_stale_code\s*\(/);
assert.match(ubusSource, /L850GL_MUTATION_ADVANCED/,
	'band and SMS mutations must share the per-modem lock');

assert.match(requestSource, /blobmsg_check_attr_len\s*\(/,
	'every untrusted blob attribute must be structurally validated');
for (const parser of [
	'l850gl_ubus_message_is_empty', 'l850gl_ubus_parse_exact'
]) {
	const body = requestSource.match(new RegExp(
		`${parser}\\s*\\([\\s\\S]*?\\n\\}`));
	assert.ok(body && body[0].includes('l850gl_ubus_blob_attr_is_valid'),
		`${parser} must structurally validate each attribute before reading it`);
	assert.ok(body && body[0].includes('l850gl_ubus_rpc_session_is_valid'),
		`${parser} must support one canonical rpcd session transport field`);
}
assert.match(requestSource,
	/session_seen\s*\|\|[\s\S]*?!l850gl_ubus_rpc_session_is_valid/,
	'duplicate or malformed rpcd session fields must fail closed');
assert.doesNotMatch(requestSource, /blobmsg_for_each_attr/,
	'malformed trailing request attributes must not be hidden by an iterator stop');
assert.doesNotMatch(ubusSource, /blobmsg_for_each_attr/,
	'malformed trailing array items must not be hidden by an iterator stop');
assert.match(hostBlobSource, /malformed trailing attribute/);

assert.match(dedupeHeader, /#define L850GL_SMS_DEDUPE_SECONDS 300U/);
assert.match(dedupeHeader, /#define L850GL_SMS_DEDUPE_MAX 64U/);
assert.match(ubusSource, /sms_dedupe_prune\s*\(/);
assert.match(dedupeSource, /l850gl_sms_dedupe_is_expired\s*\(/);
assert.match(dedupeSource, /l850gl_sms_dedupe_evictions_required\s*\(/);
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
	'tests/host-cell-parser.c', 'tests/run-host-cell-parser.sh',
	'tests/host-voltage-parser.c', 'tests/run-host-voltage-parser.sh'
]) {
	assert.ok(fs.existsSync(absolute(requiredTest)), `${requiredTest} must exist`);
}

const cellSource = read('l850gl-mm-bridge/src/l850_cell.c');
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
assert.match(cellSource, /L850GL_L850_CELL_MAX_RESULTS/,
	'cell scan output must be bounded');
assert.match(cellSource, /l850gl_l850_earfcn_to_band/,
	'EARFCN must be mapped to a supported LTE band before mutation');

const init = read('l850gl-mm-bridge/files/etc/init.d/l850gl-mm-bridge');
assert.match(init, /^USE_PROCD=1$/m);
assert.match(init, /^START=75$/m);
assert.match(init, /^PROG=\/usr\/sbin\/l850gl-mm-bridge$/m);
assert.match(init, /command "\$PROG" --foreground/);

const luciMakefile = read('luci-app-l850gl-mm/Makefile');
assert.match(luciMakefile, /^PKG_VERSION:=0\.6\.0$/m);
assert.match(luciMakefile, /^PKG_RELEASE:=2$/m);
assert.match(luciMakefile, /^LUCI_URL:=https:\/\/github\.com\/As-tsaqib\/luci-app-L850GL-MM$/m);
assert.match(luciMakefile, /^LUCI_MAINTAINER:=As Tsaqib <[^>]+>$/m);
for (const dependency of [
	'+luci-base', '+l850gl-mm-bridge', '+modemmanager', '+luci-proto-modemmanager',
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
	'luci-app-l850gl-mm/root/usr/share/rpcd/acl.d/luci-app-l850gl-mm.json'
));
assert.deepStrictEqual(Object.keys(acl).sort(), [
	'luci-app-l850gl-mm-lock-band',
	'luci-app-l850gl-mm-lock-pci-expert',
	'luci-app-l850gl-mm-overview',
	'luci-app-l850gl-mm-sms-read',
	'luci-app-l850gl-mm-sms-write'
]);
assert.deepStrictEqual(acl['luci-app-l850gl-mm-overview'].read.ubus['l850gl.mm'], [
	'list_modems', 'get_overview', 'get_lock_status'
]);
assert.deepStrictEqual(
	acl['luci-app-l850gl-mm-overview'].read.ubus['l850gl.mm.l850'],
	[ 'get_carrier_info' ]);
assert.deepStrictEqual(acl['luci-app-l850gl-mm-sms-read'].read.ubus['l850gl.mm'], [
	'list_sms'
]);
assert.deepStrictEqual(acl['luci-app-l850gl-mm-sms-write'].write.ubus['l850gl.mm'], [
	'send_sms', 'delete_sms'
]);
assert.deepStrictEqual(acl['luci-app-l850gl-mm-lock-band'].write.ubus['l850gl.mm'], [
	'set_bands', 'set_modes'
]);
assert.deepStrictEqual(
	acl['luci-app-l850gl-mm-lock-pci-expert'].read.ubus['l850gl.mm.l850'],
	[ 'cell_scan', 'get_carrier_info', 'cell_lock_status' ]);
assert.deepStrictEqual(
	acl['luci-app-l850gl-mm-lock-pci-expert'].write.ubus['l850gl.mm.l850'],
	[ 'set_cell_lock', 'clear_cell_lock' ]);
const aclSource = read(
	'luci-app-l850gl-mm/root/usr/share/rpcd/acl.d/luci-app-l850gl-mm.json');
for (const forbidden of [ '"*"', '"cgi-io"', '"file"', '"uci"', '"exec"' ])
	assert.ok(!aclSource.includes(forbidden), `ACL must not contain ${forbidden}`);

const staticWorkflow = read('.github/workflows/static.yml');
assert.match(staticWorkflow, /run-host-ubus-blob\.sh/);
assert.match(staticWorkflow, /run-host-sms-dedupe\.sh/);
assert.match(staticWorkflow, /run-host-cell-parser\.sh/);
assert.match(staticWorkflow, /run-host-ca-parser\.sh/);
assert.match(staticWorkflow, /run-host-voltage-parser\.sh/);
assert.match(staticWorkflow, /make check/);
const sdkWorkflow = read('.github/workflows/openwrt-sdk.yml');
assert.ok(sdkWorkflow.includes("grep -Fq -- '-Dbuiltin_plugins=true'"));
assert.ok(sdkWorkflow.includes('CONFIG_L850GL_MM_BRIDGE_EXPERT=y'),
	'SDK CI must exercise the explicit expert build separately');
assert.ok(sdkWorkflow.includes('CONFIG_MODEMMANAGER_WITH_AT_COMMAND_VIA_DBUS=y'));
assert.ok(sdkWorkflow.includes('package/feeds/packages/modemmanager/compile'),
	'expert CI must rebuild the matching ModemManager package');
assert.ok(sdkWorkflow.includes("grep -Fq -- '-Dat_command_via_dbus=true'"));
assert.ok(sdkWorkflow.includes('Firmware_Allowlist=18500.5001.00.05.27.30'));
assert.ok(sdkWorkflow.includes("grep -Fx 'get_carrier_info'"),
	'expert SDK verification must retain the typed carrier method');
assert.ok(sdkWorkflow.includes("grep -Fx 'AT+GTCAINFO?'"),
	'base/expert SDK verification must gate the reviewed carrier command');
assert.ok(sdkWorkflow.includes("grep -Fx 'AT+CBC'"),
	'base/expert SDK verification must gate the reviewed voltage command');
assert.strictEqual((sdkWorkflow.match(/API_Schema=4/g) || []).length, 2,
	'both SDK artifact manifests must identify schema 4');
assert.ok(!sdkWorkflow.includes('API_Schema=2'));
assert.ok(!sdkWorkflow.includes('API_Schema=3'));
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
