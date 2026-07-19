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
const statusAcl = acl['luci-app-fibocom-status'];
const smsReadAcl = acl['luci-app-fibocom-sms-read'];
const smsWriteAcl = acl['luci-app-fibocom-sms-write'];
const radioAcl = acl['luci-app-fibocom-radio'];

assert.strictEqual(menu['admin/modem'].action.type, 'firstchild');
assert.deepStrictEqual(menuRoot.depends.acl, [ 'luci-app-fibocom-status' ]);
assert.strictEqual(menuRoot.action.type, 'firstchild');

const expectedViews = [ 'overview', 'status', 'sms', 'advanced', 'settings' ];
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
}).sort(), 'menu must advertise exactly the implemented base-package tabs');
assert.ok(!fs.existsSync(path.join(resources, 'view/fibocom/diagnostics.js')));
assert.strictEqual(menu['admin/modem/fibocom/sms'].order, 30);
assert.deepStrictEqual(menu['admin/modem/fibocom/sms'].depends.acl,
	[ 'luci-app-fibocom-sms-read' ]);
assert.strictEqual(menu['admin/modem/fibocom/advanced'].order, 40);
assert.deepStrictEqual(menu['admin/modem/fibocom/advanced'].depends.acl,
	[ 'luci-app-fibocom-status', 'luci-app-fibocom-radio' ]);

assert.deepStrictEqual(Object.keys(acl).sort(), [
	'luci-app-fibocom-radio',
	'luci-app-fibocom-sms-read',
	'luci-app-fibocom-sms-write',
	'luci-app-fibocom-status'
]);
assert.deepStrictEqual(Object.keys(statusAcl), [ 'description', 'read' ]);
assert.deepStrictEqual(Object.keys(statusAcl.read), [ 'ubus' ]);
assert.deepStrictEqual(Object.keys(statusAcl.read.ubus), [ 'fibocom.mm' ]);
assert.deepStrictEqual(statusAcl.read.ubus['fibocom.mm'].slice().sort(), [
	'get_capabilities', 'get_overview', 'get_status', 'list_modems'
]);
assert.deepStrictEqual(smsReadAcl.read.ubus['fibocom.mm'], [ 'list_sms' ]);
assert.strictEqual(smsReadAcl.write, undefined);
assert.deepStrictEqual(smsWriteAcl.write.ubus['fibocom.mm'].slice().sort(), [
	'delete_sms', 'send_sms'
]);
assert.strictEqual(smsWriteAcl.read, undefined);
assert.deepStrictEqual(Object.keys(radioAcl), [ 'description', 'write' ]);
assert.deepStrictEqual(Object.keys(radioAcl.write), [ 'ubus' ]);
assert.deepStrictEqual(Object.keys(radioAcl.write.ubus), [ 'fibocom.mm' ]);
assert.deepStrictEqual(radioAcl.write.ubus['fibocom.mm'].slice().sort(), [
	'reset', 'set_bands', 'set_primary_sim_slot', 'set_radio'
]);
assert.strictEqual(radioAcl.read, undefined);

const aclSource = fs.readFileSync(aclPath, 'utf8');
[ 'cgi-io', 'file', 'uci', 'exec' ].forEach(function(forbidden) {
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

assert.strictEqual(api.SCHEMA_VERSION, 1);
assert.deepStrictEqual(declarations.map(function(call) { return call.object; }),
	Array(11).fill('fibocom.mm'));
assert.deepStrictEqual(declarations.map(function(call) { return call.method; }),
	[ 'list_modems', 'get_overview', 'get_status', 'get_capabilities',
		'set_bands', 'set_radio', 'reset', 'set_primary_sim_slot',
		'list_sms', 'send_sms', 'delete_sms' ]);
assert.ok(declarations.every(function(call) { return call.reject === true; }),
	'ubus and ACL errors must reject instead of looking like an empty snapshot');
assert.strictEqual(declarations[0].params, undefined);
assert.deepStrictEqual(declarations[1].params, [ 'modem_id' ]);
assert.deepStrictEqual(declarations[2].params, [ 'modem_id' ]);
assert.deepStrictEqual(declarations[3].params, [ 'modem_id' ]);
assert.deepStrictEqual(declarations[4].params,
	[ 'modem_id', 'generation', 'bands', 'confirm' ]);
assert.deepStrictEqual(declarations[5].params,
	[ 'modem_id', 'generation', 'enabled', 'confirm' ]);
assert.deepStrictEqual(declarations[6].params,
	[ 'modem_id', 'generation', 'confirm' ]);
assert.deepStrictEqual(declarations[7].params,
	[ 'modem_id', 'generation', 'slot', 'confirm' ]);
assert.deepStrictEqual(declarations[8].params,
	[ 'modem_id', 'folder', 'limit', 'cursor' ]);
assert.deepStrictEqual(declarations[9].params, [
	'modem_id', 'generation', 'messaging_generation', 'recipient', 'text', 'client_token'
]);
assert.deepStrictEqual(declarations[10].params, [
	'modem_id', 'generation', 'messaging_generation', 'sms_id', 'confirm'
]);
assert.deepStrictEqual(api.listModems().arguments, []);
assert.deepStrictEqual(api.getOverview('fibocom-test').arguments, [ 'fibocom-test' ]);
assert.deepStrictEqual(api.getStatus('fibocom-test').arguments, [ 'fibocom-test' ]);
assert.deepStrictEqual(api.getCapabilities('fibocom-test').arguments, [ 'fibocom-test' ]);
assert.deepStrictEqual(api.setBands('fibocom-test', 4, [ 'eutran-1' ], true).arguments,
	[ 'fibocom-test', 4, [ 'eutran-1' ], true ]);
assert.deepStrictEqual(api.setRadio('fibocom-test', 4, false, true).arguments,
	[ 'fibocom-test', 4, false, true ]);
assert.deepStrictEqual(api.reset('fibocom-test', 4, true).arguments,
	[ 'fibocom-test', 4, true ]);
assert.deepStrictEqual(api.setPrimarySimSlot('fibocom-test', 4, 2, true).arguments,
	[ 'fibocom-test', 4, 2, true ]);
assert.deepStrictEqual(api.listSms('fibocom-test', 'inbox', 100, '').arguments,
	[ 'fibocom-test', 'inbox', 100, '' ]);
assert.deepStrictEqual(api.sendSms(
	'fibocom-test', 4, 7, '<recipient>', '<text>', 'smsop-' + '0'.repeat(32)
).arguments, [
	'fibocom-test', 4, 7, '<recipient>', '<text>', 'smsop-' + '0'.repeat(32)
]);
assert.deepStrictEqual(api.deleteSms('fibocom-test', 4, 7, 'sms-test', true).arguments,
	[ 'fibocom-test', 4, 7, 'sms-test', true ]);

global._ = function(value) { return value; };
global.E = function(tag, attributes, children) {
	return {
		tag,
		nodeType: 1,
		attributes: attributes || {},
		children: children || []
	};
};
global.L = {
	url: function() { return Array.from(arguments).join('/'); }
};

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

const widgets = evaluate('htdocs/luci-static/resources/fibocom/widgets.js', { baseclass });
assert.deepStrictEqual(widgets.modems({ schema: 1, ok: true, modems: [] }), []);
assert.strictEqual(widgets.modems({ modems: [ { modem_id: 'fibocom-test' } ] })[0].modem_id,
	'fibocom-test');
assert.strictEqual(widgets.responseError({ schema: 1, ok: true }), null);
assert.strictEqual(widgets.responseError({
	ok: false,
	error: { code: 'not_found', message: 'Modem not found' }
}), 'Modem not found (not_found)');
assert.strictEqual(widgets.dependencyRows({
	dependencies: { modemmanager: 'available', mbim: { state: 'available', version: '1.0' } }
}).length, 2);
assert.strictEqual(widgets.portRows({
	ports: [ { role: 'primary', name: 'cdc-wdm0', type: 'mbim', primary: true } ]
})[0][1].children[0], 'cdc-wdm0');
assert.strictEqual(widgets.simSlotRows({
	slots: [ { slot: 1, present: true, active: true, iccid: '***1234' } ]
})[0][0], '1');
assert.strictEqual(widgets.signalRows({ lte: { rsrp: -90, rsrq: -10 } }).length, 2);
assert.strictEqual(widgets.cellRows({ cells: [ { serving: true, pci: 0, earfcn: 1650 } ] })[0][4], '0');
assert.strictEqual(widgets.bearerRows([ {
	connected: true,
	interface: 'wwan0',
	ip_families: [ 'ipv4' ],
	ipv4: { address: '192.0.2.2', gateway: '192.0.2.1', dns: [ '192.0.2.53' ] }
} ])[0][1].children[0], 'wwan0');
assert.strictEqual(widgets.capabilityRows({
	capabilities: { messaging: { state: 'available', mutable: true, reason: 'dbus-interface-present' } }
}).length, 1);

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
});

const summary = {
	modem_id: 'fibocom-test',
	generation: 4,
	manufacturer: 'Fibocom Wireless Inc.',
	model: 'L850-GL',
	plugin: 'fibocom',
	composition: 'mbim',
	state: 'connected',
	supported: true,
	support_reason: 'l850-mbim'
};
const listResult = {
	schema: 1,
	ok: true,
	modems: [ summary ],
	dependencies: {
		modemmanager: 'available',
		netifd_proto: 'available',
		fibocom_plugin: 'available',
		mbim: 'available'
	}
};
const overviewResult = {
	schema: 1,
	ok: true,
	modem_id: 'fibocom-test',
	generation: 4,
	freshness: 'fresh',
	modem: { model: 'L850-GL', revision: 'test', state: 'connected' },
	sim: { present: true, slot: 1, lock: 'none' },
	network: { registration: 'home', operator: 'Test', access: [ 'lte' ] },
	signal: { quality: 72, recent: true },
	bearer: { connected: true, interface: 'wwan0', ip_families: [ 'ipv4' ] },
	openwrt: { network: 'wan', up: true },
	warnings: []
};
const statusResult = {
	schema: 1,
	ok: true,
	modem_id: 'fibocom-test',
	generation: 4,
	general: {
		manufacturer: 'Fibocom Wireless Inc.', model: 'L850-GL', revision: 'test',
		plugin: 'fibocom', drivers: [ 'cdc_mbim' ], state: 'connected'
	},
	ports: [ { name: 'cdc-wdm0', type: 'mbim', role: 'primary', primary: true } ],
	sim: {
		present: true,
		primary_slot: 1,
		iccid: '***1234',
		lock: 'none',
		slots: [
			{ slot: 1, present: true, active: true },
			{ slot: 2, present: false, active: false }
		]
	},
	network: { registration: 'home', operator: 'Test', access: [ 'lte' ] },
	radio: {
		state: 'enabled',
		power_state: 'on',
		supported_bands: [ 'eutran-1', 'eutran-3', 'eutran-8' ],
		current_bands: [ 'eutran-1', 'eutran-3' ],
		band_selection: 'explicit',
		supported_modes: [ { allowed: [ '4g' ], preferred: 'none' } ],
		current_modes: { allowed: [ '4g' ], preferred: 'none' }
	},
	signal: { quality: 72, recent: true, lte: { rsrp: -90 } },
	cell: { state: 'unsupported', reason: 'not-advertised', cells: [] },
	bearers: [ { connected: true, interface: 'wwan0', ip_families: [ 'ipv4' ] } ],
	openwrt: { network: 'wan', up: true },
	network_binding: {
		state: 'bound', section: 'wan', allowedmode: '4g', preferredmode: 'none'
	},
	diagnostics: { modemmanager_version: '1.24.0', mbim: true }
};
const capabilityResult = {
	schema: 1,
	ok: true,
	modem_id: 'fibocom-test',
	generation: 4,
	capabilities: {
		mbim_data: { state: 'available', mutable: false, reason: 'modemmanager' },
		bands: { state: 'available', mutable: true, reason: 'supported-bands-present' },
		modes: { state: 'available', mutable: false, reason: 'network-uci-owned' },
		radio: { state: 'available', mutable: true, reason: 'standard-method' },
		reset: { state: 'available', mutable: true, reason: 'standard-method' },
		sim_slot: { state: 'available', mutable: true, reason: 'multiple-slots' }
	}
};
const smsResult = {
	schema: 1,
	ok: true,
	modem_id: 'fibocom-test',
	generation: 4,
	messaging_generation: 7,
	revision: 1,
	cache_state: 'fresh',
	messages: [],
	next_cursor: '',
	has_more: false
};

const overviewView = evaluate('htdocs/luci-static/resources/view/fibocom/overview.js', viewDependencies);
const statusView = evaluate('htdocs/luci-static/resources/view/fibocom/status.js', viewDependencies);
const smsView = evaluate('htdocs/luci-static/resources/view/fibocom/sms.js', viewDependencies);
const advancedView = evaluate('htdocs/luci-static/resources/view/fibocom/advanced.js', viewDependencies);
const settingsView = evaluate('htdocs/luci-static/resources/view/fibocom/settings.js', viewDependencies);

assert.strictEqual(overviewView.render({
	list: listResult,
	entries: [ { summary: summary, overview: overviewResult } ]
}).tag, 'div');
assert.strictEqual(overviewView.render({
	list: { transport_error: 'connection lost' }, entries: []
}).tag, 'div');
assert.strictEqual(statusView.render({
	list: listResult,
	entries: [ { summary: summary, status: statusResult, capabilities: capabilityResult } ]
}).tag, 'div');
assert.strictEqual(statusView.render({
	list: { transport_error: 'permission denied' }, entries: []
}).tag, 'div');
assert.strictEqual(smsView.render({
	list: listResult,
	entries: [ { summary: summary, messages: smsResult } ]
}).tag, 'div');
assert.strictEqual(smsView.render({
	list: { transport_error: 'permission denied' }, entries: []
}).tag, 'div');
assert.strictEqual(advancedView.render({
	list: listResult,
	entries: [ { summary: summary, status: statusResult, capabilities: capabilityResult } ]
}).tag, 'div');
assert.strictEqual(advancedView.render({
	list: { transport_error: 'permission denied' }, entries: []
}).tag, 'div');
assert.strictEqual(settingsView.render().tag, 'div');

[ overviewView, statusView, smsView, advancedView, settingsView ].forEach(function(module) {
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
	assert.doesNotMatch(source, /(?:innerHTML|outerHTML|insertAdjacentHTML|document\.write)/,
		`${path.relative(appRoot, file)} must construct native DOM nodes safely`);
	assert.doesNotMatch(source, /\blocalStorage\b/,
		`${path.relative(appRoot, file)} must not persist private state in the browser`);
});

const smsSource = read('htdocs/luci-static/resources/view/fibocom/sms.js');
assert.ok(smsSource.includes('poll.add'));
assert.ok(smsSource.includes('ui.showModal'));
assert.ok(smsSource.includes('window.crypto'));
assert.ok(smsSource.includes('getRandomValues'));
assert.ok(smsSource.includes("let token = 'smsop-';"));
assert.ok(smsSource.includes('text_truncated'));
assert.ok(smsSource.includes("[ 'outcome_unknown', 'timeout', 'busy' ]"));
assert.ok(smsSource.includes("case 'inbound':"));
assert.ok(smsSource.includes("case 'outbound':"));
assert.ok(smsSource.includes("case 'report':"));
assert.ok(smsSource.includes("_('Status report')"));
assert.ok(smsSource.includes('redrawPending'));
assert.ok(smsSource.includes("'focusout'"));
assert.ok(smsSource.includes('retrySafetyNotice'));
assert.ok(smsSource.includes('bridge memory for five minutes'));
assert.doesNotMatch(smsSource, /\.slice\s*\(\s*0\s*,/,
	'SMS text must never be truncated before display or sending');

const advancedSource = read('htdocs/luci-static/resources/view/fibocom/advanced.js');
assert.ok(advancedSource.includes('poll.add'));
assert.ok(advancedSource.includes('ui.showModal'));
assert.ok(advancedSource.includes("[ 'any' ]"));
assert.ok(advancedSource.includes('api.setBands'));
assert.ok(advancedSource.includes('api.setRadio'));
assert.ok(advancedSource.includes('api.reset'));
assert.ok(advancedSource.includes('api.setPrimarySimSlot'));
assert.ok(advancedSource.includes('result.accepted !== true'));
assert.ok(advancedSource.includes("'stale_generation'"));
assert.ok(advancedSource.includes("error.code === 'outcome_unknown'"));
assert.ok(advancedSource.includes('do not retry until the live modem state confirms'));
assert.ok(advancedSource.includes("L.url('admin/network/network', section)"));
assert.match(advancedSource, /api\.setBands\([\s\S]*?bands, true\)/);
assert.match(advancedSource, /api\.setRadio\([\s\S]*?true, true\)/);
assert.match(advancedSource, /api\.setRadio\([\s\S]*?false, true\)/);
assert.match(advancedSource, /api\.reset\([^)]*true\)/);
assert.match(advancedSource, /api\.setPrimarySimSlot\([\s\S]*?slot, true\)/);
assert.doesNotMatch(advancedSource, /set(?:Current)?Modes/i,
	'Advanced must not mutate ModemManager modes directly');
assert.doesNotMatch(advancedSource, /\/dev\/|tty(?:USB|ACM)|SetCurrentBands|SetCurrentModes/,
	'Advanced must not expose device selectors or direct D-Bus method names');

const settingsSource = read('htdocs/luci-static/resources/view/fibocom/settings.js');
assert.ok(settingsSource.includes("L.url('admin/network/network')"));
assert.ok(settingsSource.includes("L.url('admin/status/modemmanager')"));
assert.ok(!read('htdocs/luci-static/resources/view/fibocom/overview.js').includes('rescan'));
assert.strictEqual(listFiles(appRoot).filter(function(file) { return file.endsWith('.lua'); }).length, 0,
	'legacy Lua controllers and models are forbidden');

const makefile = read('Makefile');
[
	'@MODEMMANAGER_WITH_MBIM',
	'@MODEMMANAGER_WITH_NETIFD',
	'+luci-base',
	'+fibocom-mm-bridge',
	'+modemmanager',
	'+luci-proto-modemmanager',
	'+kmod-usb-acm',
	'+kmod-usb-net-cdc-mbim',
	'+kmod-usb-wdm'
].forEach(function(dependency) {
	assert.ok(makefile.includes(dependency), `Makefile must include ${dependency}`);
});
assert.ok(makefile.includes('PKG_LICENSE:=Apache-2.0'));
assert.ok(makefile.includes('include $(TOPDIR)/feeds/luci/luci.mk'));

const pot = read('po/templates/fibocom.pot');
[ 'Diagnostics', 'fibocomd', 'Rescan devices', 'Shadow mode' ].forEach(function(legacy) {
	assert.ok(!pot.includes(legacy), `translation template must not contain legacy text: ${legacy}`);
});
[ 'SMS', 'Compose SMS', 'Delete SMS', 'Messaging cache', 'Retry SMS send',
	'Advanced', 'Band selection', 'Radio power', 'Reset modem', 'Primary SIM slot'
].forEach(function(text) {
	assert.ok(pot.includes(`msgid "${text}"`), `translation template must contain: ${text}`);
});
assert.ok(!pot.includes('not available in this P0 interface'));

console.log('luci-app-fibocom static checks: OK');
