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
	return fs.readFileSync(absolute(relativePath), 'utf8').replace(/\r\n/g, '\n');
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
		`${retired} must remain retired from the 1.0 product tree`);
}

const bridgeMakefile = read('l850gl-mm-bridge/Makefile');
assert.match(bridgeMakefile, /^PKG_NAME:=l850gl-mm-bridge$/m);
assert.match(bridgeMakefile, /^PKG_VERSION:=1\.0\.0$/m);
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
assert.doesNotMatch(bridgeMakefile, /depends on PACKAGE_l850gl-mm-bridge/,
	'the package-scoped expert option must not depend recursively on itself');
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
const l850MutationPolicySource = read(
	'l850gl-mm-bridge/src/l850_mutation_policy.c');
const l850MutationPolicyHeader = read(
	'l850gl-mm-bridge/src/l850_mutation_policy.h');
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

const mmCore124Compatibility = ubusSource.match(
	/static gboolean\nmm_core_error_is_timeout[\s\S]*?\n}\n\nstatic gboolean\nmm_core_error_is_throttled[\s\S]*?\n}/);
assert.ok(mmCore124Compatibility,
	'libmm-glib 1.24-only core errors must have one compatibility boundary');
assert.strictEqual((mmCore124Compatibility[0].match(
	/#if MM_CHECK_VERSION\(1, 24, 0\)/g) || []).length, 2,
	'both 1.24-only core errors must be compile-time version guarded');
assert.strictEqual((mmCore124Compatibility[0].match(
	/MM_CORE_ERROR_TIMEOUT/g) || []).length, 1);
assert.strictEqual((mmCore124Compatibility[0].match(
	/MM_CORE_ERROR_THROTTLED/g) || []).length, 1);
assert.doesNotMatch(ubusSource.replace(mmCore124Compatibility[0], ''),
	/MM_CORE_ERROR_(?:TIMEOUT|THROTTLED)/,
	'1.24-only enum constants must not escape their compatibility boundary');
assert.match(ubusSource,
	/g_error_matches\(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT\)/,
	'1.22 builds must retain the GIO timeout fallback');
assert.match(ubusSource, /remote_error_has_suffix\(remote, "\.Core\.Timeout"\)/,
	'1.22 builds must retain the remote timeout-name fallback');
assert.match(ubusSource,
	/remote_error_has_suffix\(remote, "\.Core\.Throttled"\)/,
	'1.22 builds must retain the remote throttled-name fallback');

assert.match(bridgeHeader, /#define L850GL_MM_API_SCHEMA 4U/);
assert.match(bridgeHeader, /#define L850GL_MM_BRIDGE_VERSION "1\.0\.0"/);
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
	/sinr == CA_SINR_SENTINEL && !allow_unavailable_sinr/,
	'the live unavailable SINR sentinel must be scoped to reviewed secondary records');
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
	'valid-live-secondary-sinr-unavailable.txt',
	'valid-inactive-before-primary.txt',
	'invalid-secondary-copied-ul-mismatch.txt',
	'invalid-inactive-copied-ul-mismatch.txt',
	'invalid-unverified-secondary-uplink.txt',
	'invalid-primary-ul-sentinel.txt',
	'invalid-primary-sinr-unavailable.txt',
	'invalid-secondary-sinr-255.txt',
	'invalid-secondary-sinr-128.txt',
	'invalid-secondary-sinr-unavailable-pci.txt'
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
assert.match(sourceMakefile, /\bl850_mutation_policy\.c\b/,
	'the NVM verification policy must be built into the bridge');
assert.match(l850MutationPolicyHeader,
	/#define L850GL_NVM_PRE_RESET_REQUIRED_MATCHES 2U/,
	'pre-reset verification must require two consecutive NVM matches');
assert.match(l850MutationPolicyHeader,
	/#define L850GL_NVM_POST_RESET_REQUIRED_MATCHES 1U/,
	'post-reset verification must accept one matching NVM observation');
assert.match(l850MutationPolicySource,
	/observation == L850GL_NVM_OBSERVATION_MISMATCH[\s\S]*?consecutive_matches = 0U/,
	'a mismatch must reset the consecutive pre-reset match streak');
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
assert.match(ubusSource,
	/#define L850_NVM_COMMAND_TIMEOUT_SECONDS 5U/,
	'NVM verification reads must use a short bounded command timeout');
assert.match(ubusSource,
	/#define L850_NVM_VERIFY_TIMEOUT_SECONDS 10U/,
	'each NVM verification stage must have a bounded ten-second window');
assert.match(ubusSource, /#define L850_NVM_VERIFY_POLL_MS 1000U/,
	'NVM verification retries must be spaced by one second');
assert.match(l850MutationPolicyHeader,
	/#define L850GL_NVM_PRE_RESET_REQUIRED_MATCHES 2U/,
	'pre-reset persistence requires two consecutive exact NVM reads');
assert.match(l850MutationPolicyHeader,
	/#define L850GL_NVM_POST_RESET_REQUIRED_MATCHES 1U/,
	'post-reset persistence requires one exact fresh NVM read');
const mutationSetReadyStart = ubusSource.indexOf(
	'\nstatic void\nl850_mutation_set_ready(');
const mutationSetReadyEnd = ubusSource.indexOf(
	'\nstatic gboolean\nl850_modem_is_registered(', mutationSetReadyStart);
assert.ok(mutationSetReadyStart > 0 &&
	mutationSetReadyEnd > mutationSetReadyStart,
	'the PCI command acknowledgement callback must be discoverable');
const mutationSetReady = ubusSource.slice(mutationSetReadyStart,
	mutationSetReadyEnd);
assert.match(mutationSetReady,
	/operation->configuration_acknowledged\s*=\s*TRUE;[\s\S]*?L850GL_NVM_VERIFY_PRE_RESET/,
	'exact acknowledgement must enter pre-reset NVM verification');
assert.doesNotMatch(mutationSetReady, /l850_mutation_start_reset\s*\(/,
	'acknowledgement alone must never dispatch reset');
const mutationNvmReadyStart = ubusSource.lastIndexOf(
	'\nstatic void\nl850_mutation_nvm_ready(');
const mutationNvmReadyEnd = ubusSource.indexOf(
	'\nstatic gboolean\nl850_mutation_timeout(', mutationNvmReadyStart);
assert.ok(mutationNvmReadyStart > 0 &&
	mutationNvmReadyEnd > mutationNvmReadyStart,
	'the concrete NVM verification callback must be discoverable');
const mutationNvmReady = ubusSource.slice(mutationNvmReadyStart,
	mutationNvmReadyEnd);
assert.match(mutationNvmReady, /l850gl_nvm_verifier_observe\s*\(/);
assert.match(mutationNvmReady,
	/g_timeout_add\(L850_NVM_VERIFY_POLL_MS,[\s\S]*?l850_mutation_poll/,
	'valid NVM mismatch must use the bounded coordinator poll');
assert.match(mutationNvmReady,
	/decision == L850GL_NVM_DECISION_RETRY[\s\S]*?return;/,
	'NVM mismatch must retry only the read-only verification query');
assert.match(mutationNvmReady,
	/operation->verification_stage = NULL;[\s\S]*?if \(pre_reset\)[\s\S]*?l850_mutation_start_reset/,
	'reset may start only after the pre-reset verifier is ready');
assert.strictEqual((ubusSource.match(
	/l850_mutation_start_reset\(operation\);/g) || []).length, 1,
	'the coordinator must have exactly one reset transition and no fallback');
assert.match(ubusSource, /"pre_reset_nvm"\s*:\s*"post_reset_nvm"/,
	'failures must identify the bounded NVM verification stage');
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
	'tests/host-cell-mutation-policy.c',
	'tests/run-host-cell-mutation-policy.sh',
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
assert.match(luciMakefile, /^PKG_VERSION:=1\.0\.0$/m);
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
assert.match(staticWorkflow, /run-host-cell-mutation-policy\.sh/);
assert.match(staticWorkflow, /run-host-ca-parser\.sh/);
assert.match(staticWorkflow, /run-host-voltage-parser\.sh/);
assert.match(staticWorkflow, /make check/);
const sdkWorkflow = read('.github/workflows/openwrt-sdk.yml');
const releaseWorkflow = read('.github/workflows/release-bundle.yml');
const expertRecipeTransformer = read('packaging/prepare-modemmanager-expert.py');
const expertRecipeTransformerTests = read(
	'tests/test-prepare-modemmanager-expert.py');
assert.ok(fs.existsSync(absolute('tests/test-prepare-modemmanager-expert.py')),
	'the expert recipe transformer must have behavioral tests');
for (const contract of [
	'EXPERT_PACKAGE = "modemmanager-l850gl-expert"',
	'PROVIDES:=modemmanager',
	'CONFLICTS:=modemmanager',
	'for suffix in ("config", "description", "install")',
	'PACKAGE_{EXPERT_PACKAGE}',
	'stock ModemManager binary package remains after rename',
	'stock ModemManager binary output remains registered'
])
	assert.ok(expertRecipeTransformer.includes(contract),
		`expert-only ModemManager rename must retain ${contract}`);
for (const recipe of [ '1.22.0', '20', '1.24.0', '10' ])
	assert.ok(expertRecipeTransformer.includes(`"${recipe}"`));
assert.ok(expertRecipeTransformer.includes(
	'expected exactly one Config.in dependency on PACKAGE_modemmanager'));
for (const contract of [
	'assertNotIn("define Package/modemmanager\\n", transformed)',
	'depends on PACKAGE_modemmanager-l850gl-expert'
])
	assert.ok(expertRecipeTransformerTests.includes(contract),
		`expert rename tests must prove ${contract}`);
assert.match(expertRecipeTransformerTests,
	/assertNotIn\(\s*"\$\(eval \$\(call BuildPackage,modemmanager\)\)",\s*transformed\s*\)/,
	'expert rename tests must prove the stock binary output is absent');
assert.ok(sdkWorkflow.includes("grep -Fq -- '-Dbuiltin_plugins=true'"));
assert.ok(!sdkWorkflow.includes('prepare-modemmanager-expert.py'),
	'the base SDK workflow must not transform the stock ModemManager recipe');
assert.ok(sdkWorkflow.includes(
	'The base workflow must use the unmodified stock recipe'));
assert.ok(sdkWorkflow.includes('error: recursive dependency detected!'),
	'SDK CI must fail closed on recursive Kconfig diagnostics');
assert.ok(sdkWorkflow.includes(
	'# CONFIG_MODEMMANAGER_WITH_AT_COMMAND_VIA_DBUS is not set'));
assert.ok(sdkWorkflow.includes('# CONFIG_L850GL_MM_BRIDGE_EXPERT is not set'));
for (const expertBuildContract of [
	'package/feeds/packages/modemmanager/compile',
	'dist/expert',
	'Firmware_Allowlist=',
	'apk add --simulate --allow-untrusted'
])
	assert.ok(!sdkWorkflow.includes(expertBuildContract),
		`base SDK workflow must not contain ${expertBuildContract}`);
for (const enabledExpertConfig of [
	'CONFIG_MODEMMANAGER_WITH_AT_COMMAND_VIA_DBUS=y',
	'CONFIG_L850GL_MM_BRIDGE_EXPERT=y',
	'CONFIG_PACKAGE_modemmanager-l850gl-expert=m'
])
	assert.doesNotMatch(sdkWorkflow,
		new RegExp(`^\\s+${enabledExpertConfig}$`, 'm'),
		`base SDK workflow must not enable ${enabledExpertConfig}`);
assert.ok(sdkWorkflow.includes("grep -Fx 'AT+GTCAINFO?'"),
	'base SDK verification must reject the expert carrier command');
assert.ok(sdkWorkflow.includes("grep -Fx 'AT+CBC'"),
	'base SDK verification must reject the expert voltage command');
assert.ok(sdkWorkflow.includes('Expert_Object=absent'));
assert.strictEqual((sdkWorkflow.match(/API_Schema=4/g) || []).length, 1,
	'the base SDK artifact manifest must identify schema 4 exactly once');
assert.strictEqual((sdkWorkflow.match(/Release=1\.0\.0-r2/g) || []).length, 1,
	'the base SDK artifact manifest must identify release 1.0.0-r2 exactly once');
assert.ok(!sdkWorkflow.includes('Release=0.6.0-r6'),
	'the final SDK workflow must not label new artifacts as the previous release');
assert.ok(!sdkWorkflow.includes('API_Schema=2'));
assert.ok(!sdkWorkflow.includes('API_Schema=3'));
assert.ok(!sdkWorkflow.includes('luci-app-fibocom-esim'));
assert.ok(!sdkWorkflow.includes('luci-app-lpac'));
assert.ok(!sdkWorkflow.includes('LPAC_'));

for (const arch of [
	'aarch64_generic',
	'aarch64_cortex-a53',
	'arm_cortex-a7_neon-vfpv4',
	'mips_24kc',
	'mipsel_24kc',
	'x86_64'
]) {
	assert.ok(releaseWorkflow.includes(arch),
		`release matrix must include canonical architecture ${arch}`);
}
for (const typo of [
	'aarch64_cortex_a53', 'arm_cortex_a7-neon-vfvp4', 'mipsl_24kc', 'x86-64'
]) {
	assert.ok(!releaseWorkflow.includes(typo),
		`release matrix must not use non-canonical architecture ${typo}`);
}
assert.ok(releaseWorkflow.includes('24.10.8'));
assert.ok(releaseWorkflow.includes('25.12.5'));
assert.ok(!releaseWorkflow.includes('24.10.7'));
assert.ok(releaseWorkflow.includes(
	'while (field_index <= NF && $field_index ~ /^--[^[:space:]]+$/)'),
	'release feed validation must accept pinned SDK feeds with options');
assert.ok(releaseWorkflow.includes('touch .config'));
assert.ok(releaseWorkflow.includes(
	'printf \'CONFIG_%s=y\\n\' "${TARGET_SYMBOL}" "${SUBTARGET_SYMBOL}" >> .config'),
	'release configuration must initialize target-specific SDKs without .config');
assert.ok(releaseWorkflow.includes('23abaa6f3b0fdfd76b570031107e5718476ff0c8'));
assert.ok(releaseWorkflow.includes('d011c4fb8af70795928937ad5195479cc4ff6de9'));
assert.ok(releaseWorkflow.includes("package_format='ipk'"));
assert.ok(releaseWorkflow.includes("package_format='apk'"));
assert.ok(releaseWorkflow.includes('CONFIG_PACKAGE_modemmanager-l850gl-expert=m'));
assert.ok(releaseWorkflow.includes("grep -Fqx 'CONFIG_L850GL_MM_BRIDGE_EXPERT=y'"));
assert.ok(releaseWorkflow.includes('recursive dependency'),
	'release builds must fail on every recursive Kconfig diagnostic');
assert.ok(releaseWorkflow.includes('apk del modemmanager'));
assert.ok(releaseWorkflow.includes('opkg remove --force-depends modemmanager'));
assert.ok(releaseWorkflow.includes('apk add --simulate --allow-untrusted'));
assert.ok(releaseWorkflow.includes('opkg --noaction install'));
assert.ok(releaseWorkflow.includes(
	'apk del luci-app-l850gl-mm l850gl-mm-bridge modemmanager-l850gl-expert'));
assert.ok(releaseWorkflow.includes(
	'opkg remove --force-depends luci-app-l850gl-mm l850gl-mm-bridge modemmanager-l850gl-expert'));
assert.ok(releaseWorkflow.includes('ModemManager_Recipe_Commit='));
assert.ok(releaseWorkflow.includes('Expert_Firmware_Allowlist=all'));
assert.ok(releaseWorkflow.includes('[ "$(wc -l < actual-assets.txt)" -eq 12 ]'),
	'release publication must require the exact twelve-bundle Cartesian matrix');
assert.ok(releaseWorkflow.includes('sha256sum -c SHA256SUMS'));
assert.ok(releaseWorkflow.includes('contents: write'));
assert.ok(releaseWorkflow.includes("github.ref == 'refs/tags/v1.0.0'"),
	'only the exact final tag may publish hard-coded 1.0.0 assets');
assert.ok(releaseWorkflow.includes('prerelease: false'));
assert.ok(!releaseWorkflow.includes('dist/base'),
	'GitHub Release assets must never include the verification-only base build');

const liveFixture = JSON.parse(read('tests/fixtures/live/l850-mbim-connected.json'));
assert.strictEqual(liveFixture.hardware.model, 'L850-GL');
assert.strictEqual(liveFixture.hardware.composition, 'mbim');
assert.strictEqual(liveFixture.modemmanager.state, 'connected');
assert.strictEqual(liveFixture.openwrt.protocol, 'modemmanager');
assert.ok(Object.values(liveFixture.privacy).every(function(value) {
	return value === 'redacted';
}), 'live fixture privacy fields must remain redacted');

console.log('package contract validation passed');
