// SPDX-FileCopyrightText: 2026 As Tsaqib
// SPDX-License-Identifier: Apache-2.0
/* global __dirname, global, require */

'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');

const appRoot = path.resolve(__dirname, '..');
const resources = path.join(appRoot, 'htdocs/luci-static/resources');
const menuPath = path.join(appRoot, 'root/usr/share/luci/menu.d/luci-app-fibocom.json');
const aclPath = path.join(appRoot, 'root/usr/share/rpcd/acl.d/luci-app-fibocom.json');

function read(relativePath) {
	return fs.readFileSync(path.join(appRoot, relativePath), 'utf8');
}

function listFiles(root) {
	if (!fs.existsSync(root))
		return [];

	return fs.readdirSync(root, { withFileTypes: true }).flatMap(function(entry) {
		const absolute = path.join(root, entry.name);

		return entry.isDirectory() ? listFiles(absolute) : [ absolute ];
	});
}

function evaluate(relativePath, dependencies) {
	const names = Object.keys(dependencies);
	const source = read(relativePath);

	return Function.apply(null, names.concat(source)).apply(null, names.map(function(name) {
		return dependencies[name];
	}));
}

const menu = JSON.parse(fs.readFileSync(menuPath, 'utf8'));
const acl = JSON.parse(fs.readFileSync(aclPath, 'utf8'));
const menuRoot = menu['admin/modem/fibocom'];

assert.strictEqual(menu['admin/modem'].action.type, 'firstchild');
assert.deepStrictEqual(menuRoot.depends.acl, [ 'luci-app-fibocom-overview' ]);
assert.strictEqual(menuRoot.action.type, 'firstchild');

const expectedViews = [ 'overview', 'lock', 'sms' ];
expectedViews.forEach(function(name) {
	const entry = menu[`admin/modem/fibocom/${name}`];

	assert.ok(entry, `menu entry for ${name} must exist`);
	assert.strictEqual(entry.action.type, 'view');
	assert.strictEqual(entry.action.path, `fibocom/${name}`);
	assert.ok(fs.existsSync(path.join(resources, 'view/fibocom', `${name}.js`)),
		`${name}.js must exist`);
});
assert.deepStrictEqual(Object.keys(menu).filter(function(key) {
	return key.startsWith('admin/modem/fibocom/');
}).sort(), expectedViews.map(function(name) {
	return `admin/modem/fibocom/${name}`;
}).sort(), 'the package must advertise exactly Overview, Lock, and SMS');
for (const retired of [ 'status', 'advanced', 'settings', 'diagnostics' ]) {
	assert.ok(!fs.existsSync(path.join(resources, 'view/fibocom', `${retired}.js`)),
		`${retired}.js must be retired`);
}
assert.strictEqual(menu['admin/modem/fibocom/overview'].order, 10);
assert.strictEqual(menu['admin/modem/fibocom/lock'].order, 20);
assert.strictEqual(menu['admin/modem/fibocom/sms'].order, 30);
assert.deepStrictEqual(menu['admin/modem/fibocom/lock'].depends.acl,
	[ 'luci-app-fibocom-overview', 'luci-app-fibocom-lock-band' ]);
assert.deepStrictEqual(menu['admin/modem/fibocom/sms'].depends.acl,
	[ 'luci-app-fibocom-sms-read' ]);

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
assert.strictEqual(acl['luci-app-fibocom-sms-read'].write, undefined);
assert.deepStrictEqual(acl['luci-app-fibocom-sms-write'].write.ubus['fibocom.mm'], [
	'send_sms', 'delete_sms'
]);
assert.strictEqual(acl['luci-app-fibocom-sms-write'].read, undefined);
assert.deepStrictEqual(acl['luci-app-fibocom-lock-band'].write.ubus['fibocom.mm'], [
	'set_bands'
]);
assert.deepStrictEqual(
	acl['luci-app-fibocom-lock-pci-expert'].read.ubus['fibocom.mm.l850'],
	[ 'cell_scan', 'cell_lock_status' ]);
assert.deepStrictEqual(
	acl['luci-app-fibocom-lock-pci-expert'].write.ubus['fibocom.mm.l850'],
	[ 'set_cell_lock', 'clear_cell_lock' ]);
const aclSource = fs.readFileSync(aclPath, 'utf8');
[ 'cgi-io', 'file', 'uci', 'exec', 'shell' ].forEach(function(forbidden) {
	assert.ok(!aclSource.includes(`"${forbidden}"`), `ACL must not grant ${forbidden}`);
});
assert.ok(!aclSource.includes('"*"'), 'ACL must not grant wildcard access');

const declarations = [];
const rpc = {
	declare: function(specification) {
		declarations.push(specification);

		return function() {
			return { specification: specification, arguments: Array.from(arguments) };
		};
	}
};
const baseclass = { extend: function(specification) { return specification; } };
const api = evaluate('htdocs/luci-static/resources/fibocom/api.js', { baseclass, rpc });

assert.strictEqual(api.SCHEMA_VERSION, 2);
assert.deepStrictEqual(declarations.map(function(call) {
	return `${call.object}.${call.method}`;
}), [
	'fibocom.mm.list_modems',
	'fibocom.mm.get_overview',
	'fibocom.mm.get_lock_status',
	'fibocom.mm.set_bands',
	'fibocom.mm.list_sms',
	'fibocom.mm.send_sms',
	'fibocom.mm.delete_sms',
	'fibocom.mm.l850.cell_scan',
	'fibocom.mm.l850.cell_lock_status',
	'fibocom.mm.l850.set_cell_lock',
	'fibocom.mm.l850.clear_cell_lock'
]);
assert.ok(declarations.every(function(call) { return call.reject === true; }),
	'ubus and ACL errors must reject instead of looking like an empty snapshot');
assert.strictEqual(declarations[0].params, undefined);
assert.deepStrictEqual(declarations[1].params, [ 'modem_id' ]);
assert.deepStrictEqual(declarations[2].params, [ 'modem_id' ]);
assert.deepStrictEqual(declarations[3].params,
	[ 'modem_id', 'generation', 'bands', 'confirm' ]);
assert.deepStrictEqual(declarations[4].params,
	[ 'modem_id', 'folder', 'limit', 'cursor' ]);
assert.deepStrictEqual(declarations[5].params, [
	'modem_id', 'generation', 'messaging_generation', 'recipient', 'text', 'client_token'
]);
assert.deepStrictEqual(declarations[6].params, [
	'modem_id', 'generation', 'messaging_generation', 'sms_id', 'confirm'
]);
assert.deepStrictEqual(declarations[7].params, [ 'modem_id', 'generation' ]);
assert.deepStrictEqual(declarations[8].params, [ 'modem_id', 'generation' ]);
assert.deepStrictEqual(declarations[9].params,
	[ 'modem_id', 'generation', 'earfcn', 'pci', 'confirm' ]);
assert.deepStrictEqual(declarations[10].params,
	[ 'modem_id', 'generation', 'confirm' ]);
assert.deepStrictEqual(api.listModems().arguments, []);
assert.deepStrictEqual(api.getOverview('fibocom-test').arguments, [ 'fibocom-test' ]);
assert.deepStrictEqual(api.getLockStatus('fibocom-test').arguments, [ 'fibocom-test' ]);
assert.deepStrictEqual(api.setBands('fibocom-test', 4, [ 'eutran-1' ], true).arguments,
	[ 'fibocom-test', 4, [ 'eutran-1' ], true ]);
assert.deepStrictEqual(api.listSms('fibocom-test', 'inbox', 100, 'next').arguments,
	[ 'fibocom-test', 'inbox', 100, 'next' ]);
assert.deepStrictEqual(api.cellScan('fibocom-test', 4).arguments,
	[ 'fibocom-test', 4 ]);
assert.deepStrictEqual(api.cellLockStatus('fibocom-test', 4).arguments,
	[ 'fibocom-test', 4 ]);
assert.deepStrictEqual(api.setCellLock('fibocom-test', 4, 1650, 0, true).arguments,
	[ 'fibocom-test', 4, 1650, 0, true ]);
assert.deepStrictEqual(api.clearCellLock('fibocom-test', 4, true).arguments,
	[ 'fibocom-test', 4, true ]);
for (const retired of [ 'getStatus', 'getCapabilities', 'setRadio', 'reset', 'setPrimarySimSlot' ])
	assert.strictEqual(api[retired], undefined, `${retired} must be retired from the UI API`);

global._ = function(value) { return value; };
global.E = function(tag, attributes, children) {
	return {
		tag,
		nodeType: 1,
		attributes: attributes || {},
		children: children || []
	};
};
global.L = { url: function() { return Array.from(arguments).join('/'); } };

if (!String.prototype.format) {
	Object.defineProperty(String.prototype, 'format', {
		value: function() {
			const values = arguments;
			let offset = 0;

			return String(this).replace(/%(?:s|d)/g, function() {
				return String(values[offset++]);
			});
		}
	});
}

function renderedText(node) {
	const values = [];

	(function collect(value) {
		if (Array.isArray(value))
			value.forEach(collect);
		else if (value != null && typeof value === 'object' && Array.isArray(value.children))
			value.children.forEach(collect);
		else if (typeof value === 'string' || typeof value === 'number')
			values.push(String(value));
	})(node);

	return values;
}

const widgets = evaluate('htdocs/luci-static/resources/fibocom/widgets.js', { baseclass });
assert.strictEqual(widgets.isCompatible({ schema: 2, ok: true }), true);
assert.strictEqual(widgets.isCompatible({ schema: 1, ok: true }), false);
assert.strictEqual(widgets.isCompatible({ schema: 2, ok: 'yes' }), false);
assert.deepStrictEqual(widgets.modems({ schema: 2, ok: true, modems: [] }), []);
assert.deepStrictEqual(widgets.modems({ schema: 1, ok: true, modems: [] }), []);
assert.match(widgets.responseError({ schema: 1, ok: true }), /schema/i);
assert.match(widgets.responseError({ schema: 2, ok: true }), /malformed/i,
	'a partial success response must fail closed');
assert.strictEqual(widgets.mutationAllowed({ schema: 2, generated_at: 1, ok: true }, {
	modem_id: 'fibocom-test', generation: 4
}, 'fibocom-test', 4), true);
assert.strictEqual(widgets.mutationAllowed({ schema: 3, generated_at: 1, ok: true }, {
	modem_id: 'fibocom-test', generation: 4
}, 'fibocom-test', 4), false);

const view = { extend: function(specification) { return specification; } };
const inert = new Proxy({}, { get: function() { return function() {}; } });
const viewDependencies = {
	dom: inert,
	poll: inert,
	ui: inert,
	view,
	api: inert,
	widgets
};

expectedViews.forEach(function(name) {
	const relative = `htdocs/luci-static/resources/view/fibocom/${name}.js`;
	const source = read(relative);
	const dependencies = {};

	[ 'dom', 'poll', 'ui', 'view', 'api', 'widgets' ].forEach(function(dependency) {
		dependencies[dependency] = viewDependencies[dependency];
	});
	assert.ok(evaluate(relative, dependencies), `${name}.js must evaluate as a LuCI module`);
	assert.ok(!source.includes('rpc.declare'), `${name}.js must use the shared API module`);
	assert.ok(!source.includes('innerHTML'), `${name}.js must not inject HTML strings`);
	assert.ok(!source.includes('localStorage'), `${name}.js must not persist private UI state`);
	assert.doesNotMatch(source, /\bconsole\s*\./,
		`${name}.js must not log private modem or SMS data`);
	assert.match(source, /poll\.add\([\s\S]*?,\s*10\s*\)/,
		`${name}.js must poll typed snapshots every ten seconds`);
});

const summary = {
	modem_id: 'fibocom-test',
	generation: 4,
	manufacturer: 'Fibocom Wireless Inc.',
	model: 'L850-GL',
	revision: '18500.test',
	state: 'connected',
	power: 'on'
};
const listResult = {
	schema: 2,
	generated_at: 1,
	ok: true,
	modems: [ summary ]
};
const overviewResult = {
	schema: 2,
	generated_at: 1,
	ok: true,
	modem_id: 'fibocom-test',
	generation: 4,
	identity: {
		manufacturer: 'Fibocom Wireless Inc.', model: 'L850-GL', revision: '18500.test'
	},
	modem: { state: 'connected', power: 'on' },
	sim: { present: true, lock: 'none' },
	network: {
		operator: 'Example', registration: 'home', roaming: false, access: [ 'lte' ]
	},
	signal: { quality: 72, recent: true, rsrp: -90, rsrq: -10, sinr: 14 },
	bearer: { connected: true, interface: 'wwan0' },
	current_bands: [ 'eutran-1', 'eutran-3' ],
	serving_cell: { state: 'unavailable', reason: 'not-validated' },
	capabilities: {
		sms: { state: 'available', mutable: true },
		band_lock: { state: 'available', mutable: true },
		pci_lock: { state: 'unsupported_build', mutable: false }
	},
	warnings: []
};
const lockResult = {
	schema: 2,
	generated_at: 1,
	ok: true,
	modem_id: 'fibocom-test',
	generation: 4,
	supported_bands: [ 'eutran-1', 'eutran-3', 'eutran-8' ],
	current_bands: [ 'eutran-1', 'eutran-3' ],
	band_selection: 'explicit',
	current_modes: { allowed: [ '4g' ], preferred: 'none' },
	band_lock: { state: 'available', mutable: true, retry_after_ms: 0 },
	pci_lock: { state: 'unsupported_build', mutable: false, reason: 'expert-object-absent' }
};
const smsResult = {
	schema: 2,
	generated_at: 1,
	ok: true,
	modem_id: 'fibocom-test',
	generation: 4,
	messaging_generation: 7,
	revision: 1,
	cache_state: 'fresh',
	dedupe_capacity: 64,
	dedupe_window_seconds: 300,
	messages: [],
	next_cursor: 'sms-next',
	has_more: true
};
const expertResult = {
	schema: 2,
	generated_at: 1,
	ok: false,
	modem_id: 'fibocom-test',
	generation: 4,
	state: 'unsupported_firmware',
	error: {
		code: 'unsupported_firmware',
		message: 'Firmware is not in the live-validated allowlist',
		retryable: false
	}
};

const overviewView = evaluate(
	'htdocs/luci-static/resources/view/fibocom/overview.js', viewDependencies);
const lockView = evaluate(
	'htdocs/luci-static/resources/view/fibocom/lock.js', viewDependencies);
const smsView = evaluate(
	'htdocs/luci-static/resources/view/fibocom/sms.js', viewDependencies);

assert.strictEqual(overviewView.render({
	list: listResult,
	entries: [ { summary: summary, overview: overviewResult } ]
}).tag, 'div');
assert.strictEqual(lockView.render({
	list: listResult,
	entries: [ { summary: summary, lock: lockResult, expert: expertResult } ]
}).tag, 'div');
const renderedSms = smsView.render({
	list: listResult,
	entries: [ { summary: summary, messages: smsResult } ]
});
assert.strictEqual(renderedSms.tag, 'div');
assert.ok(renderedText(renderedSms).includes('Load more'));
const binarySms = Object.assign({}, smsResult, {
	has_more: false,
	next_cursor: '',
	messages: [ {
		sms_id: 'sms-binary', folder: 'inbox', direction: 'incoming',
		state: 'received', number: '<redacted-number>', text: '',
		text_truncated: false, timestamp: '2026-07-19T12:00:00Z',
		discharge_timestamp: '', pdu_type: 'deliver', delivery_state: 0,
		message_reference: 1, storage: 'mt', has_binary_data: true
	} ]
});
const binarySmsText = renderedText(smsView.render({
	list: listResult,
	entries: [ { summary: summary, messages: binarySms } ]
}));
assert.ok(binarySmsText.includes('Binary SMS payload (not displayed)'));
assert.ok(binarySmsText.includes(
	'This SMS contains binary data. Its raw payload is intentionally not exposed or displayed.'));
const incompatibleOverview = renderedText(overviewView.render({
	list: Object.assign({}, listResult, { schema: 1 }), entries: []
}));
assert.ok(incompatibleOverview.some(function(value) { return /schema/i.test(value); }));

[ overviewView, lockView, smsView ].forEach(function(module) {
	assert.strictEqual(module.handleSaveApply, null);
	assert.strictEqual(module.handleSave, null);
	assert.strictEqual(module.handleReset, null);
});

const frontendSources = listFiles(resources).filter(function(file) {
	return file.endsWith('.js');
});
frontendSources.forEach(function(file) {
	const source = fs.readFileSync(file, 'utf8');
	const licenseMarker = [ 'SPDX-License-Identifier', 'Apache-2.0' ].join(': ');

	assert.ok(source.includes(licenseMarker),
		`${path.relative(appRoot, file)} must carry an Apache-2.0 SPDX tag`);
	assert.match(source, /'use strict';/);
	assert.doesNotMatch(source, /['"]require\s+(?:fs|uci|network|form)(?:\s|['"])/);
	assert.doesNotMatch(source, /\b(?:exec_direct|cgi-io|mmcli|mbimcli|uqmi|lpac)\b/i);
	assert.doesNotMatch(source, /JSON\.stringify\s*\(/,
		`${path.relative(appRoot, file)} must render allowlisted typed fields, not raw JSON`);
	assert.doesNotMatch(source, /(?:innerHTML|outerHTML|insertAdjacentHTML|document\.write)/);
	assert.doesNotMatch(source, /\blocalStorage\b/);
});

const overviewSource = read('htdocs/luci-static/resources/view/fibocom/overview.js');
for (const forbidden of [
	'ports', 'drivers', 'device_path', 'dbus_path', 'sysfs',
	'ip_address', 'gateway', 'dns', 'credentials', 'diagnostics'
]) {
	assert.ok(!overviewSource.includes(forbidden),
		`Overview must not render diagnostic/private field ${forbidden}`);
}
assert.ok(!overviewSource.includes('rescan'));

const smsSource = read('htdocs/luci-static/resources/view/fibocom/sms.js');
assert.ok(smsSource.includes('ui.showModal'));
assert.ok(smsSource.includes('window.crypto'));
assert.ok(smsSource.includes('getRandomValues'));
assert.ok(smsSource.includes("let token = 'smsop-';"));
assert.ok(smsSource.includes('text_truncated'));
assert.ok(smsSource.includes("[ 'outcome_unknown', 'timeout', 'busy' ]"));
assert.ok(smsSource.includes('next_cursor'));
assert.ok(smsSource.includes('has_more'));
assert.ok(smsSource.includes("_('Load more')"));
assert.ok(smsSource.includes('dedupe_capacity'));
assert.ok(smsSource.includes('dedupe_window_seconds'));
assert.ok(smsSource.includes('const SMS_CACHE_MAX = 1024'));
assert.ok(smsSource.includes('state.pageCount >= MAX_SMS_PAGES'));
assert.ok(smsSource.includes('redrawPending'));
assert.ok(smsSource.includes("'focusout'"));
assert.doesNotMatch(smsSource, /\.slice\s*\(\s*0\s*,/,
	'SMS text must never be truncated before display or sending');

const lockSource = read('htdocs/luci-static/resources/view/fibocom/lock.js');
assert.ok(lockSource.includes('ui.showModal'));
assert.ok(lockSource.includes("[ 'any' ]"));
assert.ok(lockSource.includes('api.setBands'));
assert.ok(lockSource.includes('api.cellScan'));
assert.ok(lockSource.includes('api.cellLockStatus'));
assert.ok(lockSource.includes('api.setCellLock'));
assert.ok(lockSource.includes('api.clearCellLock'));
assert.ok(lockSource.includes('expertScanContext'));
assert.ok(lockSource.includes("scan.available !== true"));
assert.ok(lockSource.includes("result.state !== 'scan_ready'"));
assert.ok(lockSource.includes("result.source !== 'modemmanager'"));
assert.ok(lockSource.includes("'unsupported_build'"));
assert.ok(lockSource.includes("'unsupported_firmware'"));
assert.ok(lockSource.includes("error.code === 'outcome_unknown'"));
assert.ok(lockSource.includes('do not retry until the live modem state confirms'));
assert.match(lockSource, /api\.setBands\([\s\S]*?bands, true\)/);
assert.match(lockSource, /api\.setCellLock\([\s\S]*?true\)/);
assert.match(lockSource, /api\.clearCellLock\([\s\S]*?true\)/);
assert.doesNotMatch(lockSource, /setRadio|setPrimarySimSlot|api\.reset/);
assert.doesNotMatch(lockSource, /raw\s*at|\/dev\/|tty(?:USB|ACM)|cdc-wdm|dbus|sysfs/i);

for (const file of frontendSources) {
	const source = fs.readFileSync(file, 'utf8');
	assert.ok(!source.includes("_(' (active)')"));
	assert.ok(!source.includes("_(' (not present)')"));
}
assert.ok(read('htdocs/luci-static/resources/fibocom/widgets.js').includes(
	"' ' + _('(active)')"));

const makefile = read('Makefile');
for (const dependency of [
	'@MODEMMANAGER_WITH_MBIM', '@MODEMMANAGER_WITH_NETIFD', '+luci-base',
	'+fibocom-mm-bridge', '+modemmanager', '+luci-proto-modemmanager',
	'+kmod-usb-acm', '+kmod-usb-net-cdc-mbim', '+kmod-usb-wdm'
]) {
	assert.ok(makefile.includes(dependency), `Makefile must include ${dependency}`);
}
assert.ok(makefile.includes('PKG_LICENSE:=Apache-2.0'));
assert.ok(makefile.includes('include $(TOPDIR)/feeds/luci/luci.mk'));

const pot = read('po/templates/fibocom.pot');
for (const retired of [
	'Status', 'Advanced', 'Settings', 'Radio power', 'Reset modem', 'Primary SIM slot',
	'Rescan devices', 'Shadow mode', 'eSIM'
]) {
	assert.ok(!pot.includes(`msgid "${retired}"`),
		`translation template must not contain retired msgid: ${retired}`);
}
for (const text of [
	'Overview', 'Lock', 'SMS', 'Band Lock', 'PCI/EARFCN Lock', 'Load more',
	'Compose SMS', 'Delete SMS', '(active)'
]) {
	assert.ok(pot.includes(`msgid "${text}"`),
		`translation template must contain: ${text}`);
}
assert.strictEqual(listFiles(appRoot).filter(function(file) {
	return file.endsWith('.lua');
}).length, 0, 'legacy Lua controllers and models are forbidden');

console.log('luci-app-fibocom static checks: OK');
