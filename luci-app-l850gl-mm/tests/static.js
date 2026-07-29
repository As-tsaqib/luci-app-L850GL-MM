// SPDX-FileCopyrightText: 2026 As Tsaqib
// SPDX-License-Identifier: Apache-2.0
/* global __dirname, global, require */

'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');

const appRoot = path.resolve(__dirname, '..');
const resources = path.join(appRoot, 'htdocs/luci-static/resources');
const menuPath = path.join(appRoot, 'root/usr/share/luci/menu.d/luci-app-l850gl-mm.json');
const aclPath = path.join(appRoot, 'root/usr/share/rpcd/acl.d/luci-app-l850gl-mm.json');

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
const menuRoot = menu['admin/modem/l850gl-mm'];

assert.strictEqual(path.basename(appRoot), 'luci-app-l850gl-mm');
assert.strictEqual(menu['admin/modem'].action.type, 'firstchild');
assert.strictEqual(menuRoot.title, 'L850GL MM');
assert.deepStrictEqual(menuRoot.depends.acl, [ 'luci-app-l850gl-mm-overview' ]);
assert.strictEqual(menuRoot.action.type, 'firstchild');

const expectedViews = [ 'overview', 'lock', 'sms' ];
expectedViews.forEach(function(name) {
	const entry = menu[`admin/modem/l850gl-mm/${name}`];

	assert.ok(entry, `menu entry for ${name} must exist`);
	assert.strictEqual(entry.action.type, 'view');
	assert.strictEqual(entry.action.path, `l850gl-mm/${name}`);
	assert.ok(fs.existsSync(path.join(resources, 'view/l850gl-mm', `${name}.js`)),
		`${name}.js must exist`);
});
assert.deepStrictEqual(Object.keys(menu).filter(function(key) {
	return key.startsWith('admin/modem/l850gl-mm/');
}).sort(), expectedViews.map(function(name) {
	return `admin/modem/l850gl-mm/${name}`;
}).sort(), 'the package must advertise exactly Overview, Lock, and SMS');
assert.deepStrictEqual(listFiles(resources).filter(function(file) {
	return file.endsWith('.css');
}).map(function(file) {
	return path.relative(resources, file).split(path.sep).join('/');
}), [ 'l850gl-mm/l850gl-mm.css' ],
'the package must install one shared stylesheet instead of desktop/mobile variants');
for (const retired of [ 'status', 'advanced', 'settings', 'diagnostics' ]) {
	assert.ok(!fs.existsSync(path.join(resources, 'view/l850gl-mm', `${retired}.js`)),
		`${retired}.js must be retired`);
}
assert.strictEqual(menu['admin/modem/l850gl-mm/overview'].order, 10);
assert.strictEqual(menu['admin/modem/l850gl-mm/lock'].order, 20);
assert.strictEqual(menu['admin/modem/l850gl-mm/sms'].order, 30);
assert.deepStrictEqual(menu['admin/modem/l850gl-mm/lock'].depends.acl,
	[ 'luci-app-l850gl-mm-overview', 'luci-app-l850gl-mm-lock-band' ]);
assert.deepStrictEqual(menu['admin/modem/l850gl-mm/sms'].depends.acl,
	[ 'luci-app-l850gl-mm-sms-read' ]);

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
assert.deepStrictEqual(acl['luci-app-l850gl-mm-sms-read'].read.ubus['l850gl.mm'], [
	'list_sms'
]);
assert.strictEqual(acl['luci-app-l850gl-mm-sms-read'].write, undefined);
assert.deepStrictEqual(acl['luci-app-l850gl-mm-sms-write'].write.ubus['l850gl.mm'], [
	'send_sms', 'delete_sms'
]);
assert.strictEqual(acl['luci-app-l850gl-mm-sms-write'].read, undefined);
assert.deepStrictEqual(acl['luci-app-l850gl-mm-lock-band'].write.ubus['l850gl.mm'], [
	'set_bands', 'set_modes'
]);
assert.deepStrictEqual(
	acl['luci-app-l850gl-mm-lock-pci-expert'].read.ubus['l850gl.mm.l850'],
	[ 'cell_scan', 'get_carrier_info', 'cell_lock_status' ]);
assert.deepStrictEqual(
	acl['luci-app-l850gl-mm-lock-pci-expert'].write.ubus['l850gl.mm.l850'],
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
const api = evaluate('htdocs/luci-static/resources/l850gl-mm/api.js', { baseclass, rpc });

assert.strictEqual(api.SCHEMA_VERSION, 4);
assert.deepStrictEqual(declarations.map(function(call) {
	return `${call.object}.${call.method}`;
}), [
	'l850gl.mm.list_modems',
	'l850gl.mm.get_overview',
	'l850gl.mm.get_lock_status',
	'l850gl.mm.set_bands',
	'l850gl.mm.set_modes',
	'l850gl.mm.list_sms',
	'l850gl.mm.send_sms',
	'l850gl.mm.delete_sms',
	'l850gl.mm.l850.cell_scan',
	'l850gl.mm.l850.get_carrier_info',
	'l850gl.mm.l850.cell_lock_status',
	'l850gl.mm.l850.set_cell_lock',
	'l850gl.mm.l850.clear_cell_lock'
]);
assert.ok(declarations.every(function(call) { return call.reject === true; }),
	'ubus and ACL errors must reject instead of looking like an empty snapshot');
assert.strictEqual(declarations[0].params, undefined);
assert.deepStrictEqual(declarations[1].params, [ 'modem_id' ]);
assert.deepStrictEqual(declarations[2].params, [ 'modem_id' ]);
assert.deepStrictEqual(declarations[3].params,
	[ 'modem_id', 'generation', 'bands', 'confirm' ]);
assert.deepStrictEqual(declarations[4].params,
	[ 'modem_id', 'generation', 'allowed', 'preferred', 'confirm' ]);
assert.deepStrictEqual(declarations[5].params,
	[ 'modem_id', 'folder', 'limit', 'cursor' ]);
assert.deepStrictEqual(declarations[6].params, [
	'modem_id', 'generation', 'messaging_generation', 'recipient', 'text', 'client_token'
]);
assert.deepStrictEqual(declarations[7].params, [
	'modem_id', 'generation', 'messaging_generation', 'sms_id', 'confirm'
]);
assert.deepStrictEqual(declarations[8].params, [ 'modem_id', 'generation' ]);
assert.deepStrictEqual(declarations[9].params, [ 'modem_id', 'generation' ]);
assert.deepStrictEqual(declarations[10].params, [ 'modem_id', 'generation' ]);
assert.deepStrictEqual(declarations[11].params,
	[ 'modem_id', 'generation', 'earfcn', 'pci', 'confirm' ]);
assert.deepStrictEqual(declarations[12].params,
	[ 'modem_id', 'generation', 'confirm' ]);
assert.deepStrictEqual(api.listModems().arguments, []);
assert.deepStrictEqual(api.getOverview('l850gl-mm-test').arguments, [ 'l850gl-mm-test' ]);
assert.deepStrictEqual(api.getLockStatus('l850gl-mm-test').arguments, [ 'l850gl-mm-test' ]);
assert.deepStrictEqual(api.setBands('l850gl-mm-test', 4, [ 'eutran-1' ], true).arguments,
	[ 'l850gl-mm-test', 4, [ 'eutran-1' ], true ]);
assert.deepStrictEqual(api.setModes('l850gl-mm-test', 4, '3g|4g', '4g', true).arguments,
	[ 'l850gl-mm-test', 4, '3g|4g', '4g', true ]);
assert.deepStrictEqual(api.listSms('l850gl-mm-test', 'inbox', 100, 'next').arguments,
	[ 'l850gl-mm-test', 'inbox', 100, 'next' ]);
assert.deepStrictEqual(api.getCarrierInfo('l850gl-mm-test', 4).arguments,
	[ 'l850gl-mm-test', 4 ]);
assert.deepStrictEqual(api.cellScan('l850gl-mm-test', 4).arguments,
	[ 'l850gl-mm-test', 4 ]);
assert.deepStrictEqual(api.cellLockStatus('l850gl-mm-test', 4).arguments,
	[ 'l850gl-mm-test', 4 ]);
assert.deepStrictEqual(api.setCellLock('l850gl-mm-test', 4, 1650, 0, true).arguments,
	[ 'l850gl-mm-test', 4, 1650, 0, true ]);
assert.deepStrictEqual(api.clearCellLock('l850gl-mm-test', 4, true).arguments,
	[ 'l850gl-mm-test', 4, true ]);
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
global.L = {
	url: function() { return Array.from(arguments).join('/'); },
	resource: function(resource) { return '/luci-static/resources/' + resource; }
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

function findNodes(node, predicate) {
	const matches = [];

	(function visit(value) {
		if (Array.isArray(value)) {
			value.forEach(visit);
			return;
		}
		if (value == null || typeof value !== 'object')
			return;
		if (predicate(value))
			matches.push(value);
		if (Array.isArray(value.children))
			value.children.forEach(visit);
	})(node);
	return matches;
}

function hasClass(node, className) {
	return node != null && node.attributes != null &&
		typeof node.attributes.class === 'string' &&
		node.attributes.class.split(/\s+/).includes(className);
}

function findKeyValueRows(node, label) {
	return findNodes(node, function(candidate) {
		const values = renderedText(candidate);

		return hasClass(candidate, 'l850gl-mm-kv-row') && values[0] === label;
	});
}

function normalizeDom(node) {
	if (Array.isArray(node))
		return node.map(normalizeDom);
	if (node == null || typeof node !== 'object')
		return node;
	if (typeof node.tag !== 'string')
		return null;

	const attributes = {};
	Object.keys(node.attributes || {}).sort().forEach(function(name) {
		const value = node.attributes[name];

		if (typeof value !== 'function')
			attributes[name] = value;
	});
	return {
		tag: node.tag,
		attributes: attributes,
		children: normalizeDom(node.children || [])
	};
}

function assertPageStructure(node, pageClass) {
	assert.ok(hasClass(node, 'cbi-map'), `${pageClass} must use the native LuCI map root`);
	assert.ok(hasClass(node, 'l850gl-mm-page'), `${pageClass} must load the shared layout scope`);
	assert.ok(hasClass(node, pageClass), `${pageClass} must expose its scoped page class`);
	const stylesheets = findNodes(node, function(candidate) {
		return candidate.tag === 'link' && candidate.attributes.rel === 'stylesheet' &&
			candidate.attributes.href === '/luci-static/resources/l850gl-mm/l850gl-mm.css';
	});

	assert.strictEqual(stylesheets.length, 1,
		`${pageClass} must load exactly one shared responsive stylesheet`);
}

function assertLabelTargets(node, context) {
	const controlIds = new Set(findNodes(node, function(candidate) {
		return candidate.attributes && typeof candidate.attributes.id === 'string';
	}).map(function(candidate) { return candidate.attributes.id; }));
	const labels = findNodes(node, function(candidate) {
		return candidate.tag === 'label' && candidate.attributes &&
			typeof candidate.attributes.for === 'string';
	});

	assert.ok(labels.length > 0, `${context} must expose explicit control labels`);
	assert.ok(labels.every(function(label) {
		return controlIds.has(label.attributes.for);
	}), `${context} labels must target controls in the same responsive markup`);
}

const widgets = evaluate('htdocs/luci-static/resources/l850gl-mm/widgets.js', { baseclass });
const sampleKeyValues = widgets.keyValueList([ [ 'Alpha', 'Beta' ] ]);
const sampleKeyValueRows = findNodes(sampleKeyValues, function(node) {
	return hasClass(node, 'l850gl-mm-kv-row');
});

assert.strictEqual(sampleKeyValueRows.length, 1);
assert.ok(hasClass(sampleKeyValueRows[0], 'cbi-value'));
assert.deepStrictEqual(renderedText(sampleKeyValues), [ 'Alpha', 'Beta' ],
	'key/value status lists must not add desktop-only Property/Value headings');
const sampleTable = widgets.table([ 'First', 'Second' ], [ [ 'A', 'B' ] ]);
const sampleCells = findNodes(sampleTable, function(node) { return node.tag === 'td'; });

assert.deepStrictEqual(sampleCells.map(function(node) {
	return node.attributes['data-title'];
}), [ 'First', 'Second' ], 'generic LuCI tables must expose responsive cell headings');
assert.ok(hasClass(widgets.badge('B1, B3', 'notice'), 'notice'),
	'locked band summaries must use the native LuCI blue notice badge');
assert.ok(hasClass(widgets.badge('Received', 'received'), 'success'),
	'received SMS state must use the native LuCI green success badge');
assert.ok(hasClass(widgets.badge('Sent', 'sent'), 'notice'),
	'sent SMS state must use the native LuCI blue notice badge');
assert.strictEqual(widgets.isCompatible({ schema: 4, ok: true }), true);
assert.strictEqual(widgets.isCompatible({ schema: 1, ok: true }), false);
assert.strictEqual(widgets.isCompatible({ schema: 4, ok: 'yes' }), false);
assert.deepStrictEqual(widgets.modems({ schema: 4, ok: true, modems: [] }), []);
assert.deepStrictEqual(widgets.modems({ schema: 1, ok: true, modems: [] }), []);
assert.match(widgets.responseError({ schema: 1, ok: true }), /schema/i);
assert.match(widgets.responseError({ schema: 4, ok: true }), /malformed/i,
	'a partial success response must fail closed');
assert.strictEqual(widgets.mutationAllowed({ schema: 4, generated_at: 1, ok: true }, {
	modem_id: 'l850gl-mm-test', generation: 4
}, 'l850gl-mm-test', 4), true);
assert.strictEqual(widgets.mutationAllowed({ schema: 2, generated_at: 1, ok: true }, {
	modem_id: 'l850gl-mm-test', generation: 4
}, 'l850gl-mm-test', 4), false);

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
	const relative = `htdocs/luci-static/resources/view/l850gl-mm/${name}.js`;
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
	modem_id: 'l850gl-mm-test',
	generation: 4,
	manufacturer: 'Example Wireless Inc.',
	model: 'L850-GL',
	revision: '18500.test',
	state: 'connected',
	power: 'on'
};
const listResult = {
	schema: 4,
	generated_at: 1,
	ok: true,
	modems: [ summary ]
};
const oversizedListResult = Object.assign({}, listResult, {
	modems: Array.from({ length: 65 }, function(_, index) {
		return Object.assign({}, summary, { modem_id: 'l850gl-mm-test-' + index });
	})
});

assert.match(widgets.listError(oversizedListResult), /malformed/i,
	'the validated inventory bound must cap an in-memory carrier cache at 64 modems');
const overviewResult = {
	schema: 4,
	generated_at: 1,
	ok: true,
	modem_id: 'l850gl-mm-test',
	generation: 4,
	identity: {
		manufacturer: 'Example Wireless Inc.', model: 'L850-GL', revision: '18500.test',
		imei: '359762080000001'
	},
	usb_mode: 'mbim',
	modem: { state: 'connected', power: 'on', voltage_mv: 3550 },
	sim: {
		present: true, lock: 'none', number: '+628111000000',
		imsi: '510100000000001', iccid: '8962100000000000001'
	},
	network: {
		operator: 'Example', registration: 'home', roaming: false, access: [ 'lte' ]
	},
	signal: { quality: 72, recent: true, rsrp: -90, rsrq: -10, sinr: 14 },
	bearer: { connected: true, interface: 'wwan0' },
	current_bands: [ 'utran-1', 'eutran-1', 'eutran-3' ],
	serving_cell: {
		state: 'available', reason: 'standard-cell-info', earfcn: 1325,
		pci: 0, band: 3, rsrp: -90, rsrq: -10
	},
	capabilities: {
		sms: { state: 'available', mutable: true },
		band_lock: { state: 'available', mutable: true },
		pci_lock: { state: 'unsupported_build', mutable: false }
	},
	warnings: []
};
const carrierResult = {
	schema: 4,
	generated_at: 1,
	ok: true,
	modem_id: 'l850gl-mm-test',
	generation: 4,
	state: 'available',
	active_bands: [ 3, 7 ],
	primary: {
		index: 1, band: 3, earfcn: 1325, pci: 0,
		dl_bandwidth_mhz: 20, ul_bandwidth_mhz: 10
	},
	secondary: [ {
		index: 2, band: 7, earfcn: 2850, pci: 321,
		dl_bandwidth_mhz: 15, ul_bandwidth_mhz: null
	} ],
	active_carriers: 2,
	source: 'modemmanager',
	method: 'l850-gtcainfo'
};
const lockResult = {
	schema: 4,
	generated_at: 1,
	ok: true,
	modem_id: 'l850gl-mm-test',
	generation: 4,
	supported_bands: [ 'utran-1', 'eutran-1', 'eutran-3', 'eutran-8' ],
	current_bands: [ 'utran-1', 'eutran-1', 'eutran-3' ],
	band_selection: 'explicit',
	current_modes: { known: true, allowed: [ '3g', '4g' ], preferred: '4g' },
	mode_policy: {
		state: 'available', mutable: true, reason: 'unique-netifd-binding',
		configured: true, allowed: '3g|4g', preferred: '4g'
	},
	band_lock: { state: 'available', mutable: true, retry_after_ms: 0 },
	pci_lock: { state: 'unsupported_build', mutable: false, reason: 'expert-object-absent' }
};
const smsResult = {
	schema: 4,
	generated_at: 1,
	ok: true,
	modem_id: 'l850gl-mm-test',
	generation: 4,
	messaging_generation: 7,
	revision: 1,
	cache_state: 'fresh',
	cache_truncated: false,
	dedupe_capacity: 64,
	dedupe_window_seconds: 300,
	folder: 'all',
	limit: 100,
	messages: [],
	next_cursor: 'sms-next',
	has_more: true
};
assert.strictEqual(widgets.smsError(smsResult, summary), null);
assert.match(widgets.smsError(Object.assign({}, smsResult, {
	cache_truncated: 'false'
}), summary), /malformed/i,
'SMS cache truncation must be a typed boolean before any bulk mutation is enabled');
assert.match(widgets.smsError(Object.assign({}, smsResult, {
	folder: 'everything'
}), summary), /malformed/i,
'SMS folder echoes must match the typed contract');
assert.match(widgets.smsError(Object.assign({}, smsResult, {
	limit: 101
}), summary), /malformed/i,
'SMS page limits outside the backend bound must fail closed');
const expertResult = {
	schema: 4,
	generated_at: 1,
	ok: false,
	modem_id: 'l850gl-mm-test',
	generation: 4,
	state: 'unsupported_firmware',
	error: {
		code: 'unsupported_firmware',
		message: 'Firmware is not in the live-validated allowlist',
		retryable: false
	}
};
const availableExpertResult = {
	schema: 4,
	generated_at: 1,
	ok: true,
	modem_id: 'l850gl-mm-test',
	generation: 4,
	state: 'available',
	mutable: true,
	reason: 'live-validated-firmware-and-nvm-state',
	lock: {
		state: 'configured_exact',
		enabled: true,
		postcondition_verified: false,
		earfcn: 1325,
		pci: 0,
		band: 3,
		source: 'l850-nvm-via-modemmanager'
	},
	scan: {
		state: 'available',
		available: true,
		reason: 'standard-with-live-validated-xmci-fallback',
		source: 'modemmanager'
	}
};

assert.strictEqual(widgets.overviewError(overviewResult, summary), null);
assert.match(widgets.overviewError(Object.assign({}, overviewResult, {
	modem: Object.assign({}, overviewResult.modem, { voltage_mv: 2499 })
}), summary), /malformed/i,
'out-of-range modem voltage must fail frontend validation');
assert.match(widgets.overviewError(Object.assign({}, overviewResult, {
	modem: Object.assign({}, overviewResult.modem, { voltage_mv: '3550' })
}), summary), /malformed/i,
'modem voltage must remain a typed integer');
assert.strictEqual(widgets.overviewError(Object.assign({}, overviewResult, {
	modem: Object.assign({}, overviewResult.modem, { voltage_mv: null })
}), summary), null, 'nullable modem voltage must keep Overview renderable');
assert.match(widgets.overviewError(Object.assign({}, overviewResult, {
	serving_cell: {
		state: 'available', reason: 'standard-cell-info', earfcn: 1325,
		pci: 504, band: 3
	}
}), summary), /malformed/i,
'an out-of-range serving PCI must fail frontend validation');
assert.strictEqual(widgets.overviewError(Object.assign({}, overviewResult, {
	serving_cell: { state: 'unavailable', reason: 'refresh-pending' }
}), summary), null, 'a typed unavailable Serving Cell state must remain renderable');
assert.match(widgets.overviewError(Object.assign({}, overviewResult, {
	usb_mode: 'qmi'
}), summary), /malformed/i,
'unrecognized USB compositions must fail closed');
assert.match(widgets.overviewError(Object.assign({}, overviewResult, {
	identity: Object.assign({}, overviewResult.identity, { imei: 359762080000001 })
}), summary), /malformed/i,
'modem identifiers must remain typed strings to preserve every digit');
assert.match(widgets.overviewError(Object.assign({}, overviewResult, {
	identity: Object.assign({}, overviewResult.identity, { imei: '1'.repeat(65) })
}), summary), /malformed/i,
'frontend identifier bounds must match the backend serializer');
assert.match(widgets.overviewError(Object.assign({}, overviewResult, {
	sim: Object.assign({}, overviewResult.sim, { number: '1'.repeat(33) })
}), summary), /malformed/i,
'frontend SIM-number bounds must match the backend serializer');
assert.match(widgets.overviewError(Object.assign({}, overviewResult, {
	sim: Object.assign({}, overviewResult.sim, { present: false })
}), summary), /malformed/i,
'an absent SIM must never carry subscriber identifiers');
assert.strictEqual(widgets.carrierInfoError(carrierResult, summary), null);
assert.match(widgets.carrierInfoError(Object.assign({}, carrierResult, {
	primary: Object.assign({}, carrierResult.primary, { ul_bandwidth_mhz: null })
}), summary), /malformed/i,
'the primary carrier must always have a validated numeric uplink bandwidth');
assert.match(widgets.carrierInfoError(Object.assign({}, carrierResult, {
	secondary: [ Object.assign({}, carrierResult.secondary[0], {
		ul_bandwidth_mhz: 5
	}) ]
}), summary), /malformed/i,
'an unverified active secondary uplink must fail closed');
const secondaryWithoutUplinkBandwidth = Object.assign({}, carrierResult.secondary[0]);

delete secondaryWithoutUplinkBandwidth.ul_bandwidth_mhz;
assert.match(widgets.carrierInfoError(Object.assign({}, carrierResult, {
	secondary: [ secondaryWithoutUplinkBandwidth ]
}), summary), /malformed/i,
'a downlink-only secondary must carry an explicit null uplink bandwidth');
assert.match(widgets.carrierInfoError(Object.assign({}, carrierResult, {
	primary: Object.assign({}, carrierResult.primary, { pci: 504 })
}), summary), /malformed/i,
'carrier PCI values outside the LTE range must fail closed');
assert.match(widgets.carrierInfoError(Object.assign({}, carrierResult, {
	primary: Object.assign({}, carrierResult.primary, { band: 1 })
}), summary), /malformed/i,
'carrier EARFCN must map to its reported LTE band');
assert.match(widgets.carrierInfoError(Object.assign({}, carrierResult, {
	primary: Object.assign({}, carrierResult.primary, { dl_bandwidth_mhz: 2 })
}), summary), /malformed/i,
'unrecognized carrier bandwidth values must fail closed');
assert.match(widgets.carrierInfoError(Object.assign({}, carrierResult, {
	secondary: [ Object.assign({}, carrierResult.secondary[0], { index: 1 }) ]
}), summary), /malformed/i,
'carrier indexes must be unique and reserve index 1 for the primary carrier');
assert.match(widgets.carrierInfoError(Object.assign({}, carrierResult, {
	secondary: [ Object.assign({}, carrierResult.secondary[0], {
		band: 3, earfcn: 1325, pci: 0
	}) ],
	active_bands: [ 3 ]
}), summary), /malformed/i,
'duplicate public carrier tuples must fail closed even with distinct indexes');
assert.match(widgets.carrierInfoError(Object.assign({}, carrierResult, {
	active_bands: [ 3, 20 ]
}), summary), /malformed/i,
'active LTE bands must exactly match the validated carrier set');
assert.match(widgets.carrierInfoError(Object.assign({}, carrierResult, {
	active_carriers: 3
}), summary), /malformed/i,
'the active carrier count must match primary plus secondary carriers');
assert.match(widgets.carrierInfoError(Object.assign({}, carrierResult, {
	method: 'unreviewed-command'
}), summary), /malformed/i,
'carrier provenance must match the exact reviewed backend contract');
assert.match(widgets.carrierInfoError(Object.assign({}, carrierResult, {
	schema: 3
}), summary), /schema/i,
'legacy expert schemas must fail closed');
assert.strictEqual(widgets.lockError(lockResult, summary), null);
assert.match(widgets.lockError(Object.assign({}, lockResult, {
	mode_policy: Object.assign({}, lockResult.mode_policy, {
		allowed: '4g', preferred: '4g'
	})
}), summary), /malformed/i,
'an inconsistent persistent mode policy must fail closed');

const overviewView = evaluate(
	'htdocs/luci-static/resources/view/l850gl-mm/overview.js', viewDependencies);
const lockView = evaluate(
	'htdocs/luci-static/resources/view/l850gl-mm/lock.js', viewDependencies);
const smsView = evaluate(
	'htdocs/luci-static/resources/view/l850gl-mm/sms.js', viewDependencies);

const overviewNode = overviewView.render({
	list: listResult,
	entries: [ {
		summary: summary, overview: overviewResult, lock: lockResult,
		carrier: carrierResult
	} ]
});
assert.strictEqual(overviewNode.tag, 'div');
assertPageStructure(overviewNode, 'l850gl-mm-overview-page');
const renderedOverview = renderedText(overviewNode);
const overviewDescription = findNodes(overviewNode, function(node) {
	return hasClass(node, 'cbi-map-descr');
})[0];
const overviewSections = findNodes(overviewNode, function(node) {
	return node.attributes && typeof node.attributes.class === 'string' &&
		node.attributes.class.split(/\s+/).includes('l850gl-mm-overview-section');
});

assert.strictEqual(overviewSections.length, 3,
	'Overview must render exactly three full-width LuCI sections');
assert.deepStrictEqual(renderedText(overviewDescription),
	[ 'Modem info by ModemManager' ],
	'Overview must use the requested concise ModemManager description');
assert.ok(overviewSections.every(function(section) {
	return hasClass(section, 'cbi-section');
}), 'every Overview group must use the standard LuCI cbi-section structure');
assert.deepStrictEqual(overviewSections.map(function(section) {
	return renderedText(section)[0];
}), [ 'Modem Info', 'Modem Status', 'Band and Cell Status' ],
'Overview sections must form one ordered vertical flow');
assert.strictEqual(findNodes(overviewNode, function(node) {
	return hasClass(node, 'l850gl-mm-overview-column');
}).length, 0, 'Overview must not retain desktop-only columns');
assert.strictEqual(findNodes(overviewNode, function(node) {
	return hasClass(node, 'l850gl-mm-device-title');
}).length, 0, 'Overview must not duplicate the modem model in a large device header');
assert.strictEqual(renderedOverview.includes('Capabilities'), false,
	'Overview must remove the complete capability subsection');
[ 'SMS', 'Band Lock', 'PCI/EARFCN Lock' ].forEach(function(label) {
	assert.strictEqual(findKeyValueRows(overviewNode, label).length, 0,
		`Overview must not render the retired ${label} capability row`);
});
[ 'Modem Info', 'Modem Status', 'Band and Cell Status', 'SIMs',
	'B1, B3', 'Firmware', 'USB Mode', 'SIM Number', 'IMEI', 'ICCID',
	'Active LTE Bands', 'Active LTE Carriers', 'LTE CA Details',
	'Total Bandwidth' ]
	.forEach(function(expected) {
		assert.ok(renderedOverview.some(function(value) { return value.includes(expected); }),
			`Overview must render the ${expected} group or friendly band label`);
	});
assert.deepStrictEqual(renderedText(findKeyValueRows(overviewNode, 'Current bands')[0]),
	[ 'Current bands', 'B1, B3' ]);
assert.ok(!renderedText(findKeyValueRows(overviewNode, 'Current bands')[0])
	.some(function(value) { return /(?:3G|4G:|utran-)/.test(value); }),
'Overview Current bands must contain only concise Bn LTE labels');
const overviewAutomaticNode = overviewView.render({
	list: listResult,
	entries: [ {
		summary: summary,
		overview: Object.assign({}, overviewResult, { current_bands: [ 'any' ] }),
		lock: Object.assign({}, lockResult, {
			band_selection: 'automatic', current_bands: [ 'any' ]
		}),
		carrier: carrierResult
	} ]
});

assert.deepStrictEqual(
	renderedText(findKeyValueRows(overviewAutomaticNode, 'Current bands')[0]),
	[ 'Current bands', 'any(automatic)' ]);
const overviewAllBandsNode = overviewView.render({
	list: listResult,
	entries: [ {
		summary: summary,
		overview: Object.assign({}, overviewResult, {
			current_bands: lockResult.supported_bands
		}),
		lock: Object.assign({}, lockResult, {
			band_selection: 'explicit', current_bands: lockResult.supported_bands
		}),
		carrier: carrierResult
	} ]
});

assert.deepStrictEqual(
	renderedText(findKeyValueRows(overviewAllBandsNode, 'Current bands')[0]),
	[ 'Current bands', 'any(automatic)' ],
	'all supported LTE bands must be summarized as automatic');
[ { transport_error: 'lock status unavailable' },
	Object.assign({}, lockResult, { schema: 3 }),
	Object.assign({}, lockResult, { generation: summary.generation + 1 }),
	Object.assign({}, lockResult, { band_selection: 'unknown' }),
	Object.assign({}, lockResult, {
		band_selection: 'explicit', current_bands: [ 'any', 'eutran-1' ]
	}),
	Object.assign({}, lockResult, {
		band_selection: 'explicit', current_bands: [ 'eutran-66' ]
	}),
	Object.assign({}, lockResult, {
		band_selection: 'explicit', current_bands: [ 'utran-1' ]
	}) ].forEach(function(lock) {
	const node = overviewView.render({
		list: listResult,
		entries: [ {
			summary: summary, overview: overviewResult, lock: lock,
			carrier: carrierResult
		} ]
	});

	assert.deepStrictEqual(renderedText(findKeyValueRows(node, 'Current bands')[0]),
		[ 'Current bands', 'Unavailable' ],
		'invalid or stale Lock snapshots must fail closed without raw bands');
});
assert.deepStrictEqual(renderedText(findKeyValueRows(overviewNode, 'USB Mode')[0]),
	[ 'USB Mode', 'MBIM' ]);
[ [ 'ncm', 'NCM' ], [ 'unknown', 'Unknown' ] ].forEach(function(expectation) {
	const usbOverviewNode = overviewView.render({
		list: listResult,
		entries: [ {
			summary: summary,
			overview: Object.assign({}, overviewResult, { usb_mode: expectation[0] }),
			carrier: carrierResult
		} ]
	});

	assert.deepStrictEqual(
		renderedText(findKeyValueRows(usbOverviewNode, 'USB Mode')[0]),
		[ 'USB Mode', expectation[1] ],
		`USB mode ${expectation[0]} must render its normalized label`);
});
assert.deepStrictEqual(renderedText(findKeyValueRows(overviewNode, 'Active LTE Bands')[0]),
	[ 'Active LTE Bands', 'B3 + B7' ]);
assert.strictEqual(findKeyValueRows(overviewNode, 'Primary LTE Band').length, 0,
	'primary LTE band must not duplicate Active LTE Bands');
assert.strictEqual(findKeyValueRows(overviewNode, 'Secondary LTE Bands').length, 0,
	'secondary LTE bands must not duplicate Active LTE Bands');
assert.deepStrictEqual(renderedText(findKeyValueRows(overviewNode, 'Active LTE Carriers')[0]),
	[ 'Active LTE Carriers', '2' ]);
const carrierDetailsText = renderedText(
	findKeyValueRows(overviewNode, 'LTE CA Details')[0]).join(' ');

assert.match(carrierDetailsText, /B3, EARFCN 1325, PCI 0/);
assert.match(carrierDetailsText, /B7, EARFCN 2850, PCI 321/);
assert.doesNotMatch(carrierDetailsText, /Primary carrier|Secondary carrier|#[1-8]/,
	'LTE CA details must start every carrier directly with its band');
assert.doesNotMatch(carrierDetailsText, /MHz|DL\/UL/,
	'LTE CA details must omit per-carrier bandwidth text');
assert.doesNotMatch(carrierDetailsText, /Source|Method/,
	'Overview must not expose internal carrier transport labels');
assert.deepStrictEqual(renderedText(findKeyValueRows(overviewNode,
	'Total Bandwidth')[0]), [ 'Total Bandwidth', 'DL 35 MHz · UL 10 MHz' ],
	'Total Bandwidth must sum all active downlinks and only reported uplinks');
const fractionalBandwidthNode = overviewView.render({
	list: listResult,
	entries: [ {
		summary: summary, overview: overviewResult, lock: lockResult,
		carrier: Object.assign({}, carrierResult, {
			primary: Object.assign({}, carrierResult.primary, {
				dl_bandwidth_mhz: 1.4, ul_bandwidth_mhz: 1.4
			}),
			secondary: [
				Object.assign({}, carrierResult.secondary[0], {
					dl_bandwidth_mhz: 1.4
				}),
				{
					index: 3, band: 1, earfcn: 325, pci: 123,
					dl_bandwidth_mhz: 1.4, ul_bandwidth_mhz: null
				}
			],
			active_bands: [ 3, 7, 1 ],
			active_carriers: 3
		})
	} ]
});

assert.deepStrictEqual(renderedText(findKeyValueRows(fractionalBandwidthNode,
	'Total Bandwidth')[0]), [ 'Total Bandwidth', 'DL 4.2 MHz · UL 1.4 MHz' ],
	'fractional bandwidth totals must remain exact to one decimal place');
const firmwareRows = findKeyValueRows(overviewNode, 'Firmware');

assert.strictEqual(firmwareRows.length, 1,
	'Overview must render exactly one modem firmware row');
assert.deepStrictEqual(renderedText(firmwareRows[0]),
	[ 'Firmware', '18500.test' ],
	'Overview must restore the modem firmware revision under a clear label');
assert.deepStrictEqual(renderedText(findKeyValueRows(overviewNode,
	'Modem voltage')[0]), [ 'Modem voltage', '3550 mV' ],
'Overview must render the validated AT+CBC voltage in millivolts');
const nullVoltageNode = overviewView.render({
	list: listResult,
	entries: [ {
		summary: summary,
		overview: Object.assign({}, overviewResult, {
			modem: Object.assign({}, overviewResult.modem, { voltage_mv: null })
		}),
		lock: lockResult,
		carrier: carrierResult
	} ]
});

assert.strictEqual(findKeyValueRows(nullVoltageNode, 'Modem voltage').length, 0,
	'a nullable voltage must omit only its row');
assert.strictEqual(findKeyValueRows(overviewNode, 'Revision').length, 0,
	'Overview must use Firmware rather than the ambiguous Revision label');
assert.strictEqual(findKeyValueRows(overviewNode, 'IMSI').length, 0,
	'Overview must not display IMSI');
assert.strictEqual(findKeyValueRows(overviewNode, 'Bearer connected').length, 0,
	'Overview must not duplicate bearer connectivity state');
assert.strictEqual(findKeyValueRows(overviewNode, 'Modem state').length, 0,
	'Overview must omit the redundant modem-state row');
const modemStatusCard = overviewSections.filter(function(section) {
	return renderedText(section)[0] === 'Modem Status';
})[0];
const bandAndCellSection = overviewSections.filter(function(section) {
	return renderedText(section)[0] === 'Band and Cell Status';
})[0];

assert.ok(modemStatusCard && bandAndCellSection);
[ 'SIM present', 'SIM lock', 'Operator', 'Registration', 'Roaming',
	'SIM Number', 'ICCID' ].forEach(function(label) {
	assert.strictEqual(findKeyValueRows(modemStatusCard, label).length, 1,
		`${label} must be grouped under Modem Status`);
	assert.strictEqual(findKeyValueRows(bandAndCellSection, label).length, 0,
		`${label} must not be duplicated in Band and Cell Status`);
});
assert.strictEqual(renderedText(modemStatusCard).filter(function(value) {
	return value === 'SIMs';
}).length, 1, 'Modem Status must contain one SIMs subsection');
const modemStatusText = renderedText(modemStatusCard);
const mergedBandText = renderedText(bandAndCellSection);

assert.ok(modemStatusText.indexOf('SIM lock') < modemStatusText.indexOf('SIMs') &&
	modemStatusText.indexOf('SIMs') < modemStatusText.indexOf('Operator'),
'the SIMs subsection must appear below the modem-status rows');
assert.strictEqual(mergedBandText.includes('LTE Carrier Aggregation'), false,
	'Band and Cell Status must not contain a redundant CA subsection header');
assert.ok(mergedBandText.indexOf('Active LTE Bands') <
	mergedBandText.indexOf('RSRP'),
'carrier and signal rows must form one flow under Band and Cell Status');
assert.strictEqual(findKeyValueRows(overviewNode, 'Signal quality').length, 0,
	'Overview must omit the redundant Signal quality progress row');
assert.strictEqual(renderedOverview.includes('Signal Status'), false,
	'the separate Signal Status header must be removed');
const renderedIdentifiers = findNodes(overviewNode, function(node) {
	return node.tag === 'code' && hasClass(node, 'l850gl-mm-identifier');
});

assert.deepStrictEqual(renderedIdentifiers.map(function(node) {
	return renderedText(node).join('');
}), [ '359762080000001', '+628111000000', '8962100000000000001' ],
'the full identifiers explicitly requested by the user must be rendered without mutation');
assert.ok(renderedIdentifiers.every(function(node) {
	return node.attributes.dir === 'ltr' && node.attributes.tabindex === '0';
}), 'identifiers must remain selectable and copy-friendly');
const emptyIdentifierOverview = Object.assign({}, overviewResult, {
	identity: Object.assign({}, overviewResult.identity, { imei: '' }),
	sim: Object.assign({}, overviewResult.sim, { number: '', imsi: '', iccid: '' })
});

assert.strictEqual(widgets.overviewError(emptyIdentifierOverview, summary), null,
	'empty identity fields are a valid typed Unavailable state');
const emptyIdentifierNode = overviewView.render({
	list: listResult,
	entries: [ {
		summary: summary, overview: emptyIdentifierOverview, carrier: carrierResult
	} ]
});

[ 'IMEI', 'ICCID' ].forEach(function(label) {
	assert.deepStrictEqual(renderedText(findKeyValueRows(emptyIdentifierNode, label)[0]),
		[ label, 'Unavailable' ]);
});
assert.strictEqual(findKeyValueRows(emptyIdentifierNode, 'SIM Number').length, 0,
	'an empty backend SIM number must remove the complete row');
assert.strictEqual(findKeyValueRows(emptyIdentifierNode, 'IMSI').length, 0,
	'IMSI must remain hidden even if the typed backend field is present or empty');
const unavailableCarrierOverview = overviewView.render({
	list: listResult,
	entries: [ { summary: summary, overview: overviewResult } ]
});

[ 'Active LTE Bands', 'Active LTE Carriers', 'LTE CA Details',
	'Total Bandwidth' ].forEach(function(label) {
	const rows = findKeyValueRows(unavailableCarrierOverview, label);

	assert.strictEqual(rows.length, 1,
		`${label} must remain visible when the expert object is absent`);
	assert.deepStrictEqual(renderedText(rows[0]), [ label, 'Unavailable' ],
		`${label} must fail closed instead of displaying partial carrier data`);
});
[ 'Primary LTE Band', 'Secondary LTE Bands' ].forEach(function(label) {
	assert.strictEqual(findKeyValueRows(unavailableCarrierOverview, label).length, 0,
		`${label} must remain omitted when carrier data is unavailable`);
});
const carrierServingFallbackNode = overviewView.render({
	list: listResult,
	entries: [ {
		summary: summary,
		overview: Object.assign({}, overviewResult, {
			serving_cell: { state: 'unavailable', reason: 'refresh-pending' }
		}),
		carrier: carrierResult
	} ]
});

assert.strictEqual(
	findKeyValueRows(carrierServingFallbackNode, 'Serving cell status').length, 0,
	'Overview must omit the redundant serving-cell status row');
assert.deepStrictEqual(
	renderedText(findKeyValueRows(carrierServingFallbackNode, 'Serving EARFCN')[0]),
	[ 'Serving EARFCN', '1325' ]);
assert.deepStrictEqual(
	renderedText(findKeyValueRows(carrierServingFallbackNode, 'Serving PCI')[0]),
	[ 'Serving PCI', '0' ],
	'validated GTCAINFO fallback must retain PCI zero');
[ 'utran-', 'eutran-' ].forEach(function(internalPrefix) {
	assert.ok(!renderedOverview.some(function(value) { return value.includes(internalPrefix); }),
		`Overview must not expose the internal ${internalPrefix} band prefix`);
});
const lockNode = lockView.render({
	list: listResult,
	entries: [ { summary: summary, lock: lockResult, expert: expertResult } ]
});
assert.strictEqual(lockNode.tag, 'div');
assertPageStructure(lockNode, 'l850gl-mm-lock-page');
const renderedLock = renderedText(lockNode);
[ 'utran-', 'eutran-' ].forEach(function(internalPrefix) {
	assert.ok(!renderedLock.some(function(value) { return value.includes(internalPrefix); }),
		`Lock must not expose the internal ${internalPrefix} band prefix`);
});
const renderedBandGroups = findNodes(lockNode, function(node) {
	return node.attributes && typeof node.attributes.class === 'string' &&
		node.attributes.class.split(/\s+/).includes('l850gl-mm-band-group');
});

assert.strictEqual(renderedBandGroups.length, 1,
	'only the LTE/4G band-lock group must be displayed');
assert.ok(renderedText(renderedBandGroups[0]).includes('4G'));
assert.ok(!renderedText(renderedBandGroups[0]).includes('3G'));
for (const expected of [ 'Allowed mode', '3G / 4G', '3G only', '4G only',
	'Preferred mode', 'No preference', 'Prefer 3G', 'Prefer 4G' ]) {
	assert.ok(renderedLock.some(function(value) { return value.includes(expected); }),
		`Lock must render the persistent mode control ${expected}`);
}
assert.strictEqual(findKeyValueRows(lockNode, 'Allowed mode').length, 0,
	'the selected allowed mode must not be duplicated above its controls');
assert.strictEqual(findKeyValueRows(lockNode, 'Preferred mode').length, 0,
	'the selected preferred mode must not be duplicated above its controls');
assert.strictEqual(findKeyValueRows(lockNode, 'Reason').length, 0,
	'Band Lock must omit the verbose capability reason row');
assert.strictEqual(findKeyValueRows(lockNode, 'Capability').length, 0,
	'Lock must not render any capability status row');
assert.strictEqual(findKeyValueRows(lockNode, 'Scan capability').length, 0,
	'the PCI panel must replace its scan-capability row with a compact status dot');
assert.strictEqual(findKeyValueRows(lockNode, 'Selection reported by ModemManager').length, 0,
	'Band Lock must omit the internal selection-report row');
const modePolicyPanel = findNodes(lockNode, function(node) {
	return hasClass(node, 'l850gl-mm-mode-policy');
})[0];
const bandLockPanel = findNodes(lockNode, function(node) {
	return hasClass(node, 'l850gl-mm-band-lock');
})[0];
const unavailablePciPanel = findNodes(lockNode, function(node) {
	return hasClass(node, 'l850gl-mm-pci-lock');
})[0];
const unavailableScanDot = findNodes(unavailablePciPanel, function(node) {
	return hasClass(node, 'l850gl-mm-scan-status-dot');
})[0];

assert.ok(modePolicyPanel && bandLockPanel && unavailablePciPanel);
assert.ok(hasClass(unavailableScanDot, 'is-unavailable'),
	'an absent expert scan must use the fail-closed red status dot');
assert.strictEqual(unavailableScanDot.attributes.role, 'status');
assert.strictEqual(unavailableScanDot.attributes['aria-live'], 'polite');
assert.strictEqual(unavailableScanDot.attributes['aria-label'], 'Cell scan unavailable');
assert.strictEqual(unavailableScanDot.attributes.title, 'Cell scan unavailable');
assert.strictEqual(findKeyValueRows(modePolicyPanel, 'Current allowed mode').length, 0);
assert.strictEqual(findKeyValueRows(modePolicyPanel, 'Current preferred mode').length, 0);
assert.strictEqual(findKeyValueRows(bandLockPanel, 'Current allowed mode').length, 0);
assert.strictEqual(findKeyValueRows(bandLockPanel, 'Current preferred mode').length, 0);
assert.strictEqual(findKeyValueRows(bandLockPanel, 'Supported LTE bands').length, 0,
	'supported LTE bands must be represented only by the checkbox grid');
assert.strictEqual(findKeyValueRows(lockNode, 'Current allowed mode families').length, 0);
const explicitCurrentBandRows = findKeyValueRows(bandLockPanel, 'Current bands');

assert.strictEqual(explicitCurrentBandRows.length, 1);
assert.deepStrictEqual(renderedText(explicitCurrentBandRows[0]),
	[ 'Current bands', 'B1, B3' ]);
assert.strictEqual(findNodes(explicitCurrentBandRows[0], function(node) {
	return hasClass(node, 'label') && hasClass(node, 'notice');
}).length, 1, 'an explicit band lock must use a blue LuCI notice badge');
const lockDescription = findNodes(lockNode, function(node) {
	return hasClass(node, 'cbi-map-descr');
});

assert.strictEqual(lockDescription.length, 1);
assert.deepStrictEqual(renderedText(lockDescription[0]), [ 'Band Lock uses ModemManager' ]);
const automaticLockNode = lockView.render({
	list: listResult,
	entries: [ { summary: summary, lock: Object.assign({}, lockResult, {
		band_selection: 'automatic',
		current_bands: lockResult.supported_bands
	}), expert: expertResult } ]
});
const automaticCurrentBandRows = findKeyValueRows(automaticLockNode, 'Current bands');

assert.strictEqual(automaticCurrentBandRows.length, 1);
assert.deepStrictEqual(renderedText(automaticCurrentBandRows[0]),
	[ 'Current bands', 'Any Supported bands' ],
	'automatic band selection must not list every supported band');
assert.strictEqual(findNodes(automaticCurrentBandRows[0], function(node) {
	return hasClass(node, 'label') && hasClass(node, 'success');
}).length, 1, 'automatic band selection must use a green LuCI success badge');
const explicitAllLockNode = lockView.render({
	list: listResult,
	entries: [ { summary: summary, lock: Object.assign({}, lockResult, {
		band_selection: 'explicit',
		current_bands: lockResult.supported_bands
	}), expert: expertResult } ]
});
const explicitAllCurrentBandRows = findKeyValueRows(explicitAllLockNode, 'Current bands');

assert.strictEqual(explicitAllCurrentBandRows.length, 1);
assert.deepStrictEqual(renderedText(explicitAllCurrentBandRows[0]),
	[ 'Current bands', 'Any Supported bands' ],
	'an explicit set containing every supported band must be presented as unlocked');
assert.strictEqual(findNodes(explicitAllCurrentBandRows[0], function(node) {
	return hasClass(node, 'label') && hasClass(node, 'success');
}).length, 1, 'a fully unlocked explicit set must use a green LuCI success badge');
const renderedAvailableLock = lockView.render({
	list: listResult,
	entries: [ { summary: summary, lock: Object.assign({}, lockResult, {
		pci_lock: { state: 'available', mutable: true,
			reason: 'live-validated-l850-command-state-machine' }
	}), expert: availableExpertResult } ]
});
assert.strictEqual(renderedAvailableLock.tag, 'div');
const availablePciPanel = findNodes(renderedAvailableLock, function(node) {
	return hasClass(node, 'l850gl-mm-pci-lock');
})[0];
const scanControl = findNodes(availablePciPanel, function(node) {
	return hasClass(node, 'l850gl-mm-scan-control');
})[0];
const readyScanDot = scanControl.children[0];
const readyScanButton = scanControl.children[1];

assert.ok(hasClass(readyScanDot, 'l850gl-mm-scan-status-dot') &&
	hasClass(readyScanDot, 'is-ready'),
'an available cell scan must use the dynamic green status dot');
assert.strictEqual(readyScanDot.attributes['aria-label'], 'Cell scan available');
assert.ok(readyScanButton.tag === 'button' &&
	renderedText(readyScanButton).includes('Scan cells'),
'the status dot must remain immediately left of the Scan cells button');
assert.strictEqual(readyScanButton.attributes.disabled, null);
const availableCellLayout = findNodes(availablePciPanel, function(node) {
	return hasClass(node, 'l850gl-mm-cell-lock-layout');
})[0];
const availableCellInputGrid = findNodes(availableCellLayout, function(node) {
	return hasClass(node, 'l850gl-mm-cell-input-grid');
})[0];
const lockStatusBox = findNodes(availableCellLayout, function(node) {
	return hasClass(node, 'l850gl-mm-cell-status-box');
})[0];
const lockStatusValue = findNodes(lockStatusBox, function(node) {
	return hasClass(node, 'l850gl-mm-cell-status-value');
})[0];

assert.ok(availableCellLayout && availableCellInputGrid && lockStatusBox &&
	lockStatusValue, 'the inputs and standalone Lock status box must share one layout');
assert.strictEqual(availableCellLayout.children[0], availableCellInputGrid);
assert.strictEqual(availableCellLayout.children[1], lockStatusBox,
	'Lock status must be the separate right-hand box after the inputs');
assert.strictEqual(findKeyValueRows(renderedAvailableLock, 'Lock status').length, 0,
	'Lock status must no longer occupy a top key/value row');
assert.strictEqual(lockStatusValue.attributes.role, 'status');
assert.strictEqual(lockStatusValue.attributes['aria-live'], 'polite');
const lockedBadge = findNodes(lockStatusBox, function(node) {
	return hasClass(node, 'l850gl-mm-cell-lock-state') && hasClass(node, 'is-locked');
})[0];
const lockStatusNumbers = findNodes(lockStatusBox, function(node) {
	return hasClass(node, 'l850gl-mm-lock-status-number');
});

[ 'LOCK', 'EARFCN', '1325', 'PCI', '0' ].forEach(function(value) {
	assert.ok(renderedText(lockStatusBox).includes(value),
		`PCI lock summary must include ${value}`);
});
assert.ok(lockedBadge && renderedText(lockedBadge).includes('LOCK'),
	'a locked cell must use the dedicated red state text');
assert.deepStrictEqual(lockStatusNumbers.map(function(node) {
	return renderedText(node).join('');
}), [ '1325', '0' ], 'EARFCN and PCI numbers must receive the blue-number style');
const lockedDetail = findNodes(lockStatusBox, function(node) {
	return hasClass(node, 'l850gl-mm-lock-status-detail');
})[0];

assert.ok(lockedDetail && lockedDetail.children.length === 2,
	'locked EARFCN and PCI must occupy two clean symmetric detail cells');
assert.ok(hasClass(lockedDetail, 'cbi-section-node'),
	'locked EARFCN and PCI must occupy a dedicated LuCI detail card');
assert.strictEqual(renderedText(lockStatusBox).includes('B3'), false,
	'Lock status details must omit the redundant LTE band');
assert.strictEqual(renderedText(lockStatusBox).includes('Lock status'), false,
	'a locked cell must omit the redundant Lock status title');
[ 'Scan reason', 'NVM lock state', 'Configured EARFCN', 'Configured PCI',
	'Configured LTE band' ].forEach(function(label) {
	assert.ok(!renderedText(renderedAvailableLock).includes(label),
		`PCI summary must omit verbose field ${label}`);
});
const clearExpertResult = Object.assign({}, availableExpertResult, {
	lock: {
		state: 'clear',
		enabled: false,
		postcondition_verified: false,
		source: 'l850-nvm-via-modemmanager'
	}
});
const renderedClearLock = lockView.render({
	list: listResult,
	entries: [ { summary: summary, lock: Object.assign({}, lockResult, {
		pci_lock: { state: 'available', mutable: true,
			reason: 'live-validated-l850-command-state-machine' }
	}), expert: clearExpertResult } ]
});
const clearLockStatusBoxes = findNodes(renderedClearLock, function(node) {
	return hasClass(node, 'l850gl-mm-cell-status-box');
});

assert.strictEqual(clearLockStatusBoxes.length, 1);
assert.ok(renderedText(clearLockStatusBoxes[0]).includes('Lock status'));
assert.ok(renderedText(clearLockStatusBoxes[0]).includes('UNLOCK'));
assert.strictEqual(findNodes(clearLockStatusBoxes[0], function(node) {
	return hasClass(node, 'l850gl-mm-lock-status') && hasClass(node, 'is-unlocked');
}).length, 1, 'the unlocked status must use the centered two-row layout');
assert.strictEqual(findNodes(clearLockStatusBoxes[0], function(node) {
	return hasClass(node, 'l850gl-mm-cell-lock-state') && hasClass(node, 'is-unlocked');
}).length, 1, 'an unlocked cell must use the dedicated green state text');
assert.strictEqual(findNodes(clearLockStatusBoxes[0], function(node) {
	return hasClass(node, 'l850gl-mm-lock-status-number');
}).length, 0, 'an unlocked cell must not render stale EARFCN or PCI values');

const busyScanExpert = Object.assign({}, availableExpertResult, {
	scan: Object.assign({}, availableExpertResult.scan, {
		state: 'busy', available: false, reason: 'scan-in-progress'
	})
});
const busyScanLock = lockView.render({
	list: listResult,
	entries: [ { summary: summary, lock: Object.assign({}, lockResult, {
		pci_lock: { state: 'available', mutable: true,
			reason: 'live-validated-l850-command-state-machine' }
	}), expert: busyScanExpert } ]
});
const busyScanDot = findNodes(busyScanLock, function(node) {
	return hasClass(node, 'l850gl-mm-scan-status-dot');
})[0];
const busyScanButton = findNodes(busyScanLock, function(node) {
	return node.tag === 'button' && renderedText(node).includes('Scan cells');
})[0];

assert.ok(hasClass(busyScanDot, 'is-loading'));
assert.strictEqual(busyScanDot.attributes['aria-label'], 'Cell scan in progress…');
assert.strictEqual(busyScanButton.attributes.disabled, '');
const limitedScanExpert = Object.assign({}, availableExpertResult, {
	scan: Object.assign({}, availableExpertResult.scan, {
		state: 'rate_limited', available: false, reason: 'completion-cooldown',
		retry_after_ms: 3000
	})
});
const limitedScanLock = lockView.render({
	list: listResult,
	entries: [ { summary: summary, lock: Object.assign({}, lockResult, {
		pci_lock: { state: 'available', mutable: true,
			reason: 'live-validated-l850-command-state-machine' }
	}), expert: limitedScanExpert } ]
});
const limitedScanDot = findNodes(limitedScanLock, function(node) {
	return hasClass(node, 'l850gl-mm-scan-status-dot');
})[0];

assert.ok(hasClass(limitedScanDot, 'is-limited'));
assert.strictEqual(limitedScanDot.attributes['aria-label'],
	'Cell scan temporarily unavailable');

const scanCells = [
	{ type: 4, serving: true, earfcn: 1325, pci: 0, band: 3, rsrp: -90, rsrq: -10 },
	{ type: 5, serving: false, earfcn: 1650, pci: 42, band: 3, rsrp: -97, rsrq: -13 }
];
let interactiveRedraws = 0;
const lockModals = [];
const interactiveDom = {
	content: function(node, replacement) {
		interactiveRedraws++;
		node.children = [ replacement ];
	}
};
const interactiveLockUi = {
	showModal: function(title, body) { lockModals.push({ title: title, body: body }); },
	hideModal: function() {}
};
const interactiveApi = {
	cellScan: function(modemId, generation) {
		assert.strictEqual(modemId, summary.modem_id);
		assert.strictEqual(generation, summary.generation);
		const result = {
			schema: 4,
			generated_at: 2,
			ok: true,
			modem_id: modemId,
			generation: generation,
			state: 'scan_ready',
			source: 'modemmanager',
			cells: scanCells
		};

		return {
			then: function(callback) {
				callback(result);
				return { catch: function() { return this; } };
			}
		};
	}
};
const interactiveLockView = evaluate(
	'htdocs/luci-static/resources/view/l850gl-mm/lock.js', {
		dom: interactiveDom, poll: inert, ui: interactiveLockUi, view: view,
		api: interactiveApi, widgets: widgets
	});
const interactiveLock = interactiveLockView.render({
	list: listResult,
	entries: [ { summary: summary, lock: Object.assign({}, lockResult, {
		pci_lock: { state: 'available', mutable: true,
			reason: 'live-validated-l850-command-state-machine' }
		}), expert: availableExpertResult } ]
});
const liveEarfcnInput = findNodes(interactiveLock, function(node) {
	return node.tag === 'input' && node.attributes.id === 'l850gl-mm-earfcn-0';
})[0];
const livePciInput = findNodes(interactiveLock, function(node) {
	return node.tag === 'input' && node.attributes.id === 'l850gl-mm-pci-0';
})[0];
const liveApplyCellButton = findNodes(interactiveLock, function(node) {
	return node.tag === 'button' && renderedText(node).includes('Apply cell lock');
})[0];
const redrawsBeforeCellInput = interactiveRedraws;

assert.strictEqual(liveApplyCellButton.attributes.disabled, '',
	'an empty EARFCN must initially disable Apply cell lock');
liveEarfcnInput.attributes.input({ target: { value: '1325' } });
assert.strictEqual(interactiveRedraws, redrawsBeforeCellInput,
	'EARFCN input must not redraw the full Lock DOM or lose focus');
assert.strictEqual(liveApplyCellButton.disabled, false,
	'a valid EARFCN with empty PCI must enable Apply cell lock immediately');
liveApplyCellButton.attributes.click();
assert.strictEqual(lockModals.length, 1,
	'one click after valid input must open exactly one confirmation modal');
assert.strictEqual(lockModals[0].title, 'Apply PCI/EARFCN lock');
liveEarfcnInput.attributes.input({ target: { value: '-1' } });
assert.strictEqual(liveApplyCellButton.disabled, true,
	'an invalid EARFCN must disable Apply cell lock immediately');
liveEarfcnInput.attributes.input({ target: { value: '70546' } });
assert.strictEqual(liveApplyCellButton.disabled, true,
	'an EARFCN above the backend LTE bound must remain disabled');
liveEarfcnInput.attributes.input({ target: { value: '1.5' } });
assert.strictEqual(liveApplyCellButton.disabled, true,
	'a fractional EARFCN must remain disabled');
liveEarfcnInput.attributes.input({ target: { value: '0' } });
assert.strictEqual(liveApplyCellButton.disabled, false,
	'EARFCN zero must remain a valid LTE channel number');
liveEarfcnInput.attributes.input({ target: { value: '1325' } });
livePciInput.attributes.input({ target: { value: '504' } });
assert.strictEqual(liveApplyCellButton.disabled, true,
	'PCI greater than 503 must disable Apply cell lock immediately');
livePciInput.attributes.input({ target: { value: '' } });
assert.strictEqual(liveApplyCellButton.disabled, false,
	'an empty optional PCI must be valid');
livePciInput.attributes.input({ target: { value: '0' } });
assert.strictEqual(liveApplyCellButton.disabled, false,
	'PCI zero must be valid and enable Apply cell lock');
livePciInput.attributes.input({ target: { value: '503' } });
assert.strictEqual(liveApplyCellButton.disabled, false,
	'PCI 503 must remain valid at the inclusive upper bound');
livePciInput.attributes.input({ target: { value: '0' } });

[ Object.assign({}, availableExpertResult, { mutable: false }),
	Object.assign({}, availableExpertResult, { state: 'busy', mutable: false }) ]
	.forEach(function(blockedExpert, blockedIndex) {
		const blockedView = evaluate(
			'htdocs/luci-static/resources/view/l850gl-mm/lock.js', viewDependencies);
		const blockedNode = blockedView.render({
			list: listResult,
			entries: [ { summary: summary, lock: lockResult, expert: blockedExpert } ]
		});
		const blockedEarfcn = findNodes(blockedNode, function(node) {
			return node.tag === 'input' && node.attributes.id === 'l850gl-mm-earfcn-0';
		})[0];
		const blockedApply = findNodes(blockedNode, function(node) {
			return node.tag === 'button' && renderedText(node).includes('Apply cell lock');
		})[0];

		blockedEarfcn.attributes.input({ target: { value: '1325' } });
		assert.strictEqual(blockedApply.disabled, true,
			`blocked expert state ${blockedIndex + 1} must remain disabled after valid input`);
	});
const mode4gOnly = findNodes(interactiveLock, function(node) {
	return node.tag === 'input' && node.attributes.id ===
		'l850gl-mm-allowed-mode-0-2';
})[0];

assert.ok(mode4gOnly, 'persistent mode selection must render a 4G-only radio');
assert.ok(hasClass(mode4gOnly, 'cbi-input-radio'),
	'mode choices must use the native LuCI radio class');
mode4gOnly.attributes.change({ target: { checked: true, value: '4g' } });
const preferredNone = findNodes(interactiveLock, function(node) {
	return node.tag === 'input' && node.attributes.id ===
		'l850gl-mm-preferred-mode-0-0';
})[0];
const modeApplyButton = findNodes(interactiveLock, function(node) {
	return node.tag === 'button' && renderedText(node).includes('Apply mode selection');
})[0];

assert.ok(preferredNone.attributes.checked != null,
	'a single allowed mode must force the persistent preference to none');
assert.ok(preferredNone.attributes.disabled != null,
	'preferred-mode radios must be disabled for a single allowed mode');
assert.strictEqual(modeApplyButton.attributes.disabled, null,
	'changing a valid mode policy must enable its confirmation action');
const initialBandCheckboxes = findNodes(interactiveLock, function(node) {
	return node.tag === 'input' && /^l850gl-mm-band-0-[0-9]+$/.test(node.attributes.id);
});
const invertButton = findNodes(interactiveLock, function(node) {
	return node.tag === 'button' && renderedText(node).includes('Invert');
})[0];

assert.ok(invertButton, 'Band Lock must render an Invert button');
assert.ok(initialBandCheckboxes.every(function(node) {
	return hasClass(node, 'cbi-input-checkbox');
}), 'band choices must use the native LuCI checkbox class');
assert.strictEqual(initialBandCheckboxes.filter(function(node) {
	return node.attributes.checked != null;
}).length, 2);
invertButton.attributes.click();
const invertedBandCheckboxes = findNodes(interactiveLock, function(node) {
	return node.tag === 'input' && /^l850gl-mm-band-0-[0-9]+$/.test(node.attributes.id);
});
const invertedCheckedBands = invertedBandCheckboxes.filter(function(node) {
	return node.attributes.checked != null;
});

assert.strictEqual(invertedCheckedBands.length, 1,
	'Invert must flip every supported explicit LTE band locally');
assert.strictEqual(invertedCheckedBands[0].attributes.id, 'l850gl-mm-band-0-2');
const scanButton = findNodes(interactiveLock, function(node) {
	return node.tag === 'button' && renderedText(node).includes('Scan cells');
})[0];

assert.ok(scanButton, 'The PCI section must render the cell scan button');
scanButton.attributes.click();
const useButtons = findNodes(interactiveLock, function(node) {
	return node.tag === 'button' && renderedText(node).includes('Use');
});
const scanCards = findNodes(interactiveLock, function(node) {
	return node.attributes && typeof node.attributes.class === 'string' &&
		node.attributes.class.split(/\s+/).includes('l850gl-mm-cell-card');
});
const scanResultList = findNodes(interactiveLock, function(node) {
	return node.attributes && typeof node.attributes.class === 'string' &&
		node.attributes.class.split(/\s+/).includes('l850gl-mm-cell-cards');
})[0];
const compactScanText = renderedText(scanResultList);
const scanCardFields = findNodes(scanResultList, function(node) {
	return node.attributes && typeof node.attributes.class === 'string' &&
		node.attributes.class.split(/\s+/).includes('l850gl-mm-cell-card-field');
});
const scanHint = findNodes(interactiveLock, function(node) {
	return node.attributes && typeof node.attributes.class === 'string' &&
		node.attributes.class.split(/\s+/).includes('l850gl-mm-scan-hint');
})[0];

assert.strictEqual(useButtons.length, 0,
	'scan rows must not spend mobile space on Use buttons');
assert.strictEqual(scanCards.length, scanCells.length,
	'each validated scan result must render one tappable card');
assert.strictEqual(scanCardFields.length, scanCells.length * 5,
	'each scan card must retain exactly five compact information fields');
assert.strictEqual(findNodes(scanResultList, function(node) {
	return node.tag === 'table';
}).length, 0, 'scan results must not use a table');
assert.ok(scanCards.every(function(node) {
	return node.tag === 'button' && node.attributes.type === 'button' &&
		hasClass(node, 'cbi-section-node') && node.attributes.style == null;
}), 'each scan result must be a theme-neutral semantic LuCI card button');
const lockActionButtons = findNodes(interactiveLock, function(node) {
	return node.tag === 'button' && !hasClass(node, 'l850gl-mm-cell-card');
});

assert.ok(lockActionButtons.every(function(node) {
	return hasClass(node, 'btn') && hasClass(node, 'cbi-button');
}), 'Lock actions must use native LuCI button classes');
[ 'Band', 'EARFCN', 'PCI', 'RSRP', 'RSRQ', 'B3', '1325', '0', '-90 dBm', '-10 dB' ]
	.forEach(function(detail) {
		assert.ok(compactScanText.includes(detail),
			`compact scan rows must retain ${detail}`);
	});
assert.ok(compactScanText.indexOf('Band') < compactScanText.indexOf('EARFCN'),
	'Band must be the leftmost scan-result field');
assert.ok(!compactScanText.includes('Role'),
	'compact scan results must not display the internal serving role');
assert.ok(!compactScanText.includes('LTE type'),
	'compact scan results must not display the parser LTE type');
assert.ok(scanHint && renderedText(scanHint).includes('Tap line to use'));
assert.strictEqual(scanHint.attributes.style, undefined,
	'the scan-row instruction must inherit its theme-aware stylesheet');
assert.strictEqual(scanCards[0].tag, 'button',
	'native scan buttons must provide keyboard activation without custom key handlers');
scanCards[0].attributes.click();
const selectedEarfcn = findNodes(interactiveLock, function(node) {
	return node.tag === 'input' && node.attributes.id === 'l850gl-mm-earfcn-0';
})[0];
const selectedPci = findNodes(interactiveLock, function(node) {
	return node.tag === 'input' && node.attributes.id === 'l850gl-mm-pci-0';
})[0];
const cellInputGrid = findNodes(interactiveLock, function(node) {
	return node.attributes && typeof node.attributes.class === 'string' &&
		node.attributes.class.split(/\s+/).includes('l850gl-mm-cell-input-grid');
})[0];
const cellActions = findNodes(interactiveLock, function(node) {
	return node.attributes && typeof node.attributes.class === 'string' &&
		node.attributes.class.split(/\s+/).includes('l850gl-mm-cell-actions');
})[0];
const selectedScanCards = findNodes(interactiveLock, function(node) {
	return node.attributes && typeof node.attributes.class === 'string' &&
		node.attributes.class.split(/\s+/).includes('l850gl-mm-cell-card') &&
		node.attributes['aria-pressed'] === 'true';
});

assert.strictEqual(selectedEarfcn.attributes.value, '1325');
assert.strictEqual(selectedPci.attributes.value, '0',
	'the row action must preserve PCI zero');
assert.strictEqual(cellInputGrid.attributes.style, undefined,
	'EARFCN and PCI layout must be owned by the shared responsive stylesheet');
assert.ok(hasClass(cellActions, 'cbi-page-actions') && hasClass(cellActions, 'l850gl-mm-actions'),
	'Apply and Clear actions must use the shared LuCI action layout');
assert.strictEqual(selectedScanCards.length, 1,
	'the selected scan card must receive a visible selected state');
assert.ok(renderedText(interactiveLock).some(function(value) {
	return value.includes('Selected cell (EARFCN 1325, PCI 0)');
}), 'the row action must show which cell was copied');
const scanSelectedApply = findNodes(interactiveLock, function(node) {
	return node.tag === 'button' && renderedText(node).includes('Apply cell lock');
})[0];

assert.strictEqual(scanSelectedApply.attributes.disabled, null,
	'a validated scan-card selection must render Apply cell lock enabled');
scanSelectedApply.attributes.click();
assert.strictEqual(lockModals.length, 2,
	'a selected scan card must open confirmation on the first Apply click');
assertLabelTargets(interactiveLock, 'Lock');
const renderedSms = smsView.render({
	list: listResult,
	entries: [ { summary: summary, messages: smsResult } ]
});
assert.strictEqual(renderedSms.tag, 'div');
assertPageStructure(renderedSms, 'l850gl-mm-sms-page');
assert.ok(renderedText(renderedSms).includes('Load more'));
const smsPageHeading = findNodes(renderedSms, function(node) {
	return hasClass(node, 'l850gl-mm-sms-page-heading');
})[0];
const smsStatusDot = findNodes(smsPageHeading, function(node) {
	return hasClass(node, 'l850gl-mm-sms-status-dot');
})[0];
const compactFilter = findNodes(renderedSms, function(node) {
	return node.tag === 'select' && hasClass(node, 'l850gl-mm-sms-filter-select');
})[0];
const loadedCount = findNodes(renderedSms, function(node) {
	return hasClass(node, 'l850gl-mm-sms-loaded-count');
})[0];

assert.ok(renderedText(smsPageHeading).includes('SMS'));
assert.ok(hasClass(smsStatusDot, 'is-ready'),
	'a healthy SMS cache must render a green-ready header status');
assert.strictEqual(smsStatusDot.attributes.role, 'status');
assert.strictEqual(smsStatusDot.attributes['aria-live'], 'polite');
assert.strictEqual(findNodes(renderedSms, function(node) {
	return hasClass(node, 'l850gl-mm-device-title');
}).length, 0, 'the compact folder filter must replace the large modem title');
assert.ok(compactFilter, 'the folder selector must remain available in the compact toolbar');
assert.strictEqual(compactFilter.attributes['aria-label'], 'Folder');
assert.deepStrictEqual(renderedText(compactFilter),
	[ 'All messages', 'Inbox', 'Outbox', 'Drafts', 'Unknown' ],
	'the compact filter must retain every supported SMS folder');
assert.ok(hasClass(loadedCount, 'notice'));
assert.deepStrictEqual(renderedText(loadedCount), [ '0 loaded' ]);
assert.strictEqual(findKeyValueRows(renderedSms, 'Revision').length, 0,
	'the SMS summary must omit the technical cache revision');
assert.strictEqual(findKeyValueRows(renderedSms, 'Messaging cache').length, 0,
	'the cache state must move from a technical row to the header dot');
assert.strictEqual(findKeyValueRows(renderedSms, 'Loaded messages').length, 0,
	'the loaded count must move from a key/value row to the compact toolbar');
assert.strictEqual(findKeyValueRows(renderedSms, 'Request-token capacity').length, 0);
assert.strictEqual(findKeyValueRows(renderedSms,
	'Request-token maximum age (seconds)').length, 0);
const smsToolbar = findNodes(renderedSms, function(node) {
	return hasClass(node, 'l850gl-mm-sms-toolbar');
})[0];
let composeToggle = findNodes(renderedSms, function(node) {
	return node.tag === 'button' && hasClass(node, 'l850gl-mm-compose-toggle');
})[0];

assert.ok(composeToggle, 'Write SMS must render as a compact toolbar button');
assert.ok(hasClass(composeToggle, 'btn') && hasClass(composeToggle, 'cbi-button'));
assert.strictEqual(renderedText(composeToggle).join(''), 'Write SMS');
assert.strictEqual(composeToggle.attributes['aria-expanded'], 'false');
assert.ok(smsToolbar.children.indexOf(compactFilter) < smsToolbar.children.indexOf(loadedCount));
assert.ok(smsToolbar.children.indexOf(loadedCount) < smsToolbar.children.indexOf(composeToggle),
	'filter, loaded count, and Write SMS must share the same top toolbar row');
assert.strictEqual(findNodes(renderedSms, function(node) {
	return node.tag === 'form' && hasClass(node, 'l850gl-mm-compose-form');
}).length, 0, 'the Write SMS form must remain closed by default');
composeToggle.attributes.click();
const openComposerNode = smsView.render({
	list: listResult,
	entries: [ { summary: summary, messages: smsResult } ]
});
composeToggle = findNodes(openComposerNode, function(node) {
	return node.tag === 'button' && hasClass(node, 'l850gl-mm-compose-toggle');
})[0];
const openComposer = findNodes(openComposerNode, function(node) {
	return node.tag === 'form' && hasClass(node, 'l850gl-mm-compose-form');
})[0];

assert.strictEqual(composeToggle.attributes['aria-expanded'], 'true');
assert.ok(openComposer,
	'clicking Write SMS must open its full form and survive a polling redraw');
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
const binarySmsNode = smsView.render({
	list: listResult,
	entries: [ { summary: summary, messages: binarySms } ]
});
const binarySmsText = renderedText(binarySmsNode);
assert.ok(binarySmsText.includes('Binary SMS payload (not displayed)'));
assert.ok(binarySmsText.includes(
	'This SMS contains binary data. Its raw payload is intentionally not exposed or displayed.'));
const receivedCard = findNodes(binarySmsNode, function(node) {
	return hasClass(node, 'l850gl-mm-sms-card');
})[0];
const receivedRows = findNodes(receivedCard, function(node) {
	return hasClass(node, 'l850gl-mm-kv-row');
});

assert.deepStrictEqual(receivedRows.map(function(node) {
	return renderedText(node)[0];
}), [ 'State', 'From', 'Timestamp' ],
'received cards must show only the essential SMS fields');
assert.strictEqual(findNodes(receivedCard, function(node) {
	return hasClass(node, 'label') && hasClass(node, 'success');
}).length, 1, 'received SMS state must be green');
assert.strictEqual(findNodes(receivedCard, function(node) {
	return hasClass(node, 'l850gl-mm-sms-message');
}).length, 1, 'the SMS body must use its dedicated readable marker');
assert.strictEqual(findNodes(receivedCard, function(node) {
	return hasClass(node, 'l850gl-mm-sms-body-title') &&
		renderedText(node).join('') === 'Message:';
}).length, 1, 'the full-width SMS body must have a Message: heading above it');
assert.strictEqual(findKeyValueRows(receivedCard, 'Message').length, 0,
	'the SMS body must not remain constrained by the metadata columns');
assert.ok(findNodes(binarySmsNode, function(node) {
	return node.tag === 'button';
}).every(function(node) {
	return hasClass(node, 'btn') && hasClass(node, 'cbi-button');
}), 'SMS actions must use native LuCI button classes');
assertLabelTargets(binarySmsNode, 'SMS');
const sentSms = Object.assign({}, binarySms, {
	messages: [ Object.assign({}, binarySms.messages[0], {
		sms_id: 'sms-sent', folder: 'outbox', direction: 'outgoing',
		state: 'sent', number: '<redacted-destination>', text: 'Hello',
		has_binary_data: false
	}) ]
});
const sentSmsNode = smsView.render({
	list: listResult,
	entries: [ { summary: summary, messages: sentSms } ]
});
const sentCard = findNodes(sentSmsNode, function(node) {
	return hasClass(node, 'l850gl-mm-sms-card');
})[0];
const sentRows = findNodes(sentCard, function(node) {
	return hasClass(node, 'l850gl-mm-kv-row');
});

assert.deepStrictEqual(sentRows.map(function(node) {
	return renderedText(node)[0];
}), [ 'State', 'To', 'Timestamp' ],
'sent cards must show only the essential SMS fields');
assert.strictEqual(findNodes(sentCard, function(node) {
	return hasClass(node, 'label') && hasClass(node, 'notice');
}).length, 1, 'sent SMS state must be blue');
const numericSms = Object.assign({}, binarySms, {
	messages: [ Object.assign({}, binarySms.messages[0], {
		sms_id: 'sms-numeric', text: 'OTP 123456, call +62817033 on 2026-07-28.',
		has_binary_data: false
	}) ]
});
const numericSmsNode = smsView.render({
	list: listResult,
	entries: [ { summary: summary, messages: numericSms } ]
});
const numberButtons = findNodes(numericSmsNode, function(node) {
	return node.tag === 'button' && hasClass(node, 'l850gl-mm-sms-number');
});

assert.deepStrictEqual(numberButtons.map(function(node) {
	return renderedText(node).join('');
}), [ '123456', '+62817033', '2026-07-28' ],
'number-like tokens in an SMS must retain their exact copyable formatting');
assert.ok(numberButtons.every(function(node) {
	return node.attributes.type === 'button' && node.attributes.title === 'Copy number' &&
		typeof node.attributes.click === 'function';
}), 'every number-like token must expose a keyboard-accessible copy action');
const inboundNumericSms = Object.assign({}, binarySms, {
	messages: [ Object.assign({}, binarySms.messages[0], {
		sms_id: 'sms-inbound-numeric', text: '123456', has_binary_data: false
	}) ]
});
const inboundNumericNode = smsView.render({
	list: listResult,
	entries: [ { summary: summary, messages: inboundNumericSms } ]
});
const inboundNumericBody = findNodes(inboundNumericNode, function(node) {
	return hasClass(node, 'l850gl-mm-sms-message');
})[0];
const inboundNumericButton = findNodes(inboundNumericBody, function(node) {
	return node.tag === 'button' && hasClass(node, 'l850gl-mm-sms-number');
})[0];

assert.deepStrictEqual(renderedText(inboundNumericBody), [ '123456' ],
	'an exact digits-only inbound SMS body must remain visible without added or lost text');
assert.ok(inboundNumericButton && renderedText(inboundNumericButton).join('') === '123456',
	'an exact digits-only inbound SMS must expose the whole body as one copy action');
const outboundNumericSms = Object.assign({}, binarySms, {
	messages: [ Object.assign({}, binarySms.messages[0], {
		sms_id: 'sms-outbound-numeric', folder: 'outbox', direction: 'outgoing',
		state: 'sent', text: '654321', has_binary_data: false
	}) ]
});
const outboundNumericNode = smsView.render({
	list: listResult,
	entries: [ { summary: summary, messages: outboundNumericSms } ]
});
const outboundNumericCard = findNodes(outboundNumericNode, function(node) {
	return hasClass(node, 'l850gl-mm-sms-card');
})[0];
const outboundNumericButton = findNodes(outboundNumericCard, function(node) {
	return node.tag === 'button' && hasClass(node, 'l850gl-mm-sms-number');
})[0];

assert.ok(renderedText(findNodes(outboundNumericCard, function(node) {
	return hasClass(node, 'l850gl-mm-sms-message');
})[0]).join('') === '654321',
	'an exact digits-only outbound SMS card must keep its complete body visible');
assert.ok(outboundNumericButton && renderedText(outboundNumericButton).join('') === '654321',
	'an exact digits-only outbound SMS must expose the whole body as one copy action');
const navigatorDescriptor = Object.getOwnPropertyDescriptor(global, 'navigator');
let copiedNumber = null;

Object.defineProperty(global, 'navigator', {
	configurable: true,
	value: {
		clipboard: {
			writeText: function(value) {
				copiedNumber = value;
				return Promise.resolve();
			}
		}
	}
});
try {
	assert.ok(inboundNumericButton.attributes.click() instanceof Promise);
	assert.strictEqual(copiedNumber, '123456',
		'the inbound number action must copy the exact digits-only SMS body');
	assert.ok(outboundNumericButton.attributes.click() instanceof Promise);
	assert.strictEqual(copiedNumber, '654321',
		'the outbound number action must copy the exact digits-only SMS body');
}
finally {
	if (navigatorDescriptor)
		Object.defineProperty(global, 'navigator', navigatorDescriptor);
	else
		delete global.navigator;
}

function interactionSmsMessage(smsId, number, direction, text) {
	const incoming = direction === 'incoming';

	return {
		sms_id: smsId,
		folder: incoming ? 'inbox' : 'outbox',
		direction: direction,
		state: incoming ? 'received' : 'sent',
		number: number,
		text: text,
		text_truncated: false,
		timestamp: incoming ? '2026-07-28T15:00:00Z' : '2026-07-28T15:01:00Z',
		discharge_timestamp: '',
		pdu_type: incoming ? 'deliver' : 'submit',
		delivery_state: 0,
		message_reference: 1,
		storage: 'mt',
		has_binary_data: false
	};
}

function interactionSmsEnvelope(messages, overrides) {
	return Object.assign({}, smsResult, {
		revision: 91,
		cache_state: 'ready',
		cache_truncated: false,
		folder: 'all',
		messages: messages,
		next_cursor: '',
		has_more: false
	}, overrides || {});
}

function smsPlainTarget() {
	return { closest: function() { return null; } };
}

function smsChildTarget() {
	return { closest: function() { return {}; } };
}

function fakeSmsTimers() {
	let nextId = 1;
	const timers = new Map();

	return {
		window: {
			setTimeout: function(callback, milliseconds) {
				const id = nextId++;

				timers.set(id, { callback: callback, milliseconds: milliseconds });
				return id;
			},
			clearTimeout: function(id) { timers.delete(id); }
		},
		count: function() { return timers.size; },
		runAll: function() {
			const pending = Array.from(timers.values());

			timers.clear();
			pending.forEach(function(timer) { timer.callback(); });
		}
	};
}

function createSmsInteractionHarness(messages, options) {
	const settings = options || {};
	const initial = settings.initial || interactionSmsEnvelope(messages);
	const modals = [];
	const testDom = {
		content: function(node, replacement) { node.children = [ replacement ]; }
	};
	const testUi = Object.assign({
		showModal: function(title, body) { modals.push({ title: title, body: body }); },
		hideModal: function() {},
		addNotification: function() {}
	}, settings.ui || {});
	const testApi = Object.assign({
		listModems: function() { return Promise.resolve(listResult); },
		listSms: function() { return Promise.resolve(initial); },
		sendSms: function() { throw new Error('unexpected sendSms call'); },
		deleteSms: function() { throw new Error('unexpected deleteSms call'); }
	}, settings.api || {});
	const module = evaluate('htdocs/luci-static/resources/view/l850gl-mm/sms.js', {
		dom: testDom,
		poll: inert,
		ui: testUi,
		view: view,
		api: testApi,
		widgets: widgets
	});
	const root = module.render({
		list: listResult,
		entries: [ { summary: summary, messages: initial } ]
	});

	return { root: root, modals: modals };
}

function smsCards(node) {
	return findNodes(node, function(candidate) {
		return hasClass(candidate, 'l850gl-mm-sms-card');
	});
}

function smsCardWithText(node, text) {
	return smsCards(node).find(function(card) {
		return renderedText(card).includes(text);
	});
}

function smsBulkButton(node) {
	return findNodes(node, function(candidate) {
		return candidate.tag === 'button' && hasClass(candidate, 'l850gl-mm-sms-bulk-delete');
	})[0];
}

function successfulDeleteResult(smsId) {
	return {
		schema: 4,
		generated_at: 2,
		ok: true,
		modem_id: summary.modem_id,
		generation: summary.generation,
		messaging_generation: 7,
		sms_id: smsId,
		deleted: true
	};
}

function overviewCarrierError(code, options) {
	const settings = options || {};
	const includeIdentity = settings.identity == null ?
		code !== 'busy' : settings.identity;
	const result = {
		schema: 4,
		generated_at: 2,
		ok: false,
		state: code,
		error: Object.assign({
			code: code,
			message: 'Mock carrier ' + code,
			retryable: true
		}, settings.error || {})
	};

	if (includeIdentity) {
		result.modem_id = summary.modem_id;
		result.generation = summary.generation;
	}
	if (code === 'rate_limited')
		result.retry_after_ms = settings.retry_after_ms == null ?
			4981 : settings.retry_after_ms;
	return Object.assign(result, settings.result || {});
}

function createOverviewCarrierPollingHarness(options) {
	const settings = options || {};
	const listResults = settings.list_results || [ listResult ];
	const carrierResults = settings.carrier_results || [];
	const servingUnavailable = Object.assign({}, overviewResult, {
		serving_cell: { state: 'unavailable', reason: 'refresh-pending' }
	});
	const currentSummaries = Object.create(null);
	let listIndex = 0;
	let carrierIndex = 0;
	let pollCallback = null;
	let content = null;

	function next(sequence, index) {
		return sequence[Math.min(index, sequence.length - 1)];
	}

	const pollingApi = {
		listModems: function() {
			const result = next(listResults, listIndex++);

			if (result instanceof Error)
				return Promise.reject(result);
			if (widgets.listError(result) == null) {
				Object.keys(currentSummaries).forEach(function(modemId) {
					delete currentSummaries[modemId];
				});
				result.modems.forEach(function(entry) {
					currentSummaries[entry.modem_id] = entry;
				});
			}
			return Promise.resolve(result);
		},
		getOverview: function(modemId) {
			const current = currentSummaries[modemId] || summary;

			return Promise.resolve(Object.assign({}, servingUnavailable, {
				modem_id: current.modem_id,
				generation: current.generation
			}));
		},
		getLockStatus: function(modemId) {
			const current = currentSummaries[modemId] || summary;

			return Promise.resolve(Object.assign({}, lockResult, {
				modem_id: current.modem_id,
				generation: current.generation
			}));
		},
		getCarrierInfo: function(modemId, generation) {
			const configured = next(carrierResults, carrierIndex++);
			const result = typeof configured === 'function' ?
				configured(modemId, generation) : configured;

			return result instanceof Error ? Promise.reject(result) :
				Promise.resolve(result);
		}
	};
	const pollingDom = {
		content: function(node, replacement) {
			node.children = [ replacement ];
		}
	};
	const polling = {
		add: function(callback, seconds) {
			assert.strictEqual(seconds, 10);
			pollCallback = callback;
		}
	};
	const module = evaluate('htdocs/luci-static/resources/view/l850gl-mm/overview.js', {
		dom: pollingDom,
		poll: polling,
		view: view,
		api: pollingApi,
		widgets: widgets
	});

	return {
		start: async function() {
			const root = module.render(await module.load());

			content = findNodes(root, function(node) {
				return node.attributes.id === 'l850gl-mm-overview';
			})[0];
			assert.ok(content);
			assert.ok(pollCallback);
			return content;
		},
		poll: function() {
			return pollCallback().then(function() { return content; });
		},
		content: function() { return content; }
	};
}

function assertCarrierUnavailable(node, context) {
	[ 'Active LTE Bands', 'Active LTE Carriers', 'LTE CA Details',
		'Total Bandwidth' ].forEach(function(label) {
		assert.deepStrictEqual(renderedText(findKeyValueRows(node, label)[0]),
			[ label, 'Unavailable' ], context + ': ' + label);
	});
}

async function testOverviewLoadMerge() {
	const calls = [];
	const loadingApi = {
		listModems: function() {
			calls.push([ 'listModems' ]);
			return Promise.resolve(listResult);
		},
		getOverview: function(modemId) {
			calls.push([ 'getOverview', modemId ]);
			return Promise.resolve(overviewResult);
		},
		getLockStatus: function(modemId) {
			calls.push([ 'getLockStatus', modemId ]);
			return Promise.resolve(lockResult);
		},
		getCarrierInfo: function(modemId, generation) {
			calls.push([ 'getCarrierInfo', modemId, generation ]);
			return Promise.resolve(carrierResult);
		}
	};
	const module = evaluate('htdocs/luci-static/resources/view/l850gl-mm/overview.js',
		Object.assign({}, viewDependencies, { api: loadingApi }));
	const snapshot = await module.load();

	assert.deepStrictEqual(calls, [
		[ 'listModems' ],
		[ 'getCarrierInfo', 'l850gl-mm-test', 4 ],
		[ 'getOverview', 'l850gl-mm-test' ],
		[ 'getLockStatus', 'l850gl-mm-test' ]
	]);
	assert.strictEqual(snapshot.entries[0].overview, overviewResult);
	assert.strictEqual(snapshot.entries[0].lock, lockResult);
	assert.strictEqual(snapshot.entries[0].carrier, carrierResult,
		'Overview polling must merge Overview, Lock, and expert snapshots');

	const unavailableModule = evaluate(
		'htdocs/luci-static/resources/view/l850gl-mm/overview.js',
		Object.assign({}, viewDependencies, {
			api: Object.assign({}, loadingApi, {
				getCarrierInfo: function() {
					return Promise.reject(new Error('expert object absent'));
				}
			})
		}));
	const unavailableSnapshot = await unavailableModule.load();
	const unavailableNode = unavailableModule.render(unavailableSnapshot);

	[ 'Active LTE Bands', 'Active LTE Carriers', 'LTE CA Details',
		'Total Bandwidth' ].forEach(function(label) {
		assert.deepStrictEqual(renderedText(
			findKeyValueRows(unavailableNode, label)[0]), [ label, 'Unavailable' ]);
	});

	const unavailableLockModule = evaluate(
		'htdocs/luci-static/resources/view/l850gl-mm/overview.js',
		Object.assign({}, viewDependencies, {
			api: Object.assign({}, loadingApi, {
				getLockStatus: function() {
					return Promise.reject(new Error('lock status unavailable'));
				}
			})
		}));
	const unavailableLockSnapshot = await unavailableLockModule.load();
	const unavailableLockNode = unavailableLockModule.render(unavailableLockSnapshot);

	assert.deepStrictEqual(renderedText(
		findKeyValueRows(unavailableLockNode, 'Current bands')[0]),
	[ 'Current bands', 'Unavailable' ]);
	assert.strictEqual(findKeyValueRows(unavailableLockNode, 'Manufacturer').length, 1,
		'a failed Lock snapshot must not hide the rest of Overview');
}

async function testOverviewCarrierLastKnownGood() {
	const previousDateNow = Date.now;
	const busy = overviewCarrierError('busy');
	const rateLimited = overviewCarrierError('rate_limited');
	const notReady = overviewCarrierError('not_ready');
	const timeout = overviewCarrierError('timeout');
	const dependencyUnavailable = overviewCarrierError('dependency_unavailable', {
		identity: false
	});
	const updatedCarrier = Object.assign({}, carrierResult, {
		generated_at: 2,
		active_bands: [ 8 ],
		primary: {
			index: 1, band: 8, earfcn: 3500, pci: 222,
			dl_bandwidth_mhz: 10, ul_bandwidth_mhz: 5
		},
		secondary: [],
		active_carriers: 1
	});
	const threeCarrier = Object.assign({}, carrierResult, {
		active_bands: [ 3, 7, 1 ],
		secondary: carrierResult.secondary.concat([ {
			index: 3, band: 1, earfcn: 325, pci: 123,
			dl_bandwidth_mhz: 10, ul_bandwidth_mhz: null
		} ]),
		active_carriers: 3
	});
	let nowMs = 1000;

	Date.now = function() { return nowMs; };
	try {
		const topologyHarness = createOverviewCarrierPollingHarness({
			carrier_results: [
				threeCarrier, carrierResult, updatedCarrier, threeCarrier
			]
		});
		let topologyContent = await topologyHarness.start();
		const expectedTopologies = [
			[ 'B3 + B7 + B1', '3' ],
			[ 'B3 + B7', '2' ],
			[ 'B8', '1' ],
			[ 'B3 + B7 + B1', '3' ]
		];

		for (let topologyIndex = 0; topologyIndex < expectedTopologies.length;
		     topologyIndex++) {
			if (topologyIndex > 0) {
				nowMs++;
				topologyContent = await topologyHarness.poll();
			}
			assert.deepStrictEqual(renderedText(findKeyValueRows(topologyContent,
				'Active LTE Bands')[0]),
				[ 'Active LTE Bands', expectedTopologies[topologyIndex][0] ],
				'a coherent 3CA/2CA/1CA topology must replace the prior snapshot');
			assert.deepStrictEqual(renderedText(findKeyValueRows(topologyContent,
				'Active LTE Carriers')[0]),
				[ 'Active LTE Carriers', expectedTopologies[topologyIndex][1] ]);
			assert.strictEqual(renderedText(topologyContent).includes('Unavailable'), false,
				'a coherent CA topology change must never render an unavailable gap');
		}

		nowMs = 1000;
		const transientHarness = createOverviewCarrierPollingHarness({
			carrier_results: [
				carrierResult, rateLimited, updatedCarrier, busy, notReady,
				timeout, dependencyUnavailable, dependencyUnavailable
			]
		});
		let content = await transientHarness.start();

		assert.deepStrictEqual(renderedText(
			findKeyValueRows(content, 'Active LTE Bands')[0]),
		[ 'Active LTE Bands', 'B3 + B7' ]);
		nowMs = 1001;
		content = await transientHarness.poll();
		assert.deepStrictEqual(renderedText(
			findKeyValueRows(content, 'Active LTE Bands')[0]),
		[ 'Active LTE Bands', 'B3 + B7' ],
		'an immediate rate limit must retain the validated initial carrier snapshot');
		assert.strictEqual(
			findKeyValueRows(content, 'Serving cell status').length, 0,
			'the removed serving-cell status row must stay absent during polling');
		assert.deepStrictEqual(renderedText(
			findKeyValueRows(content, 'Serving EARFCN')[0]),
		[ 'Serving EARFCN', '1325' ]);

		nowMs = 2000;
		content = await transientHarness.poll();
		assert.deepStrictEqual(renderedText(
			findKeyValueRows(content, 'Active LTE Bands')[0]),
		[ 'Active LTE Bands', 'B8' ],
		'a new validated success must replace the last-known-good carrier');
		assert.deepStrictEqual(renderedText(
			findKeyValueRows(content, 'Serving EARFCN')[0]),
		[ 'Serving EARFCN', '3500' ]);

		nowMs = 2001;
		content = await transientHarness.poll();
		assert.deepStrictEqual(renderedText(
			findKeyValueRows(content, 'Active LTE Bands')[0]),
		[ 'Active LTE Bands', 'B8' ],
		'a canonical identity-less busy envelope must retain an exact-generation cache');
		nowMs = 2002;
		content = await transientHarness.poll();
		assert.deepStrictEqual(renderedText(
			findKeyValueRows(content, 'Active LTE Bands')[0]),
		[ 'Active LTE Bands', 'B8' ],
		'a same-generation retryable not-ready state must retain the validated cache');
		nowMs = 2003;
		content = await transientHarness.poll();
		assert.deepStrictEqual(renderedText(
			findKeyValueRows(content, 'Active LTE Bands')[0]),
		[ 'Active LTE Bands', 'B8' ],
		'a same-generation retryable timeout must retain the validated cache');
		nowMs = 32000;
		content = await transientHarness.poll();
		assert.deepStrictEqual(renderedText(
			findKeyValueRows(content, 'Active LTE Bands')[0]),
		[ 'Active LTE Bands', 'B8' ],
		'an identity-less dependency outage may use the cache at exactly thirty seconds');
		nowMs = 32001;
		content = await transientHarness.poll();
		assertCarrierUnavailable(content,
			'a transient error after the fixed thirty-second TTL must fail closed');
		assert.strictEqual(
			findKeyValueRows(content, 'Serving cell status').length, 0,
			'the removed serving-cell status row must remain absent after cache expiry');
		assert.strictEqual(findKeyValueRows(content, 'Serving EARFCN').length, 0);

		const malformedSuccess = Object.assign({}, carrierResult, {
			primary: Object.assign({}, carrierResult.primary, { pci: 504 })
		});
		const malformedResponse = overviewCarrierError('malformed_response', {
			error: { retryable: false }
		});
		const failClosedCases = [
			[ 'schema mismatch', Object.assign({}, rateLimited, { schema: 3 }) ],
			[ 'generation mismatch', Object.assign({}, rateLimited, {
				generation: summary.generation + 1
			}) ],
			[ 'malformed carrier success', malformedSuccess ],
			[ 'malformed carrier response', malformedResponse ],
			[ 'transport failure', new Error('carrier transport failed') ],
			[ 'non-retryable timeout', overviewCarrierError('timeout', {
				error: { retryable: false }
			}) ],
			[ 'identity-less not-ready', overviewCarrierError('not_ready', {
				identity: false
			}) ],
			[ 'non-retryable busy', overviewCarrierError('busy', {
				error: { retryable: false }
			}) ],
			[ 'out-of-range retry_after_ms', overviewCarrierError('rate_limited', {
				retry_after_ms: 5001
			}) ],
			[ 'partial busy identity', Object.assign({}, busy, {
				modem_id: summary.modem_id
			}) ],
			[ 'mismatched transient code and state', overviewCarrierError('busy', {
				error: { code: 'rate_limited' }
			}) ]
		];

		for (const failureCase of failClosedCases) {
			nowMs = 1000;
			const harness = createOverviewCarrierPollingHarness({
				carrier_results: [ carrierResult, failureCase[1], busy ]
			});

			await harness.start();
			nowMs = 1001;
			content = await harness.poll();
			assertCarrierUnavailable(content,
				failureCase[0] + ' must invalidate the cache');
			nowMs = 1002;
			content = await harness.poll();
			assertCarrierUnavailable(content,
				failureCase[0] + ' must not be resurrected by a later busy response');
		}

		nowMs = 1000;
		const recoveryHarness = createOverviewCarrierPollingHarness({
			carrier_results: [
				threeCarrier, malformedResponse, updatedCarrier, busy
			]
		});

		await recoveryHarness.start();
		nowMs = 1001;
		content = await recoveryHarness.poll();
		assertCarrierUnavailable(content,
			'a malformed carrier response must still fail closed');
		nowMs = 1002;
		content = await recoveryHarness.poll();
		assert.deepStrictEqual(renderedText(findKeyValueRows(content,
			'Active LTE Bands')[0]), [ 'Active LTE Bands', 'B8' ],
		'a later valid topology must recover automatically without reloading the page');
		nowMs = 1003;
		content = await recoveryHarness.poll();
		assert.deepStrictEqual(renderedText(findKeyValueRows(content,
			'Active LTE Bands')[0]), [ 'Active LTE Bands', 'B8' ],
		'the automatically recovered topology must repopulate the transient cache');

		const nextSummary = Object.assign({}, summary, { generation: 5 });
		const nextList = Object.assign({}, listResult, { modems: [ nextSummary ] });
		const generationHarness = createOverviewCarrierPollingHarness({
			list_results: [ listResult, nextList ],
			carrier_results: [ carrierResult, busy ]
		});

		nowMs = 1000;
		await generationHarness.start();
		nowMs = 1001;
		content = await generationHarness.poll();
		assertCarrierUnavailable(content,
			'a modem generation change must prune the prior carrier cache');

		const emptyList = Object.assign({}, listResult, { modems: [] });
		const removalHarness = createOverviewCarrierPollingHarness({
			list_results: [ listResult, emptyList, listResult ],
			carrier_results: [ carrierResult, busy ]
		});

		nowMs = 1000;
		await removalHarness.start();
		nowMs = 1001;
		await removalHarness.poll();
		nowMs = 1002;
		content = await removalHarness.poll();
		assertCarrierUnavailable(content,
			'a removed and re-added inventory entry must not recover an old cache');

		const incompatibleList = Object.assign({}, listResult, { schema: 3 });
		const invalidInventoryHarness = createOverviewCarrierPollingHarness({
			list_results: [ listResult, incompatibleList, listResult ],
			carrier_results: [ carrierResult, busy ]
		});

		nowMs = 1000;
		await invalidInventoryHarness.start();
		nowMs = 1001;
		await invalidInventoryHarness.poll();
		nowMs = 1002;
		content = await invalidInventoryHarness.poll();
		assertCarrierUnavailable(content,
			'an invalid inventory envelope must clear every cached carrier');
	} finally {
		Date.now = previousDateNow;
	}
}

async function testSmsInteractions() {
	const previousWindow = global.window;

	try {
		const exactNumber = '+628111000';
		const conversationMessages = [
			interactionSmsMessage('sms-chat-in', exactNumber, 'incoming', 'Exact inbound'),
			interactionSmsMessage('sms-chat-out', exactNumber, 'outgoing', 'Exact outbound'),
			interactionSmsMessage('sms-chat-near', exactNumber + '9', 'incoming', 'Near number'),
			interactionSmsMessage('sms-chat-local', '08111000', 'outgoing', 'Local alias')
		];
		let timers = fakeSmsTimers();
		let conversationSendArguments = null;

		timers.window.crypto = {
			getRandomValues: function(bytes) {
				for (let index = 0; index < bytes.length; index++)
					bytes[index] = index + 1;
				return bytes;
			}
		};
		global.window = timers.window;
		let harness = createSmsInteractionHarness(conversationMessages, {
			api: {
				sendSms: function() {
					conversationSendArguments = Array.from(arguments);
					return Promise.resolve({
						schema: 4,
						generated_at: 2,
						ok: false,
						error: {
							code: 'not_ready',
							message: 'Mock send stop after argument capture',
							retryable: true
						}
					});
				}
			}
		});
		let listComposeToggle = findNodes(harness.root, function(node) {
			return node.tag === 'button' && hasClass(node, 'l850gl-mm-compose-toggle');
		})[0];

		listComposeToggle.attributes.click();
		const listRecipient = findNodes(harness.root, function(node) {
			return node.tag === 'input' && node.attributes.type === 'tel';
		})[0];

		listRecipient.attributes.input({ target: { value: '+629999999' } });
		listComposeToggle = findNodes(harness.root, function(node) {
			return node.tag === 'button' && hasClass(node, 'l850gl-mm-compose-toggle');
		})[0];
		listComposeToggle.attributes.click();
		let card = smsCardWithText(harness.root, 'Exact inbound');
		let prevented = 0;
		const opened = card.attributes.click({
			target: smsPlainTarget(),
			preventDefault: function() { prevented++; }
		});

		assert.ok(opened instanceof Promise, 'a quick card tap must start in-tab navigation');
		await opened;
		const conversationHeader = findNodes(harness.root, function(node) {
			return hasClass(node, 'l850gl-mm-conversation-header');
		});
		const chatCards = smsCards(harness.root);

		assert.strictEqual(conversationHeader.length, 1,
			'a quick tap must open one in-tab conversation');
		assert.ok(renderedText(conversationHeader[0]).includes(exactNumber),
			'the conversation heading must preserve the exact reported number');
		assert.strictEqual(chatCards.length, 2,
			'conversation matching must include only literal number matches');
		assert.ok(renderedText(harness.root).includes('Exact inbound'));
		assert.ok(renderedText(harness.root).includes('Exact outbound'));
		assert.ok(!renderedText(harness.root).includes('Near number'));
		assert.ok(!renderedText(harness.root).includes('Local alias'));
		assert.ok(chatCards.some(function(node) { return hasClass(node, 'l850gl-mm-sms-chat-inbound'); }));
		assert.ok(chatCards.some(function(node) { return hasClass(node, 'l850gl-mm-sms-chat-outbound'); }));
		const chatList = findNodes(harness.root, function(node) {
			return hasClass(node, 'l850gl-mm-sms-chat-list');
		})[0];
		let inlineComposer = findNodes(harness.root, function(node) {
			return node.tag === 'form' && hasClass(node, 'l850gl-mm-conversation-compose');
		})[0];
		const chatPanel = findNodes(harness.root, function(node) {
			return hasClass(node, 'l850gl-mm-panel') && Array.isArray(node.children) &&
				node.children.indexOf(chatList) !== -1;
		})[0];

		assert.ok(inlineComposer,
			'a numeric conversation must render an always-open inline composer');
		assert.ok(chatPanel.children.indexOf(inlineComposer) > chatPanel.children.indexOf(chatList),
			'the conversation composer must follow the chat messages at the bottom');
		assert.strictEqual(findNodes(harness.root, function(node) {
			return node.tag === 'details' && hasClass(node, 'l850gl-mm-compose');
		}).length, 0, 'conversation mode must not retain the top Write SMS panel');
		assert.strictEqual(findNodes(inlineComposer, function(node) {
			return node.tag === 'input' && node.attributes.type === 'tel';
		}).length, 0, 'the conversation recipient must not be editable');
		assert.strictEqual(smsBulkButton(harness.root), undefined,
			'normal conversation mode must replace Delete all with the inline composer');
		let inlineText = findNodes(inlineComposer, function(node) {
			return node.tag === 'textarea';
		})[0];

		inlineText.attributes.input({ target: { value: 'Conversation reply' } });
		await inlineComposer.attributes.submit({ preventDefault: function() {} });
		assert.ok(conversationSendArguments,
			'the conversation composer must use the existing SMS send path');
		assert.strictEqual(conversationSendArguments[3], exactNumber,
			'conversation send must lock the recipient to the exact chat number');
		assert.strictEqual(conversationSendArguments[4], 'Conversation reply');
		assert.match(conversationSendArguments[5], /^smsop-[0-9a-f]{32}$/);

		card = smsCardWithText(harness.root, 'Exact inbound');
		card.attributes.pointerdown({
			button: 0,
			clientX: 5,
			clientY: 5,
			target: smsPlainTarget()
		});
		timers.runAll();
		assert.strictEqual(findNodes(harness.root, function(node) {
			return hasClass(node, 'l850gl-mm-conversation-compose');
		}).length, 0, 'selection mode must temporarily replace the conversation composer');
		assert.strictEqual(renderedText(smsBulkButton(harness.root)).join(''), 'Delete',
			'conversation selection must retain the selected-message delete action');
		const cancelConversationSelection = findNodes(harness.root, function(node) {
			return node.tag === 'button' && hasClass(node, 'l850gl-mm-sms-selection-cancel');
		})[0];

		cancelConversationSelection.attributes.click();
		inlineComposer = findNodes(harness.root, function(node) {
			return node.tag === 'form' && hasClass(node, 'l850gl-mm-conversation-compose');
		})[0];
		inlineText = findNodes(inlineComposer, function(node) {
			return node.tag === 'textarea';
		})[0];
		assert.ok(inlineComposer,
			'canceling conversation selection must restore the inline composer');
		assert.ok(renderedText(inlineText).includes('Conversation reply'),
			'conversation draft text must survive selection-mode redraws');
		assert.strictEqual(smsBulkButton(harness.root), undefined);

		timers = fakeSmsTimers();
		global.window = timers.window;
		harness = createSmsInteractionHarness([
			interactionSmsMessage('sms-hold', exactNumber, 'incoming', 'Hold this message')
		]);
		card = smsCardWithText(harness.root, 'Hold this message');
		assert.strictEqual(renderedText(smsBulkButton(harness.root)).join(''), 'Delete all');
		card.attributes.pointerdown({
			button: 0,
			clientX: 10,
			clientY: 10,
			target: smsPlainTarget()
		});
		assert.strictEqual(timers.count(), 1, 'primary pointer hold must arm one timer');
		timers.runAll();
		let selectedCard = smsCardWithText(harness.root, 'Hold this message');

		assert.ok(hasClass(selectedCard, 'l850gl-mm-sms-card-selected'));
		assert.ok(hasClass(selectedCard, 'is-selected'));
		assert.strictEqual(selectedCard.attributes['aria-pressed'], 'true');
		assert.strictEqual(selectedCard.attributes['aria-selected'], 'true');
		assert.strictEqual(findNodes(selectedCard, function(node) {
			return hasClass(node, 'l850gl-mm-sms-selection-check');
		}).length, 1, 'a selected message must expose a visible check marker');
		assert.strictEqual(renderedText(smsBulkButton(harness.root)).join(''), 'Delete',
			'Delete all must become Delete while selection mode is active');
		prevented = 0;
		card.attributes.click({
			target: smsPlainTarget(),
			preventDefault: function() { prevented++; }
		});
		assert.strictEqual(prevented, 1,
			'the synthetic click following long-press must be consumed');
		assert.strictEqual(findNodes(harness.root, function(node) {
			return hasClass(node, 'l850gl-mm-conversation-header');
		}).length, 0, 'the long-press synthetic click must not open chat');
		assert.ok(hasClass(smsCardWithText(harness.root, 'Hold this message'),
			'l850gl-mm-sms-card-selected'));

		timers = fakeSmsTimers();
		global.window = timers.window;
		harness = createSmsInteractionHarness([
			interactionSmsMessage('sms-move', exactNumber, 'incoming', 'Cancel on move')
		]);
		card = smsCardWithText(harness.root, 'Cancel on move');
		card.attributes.pointerdown({
			button: 0,
			clientX: 4,
			clientY: 4,
			target: smsPlainTarget()
		});
		card.attributes.pointermove({ clientX: 15, clientY: 4 });
		assert.strictEqual(timers.count(), 0,
			'pointer movement beyond ten pixels must cancel long-press');
		timers.runAll();
		assert.strictEqual(findNodes(harness.root, function(node) {
			return hasClass(node, 'l850gl-mm-sms-card-selected');
		}).length, 0);
		assert.strictEqual(renderedText(smsBulkButton(harness.root)).join(''), 'Delete all');

		timers = fakeSmsTimers();
		global.window = timers.window;
		harness = createSmsInteractionHarness([
			interactionSmsMessage('sms-child', exactNumber, 'incoming', 'OTP 123456')
		]);
		card = smsCardWithText(harness.root, '123456');
		card.attributes.pointerdown({ button: 0, target: smsChildTarget() });
		assert.strictEqual(timers.count(), 0,
			'pointerdown on an embedded control must not arm card selection');
		card.attributes.click({ target: smsChildTarget() });
		assert.strictEqual(findNodes(harness.root, function(node) {
			return hasClass(node, 'l850gl-mm-conversation-header');
		}).length, 0, 'an embedded-control click must not open chat');
		const copyButton = findNodes(card, function(node) {
			return node.tag === 'button' && hasClass(node, 'l850gl-mm-sms-number');
		})[0];
		const trashButton = findNodes(card, function(node) {
			return node.tag === 'button' && hasClass(node, 'l850gl-mm-sms-delete-icon');
		})[0];
		let stopped = 0;

		await copyButton.attributes.click({
			preventDefault: function() { prevented++; },
			stopPropagation: function() { stopped++; }
		});
		assert.strictEqual(stopped, 1, 'copy action must stop card click propagation');
		assert.strictEqual(trashButton.attributes.title, 'Delete SMS');
		assert.strictEqual(trashButton.attributes['aria-label'], 'Delete SMS');
		stopped = 0;
		trashButton.attributes.pointerdown({ stopPropagation: function() { stopped++; } });
		trashButton.attributes.click({
			preventDefault: function() { prevented++; },
			stopPropagation: function() { stopped++; }
		});
		assert.strictEqual(stopped, 2,
			'trash pointer and click actions must stay isolated from the card');
		assert.strictEqual(harness.modals.length, 1,
			'trash icon must open the existing single-message confirmation');

		timers = fakeSmsTimers();
		global.window = timers.window;
		harness = createSmsInteractionHarness([
			interactionSmsMessage('sms-key', exactNumber, 'incoming', 'Keyboard message')
		]);
		card = smsCardWithText(harness.root, 'Keyboard message');
		prevented = 0;
		card.attributes.keydown({
			key: ' ',
			target: smsPlainTarget(),
			preventDefault: function() { prevented++; }
		});
		assert.strictEqual(prevented, 1);
		assert.ok(hasClass(smsCardWithText(harness.root, 'Keyboard message'),
			'l850gl-mm-sms-card-selected'), 'Space must select a focused message');
		selectedCard = smsCardWithText(harness.root, 'Keyboard message');
		selectedCard.attributes.keydown({
			key: 'Escape',
			target: smsPlainTarget(),
			preventDefault: function() { prevented++; }
		});
		assert.strictEqual(renderedText(smsBulkButton(harness.root)).join(''), 'Delete all',
			'Escape must leave selection mode');
		card = smsCardWithText(harness.root, 'Keyboard message');
		const keyboardOpen = card.attributes.keydown({
			key: 'Enter',
			target: smsPlainTarget(),
			preventDefault: function() { prevented++; }
		});
		await keyboardOpen;
		assert.strictEqual(findNodes(harness.root, function(node) {
			return hasClass(node, 'l850gl-mm-conversation-header');
		}).length, 1, 'Enter must open the focused conversation');
		const content = findNodes(harness.root, function(node) {
			return node.attributes && node.attributes.id === 'l850gl-mm-sms';
		})[0];
		await content.attributes.keydown({
			key: 'Escape',
			preventDefault: function() { prevented++; }
		});
		assert.strictEqual(findNodes(harness.root, function(node) {
			return hasClass(node, 'l850gl-mm-conversation-header');
		}).length, 0, 'Escape must return from the conversation view');
	}
	finally {
		if (previousWindow === undefined)
			delete global.window;
		else
			global.window = previousWindow;
	}
}

async function testSmsBulkDeletion() {
	const previousWindow = global.window;
	const timers = fakeSmsTimers();
	const messages = [
		interactionSmsMessage('sms-bulk-1', '+6281001', 'incoming', 'Bulk first'),
		interactionSmsMessage('sms-bulk-2', '+6281002', 'incoming', 'Bulk second'),
		interactionSmsMessage('sms-bulk-3', '+6281003', 'outgoing', 'Bulk third')
	];
	const deleteCalls = [];
	const pendingDeletes = [];

	global.window = timers.window;
	try {
		const harness = createSmsInteractionHarness(messages, {
			api: {
				deleteSms: function(modemId, generation, messagingGeneration, smsId, confirm) {
					deleteCalls.push(smsId);
					assert.strictEqual(modemId, summary.modem_id);
					assert.strictEqual(generation, summary.generation);
					assert.strictEqual(messagingGeneration, 7);
					assert.strictEqual(confirm, true);
					return new Promise(function(resolve) {
						pendingDeletes.push({ smsId: smsId, resolve: resolve });
					});
				}
			}
		});
		let card = smsCardWithText(harness.root, 'Bulk first');

		card.attributes.keydown({
			key: ' ', target: smsPlainTarget(), preventDefault: function() {}
		});
		card = smsCardWithText(harness.root, 'Bulk second');
		card.attributes.click({ target: smsPlainTarget() });
		card = smsCardWithText(harness.root, 'Bulk third');
		card.attributes.click({ target: smsPlainTarget() });
		const bulk = smsBulkButton(harness.root);

		assert.strictEqual(renderedText(bulk).join(''), 'Delete');
		bulk.attributes.click();
		assert.strictEqual(harness.modals.length, 1);
		const confirm = findNodes(harness.modals[0].body, function(node) {
			return node.tag === 'button' && hasClass(node, 'cbi-button-negative') &&
				renderedText(node).join('') === 'Delete';
		})[0];
		const operation = confirm.attributes.click();

		assert.deepStrictEqual(deleteCalls, [ 'sms-bulk-1' ],
			'bulk delete must dispatch only one mutation at a time');
		pendingDeletes[0].resolve(successfulDeleteResult('sms-bulk-1'));
		await Promise.resolve();
		await Promise.resolve();
		assert.deepStrictEqual(deleteCalls, [ 'sms-bulk-1', 'sms-bulk-2' ],
			'the second delete must wait for confirmation of the first');
		pendingDeletes[1].resolve({
			schema: 4,
			generated_at: 3,
			ok: false,
			error: { code: 'busy', message: 'Mock busy', retryable: true }
		});
		await operation;
		assert.deepStrictEqual(deleteCalls, [ 'sms-bulk-1', 'sms-bulk-2' ],
			'an unconfirmed bulk result must stop without retrying or dispatching later IDs');
		assert.ok(renderedText(harness.root).some(function(value) {
			return value.includes('Bulk deletion stopped: 1 deleted');
		}), 'partial bulk deletion must report its exact stopping point');
	}
	finally {
		if (previousWindow === undefined)
			delete global.window;
		else
			global.window = previousWindow;
	}
}

async function testSmsDeleteAllPreparation() {
	const previousWindow = global.window;
	const timers = fakeSmsTimers();
	const first = interactionSmsMessage('sms-page-1', '+6282001', 'incoming', 'Page first');
	const second = interactionSmsMessage('sms-page-2', '+6282002', 'incoming', 'Page second');
	const third = interactionSmsMessage('sms-page-3', '+6282003', 'outgoing', 'Page third');
	const firstPage = interactionSmsEnvelope([ first, second ], {
		next_cursor: second.sms_id,
		has_more: true
	});
	const secondPage = interactionSmsEnvelope([ third ]);
	const cursors = [];

	global.window = timers.window;
	try {
		let harness = createSmsInteractionHarness([ first, second ], {
			initial: firstPage,
			api: {
				listSms: function(modemId, folder, limit, cursor) {
					assert.strictEqual(modemId, summary.modem_id);
					assert.strictEqual(folder, 'all');
					assert.strictEqual(limit, 100);
					cursors.push(cursor);
					return Promise.resolve(cursor === '' ? firstPage : secondPage);
				}
			}
		});
		await smsBulkButton(harness.root).attributes.click();
		assert.deepStrictEqual(cursors, [ '', second.sms_id ],
			'Delete all must enumerate every cursor page before confirmation');
		assert.strictEqual(harness.modals.length, 1);
		assert.ok(renderedText(harness.modals[0].body).some(function(value) {
			return value.includes('Delete all 3 SMS messages');
		}), 'Delete all confirmation must cover the fully paginated snapshot');

		const truncated = interactionSmsEnvelope([ first ], {
			cache_state: 'ready-truncated',
			cache_truncated: true
		});

		harness = createSmsInteractionHarness([ first ], {
			initial: truncated,
			api: {
				listSms: function() { return Promise.resolve(truncated); }
			}
		});
		await smsBulkButton(harness.root).attributes.click();
		assert.strictEqual(harness.modals.length, 0,
			'a truncated inventory must fail closed before delete confirmation');
		assert.ok(renderedText(harness.root).some(function(value) {
			return value.includes('Delete all is unavailable');
		}), 'cache truncation must be explained without claiming every SMS is enumerable');
	}
	finally {
		if (previousWindow === undefined)
			delete global.window;
		else
			global.window = previousWindow;
	}
}

function testFocusedFolderPolling() {
	let pollCallback = null;
	let contentRedraws = 0;
	let headerRedraws = 0;
	let pollIndex = 0;
	const compose = {};
	const folderSelect = {
		tagName: 'SELECT',
		closest: function() { return null; }
	};
	const composeInput = {
		tagName: 'INPUT',
		closest: function(selector) {
			return selector === '.l850gl-mm-compose' ? compose : null;
		}
	};
	const documentDescriptor = Object.getOwnPropertyDescriptor(global, 'document');
	const pollResults = [
		Object.assign({}, smsResult, { cache_state: 'loading' }),
		Object.assign({}, smsResult, {
			cache_state: 'ready-truncated',
			cache_truncated: true
		})
	];
	const pollingApi = {
		listModems: function() { return Promise.resolve(listResult); },
		listSms: function() { return Promise.resolve(pollResults[pollIndex++]); }
	};
	const pollingDom = {
		content: function(node, replacement) {
			if (node.attributes.id === 'l850gl-mm-sms-header')
				headerRedraws++;
			else if (node.attributes.id === 'l850gl-mm-sms')
				contentRedraws++;
			node.children = [ replacement ];
		}
	};
	const polling = {
		add: function(callback, seconds) {
			assert.strictEqual(seconds, 10);
			pollCallback = callback;
		}
	};
	const pollingView = evaluate(
		'htdocs/luci-static/resources/view/l850gl-mm/sms.js', {
			dom: pollingDom, poll: polling, ui: inert, view: view,
			api: pollingApi, widgets: widgets
		});
	const pollingNode = pollingView.render({
		list: listResult,
		entries: [ { summary: summary, messages: smsResult } ]
	});
	const content = findNodes(pollingNode, function(node) {
		return node.attributes.id === 'l850gl-mm-sms';
	})[0];
	const header = findNodes(pollingNode, function(node) {
		return node.attributes.id === 'l850gl-mm-sms-header';
	})[0];
	function statusDot() {
		return findNodes(header, function(node) {
			return hasClass(node, 'l850gl-mm-sms-status-dot');
		})[0];
	}

	content.contains = function(node) {
		return node === folderSelect || node === composeInput || node === compose;
	};
	Object.defineProperty(global, 'document', {
		configurable: true,
		value: { activeElement: folderSelect }
	});

	return pollCallback().then(function() {
		assert.strictEqual(contentRedraws, 1,
			'a focused Folder select must not defer a ten-second SMS polling redraw');
		assert.strictEqual(headerRedraws, 1);
		assert.ok(hasClass(statusDot(), 'is-loading'),
			'the header dot must follow a loading cache snapshot');
		global.document.activeElement = composeInput;
		return pollCallback();
	}).then(function() {
		assert.strictEqual(contentRedraws, 1,
			'a focused Write SMS field must still protect the draft from polling redraws');
		assert.strictEqual(headerRedraws, 2,
			'the cache-status header must update even while message redraw is deferred');
		assert.ok(hasClass(statusDot(), 'is-limited'),
			'a truncated ready cache must not be shown as fully green');
	}).finally(function() {
		if (documentDescriptor)
			Object.defineProperty(global, 'document', documentDescriptor);
		else
			delete global.document;
	});
}
const windowDescriptor = Object.getOwnPropertyDescriptor(global, 'window');
let numericSendArguments = null;
const numericSendApi = {
	sendSms: function() {
		numericSendArguments = Array.from(arguments);
		return {
			then: function(callback) {
				callback({
					schema: 4,
					generated_at: 2,
					ok: false,
					error: {
						code: 'not_ready',
						message: 'Mock send stop after argument capture',
						retryable: true
					}
				});
				return { catch: function() { return this; } };
			}
		};
	}
};

Object.defineProperty(global, 'window', {
	configurable: true,
	value: {
		crypto: {
			getRandomValues: function(bytes) {
				for (let index = 0; index < bytes.length; index++)
					bytes[index] = index + 1;
				return bytes;
			}
		}
	}
});
try {
	const numericSendView = evaluate(
		'htdocs/luci-static/resources/view/l850gl-mm/sms.js', {
			dom: interactiveDom, poll: inert, ui: inert, view: view,
			api: numericSendApi, widgets: widgets
		});
	const numericSendNode = numericSendView.render({
		list: listResult,
		entries: [ { summary: summary, messages: smsResult } ]
	});
	const numericComposeToggle = findNodes(numericSendNode, function(node) {
		return node.tag === 'button' && hasClass(node, 'l850gl-mm-compose-toggle');
	})[0];

	numericComposeToggle.attributes.click();
	const recipientInput = findNodes(numericSendNode, function(node) {
		return node.tag === 'input' && node.attributes.type === 'tel';
	})[0];
	const messageInput = findNodes(numericSendNode, function(node) {
		return node.tag === 'textarea';
	})[0];
	const composeForm = findNodes(numericSendNode, function(node) {
		return node.tag === 'form' && hasClass(node, 'l850gl-mm-compose-form');
	})[0];

	recipientInput.attributes.input({ target: { value: '+628123456789' } });
	messageInput.attributes.input({ target: { value: '123456' } });
	composeForm.attributes.submit({ preventDefault: function() {} });
	assert.ok(numericSendArguments, 'numeric SMS submission must reach the shared RPC API');
	assert.strictEqual(numericSendArguments[3], '+628123456789');
	assert.strictEqual(numericSendArguments[4], '123456',
		'a digits-only SMS body must reach send_sms unchanged');
	assert.match(numericSendArguments[5], /^smsop-[0-9a-f]{32}$/,
		'numeric SMS submission must retain its CSPRNG client token');
}
finally {
	if (windowDescriptor)
		Object.defineProperty(global, 'window', windowDescriptor);
	else
		delete global.window;
}

function withViewport(width, callback) {
	const previousWindow = global.window;

	global.window = { innerWidth: width };
	try {
		return callback();
	}
	finally {
		if (previousWindow === undefined)
			delete global.window;
		else
			global.window = previousWindow;
	}
}

function renderFreshView(name, snapshot, width) {
	return withViewport(width, function() {
		const module = evaluate(`htdocs/luci-static/resources/view/l850gl-mm/${name}.js`,
			viewDependencies);

		return normalizeDom(module.render(snapshot));
	});
}

function renderFreshScannedLock(width) {
	return withViewport(width, function() {
		const module = evaluate('htdocs/luci-static/resources/view/l850gl-mm/lock.js', {
			dom: interactiveDom, poll: inert, ui: inert, view: view,
			api: interactiveApi, widgets: widgets
		});
		const node = module.render({
			list: listResult,
			entries: [ { summary: summary, lock: Object.assign({}, lockResult, {
				pci_lock: { state: 'available', mutable: true,
					reason: 'live-validated-l850-command-state-machine' }
			}), expert: availableExpertResult } ]
		});
		const button = findNodes(node, function(candidate) {
			return candidate.tag === 'button' && renderedText(candidate).includes('Scan cells');
		})[0];

		button.attributes.click();
		return normalizeDom(node);
	});
}

const overviewParitySnapshot = {
	list: listResult,
	entries: [ {
		summary: summary, overview: overviewResult, lock: lockResult,
		carrier: carrierResult
	} ]
};
const smsParityResult = Object.assign({}, binarySms, {
	has_more: true,
	next_cursor: 'sms-next'
});
const smsParitySnapshot = {
	list: listResult,
	entries: [ { summary: summary, messages: smsParityResult } ]
};

assert.deepStrictEqual(
	renderFreshView('overview', overviewParitySnapshot, 1440),
	renderFreshView('overview', overviewParitySnapshot, 360),
	'Overview desktop and phone must render the exact same information tree');
assert.deepStrictEqual(
	renderFreshScannedLock(1440),
	renderFreshScannedLock(360),
	'Lock desktop and phone must render the same controls and scanned-cell details');
assert.deepStrictEqual(
	renderFreshView('sms', smsParitySnapshot, 1440),
	renderFreshView('sms', smsParitySnapshot, 360),
	'SMS desktop and phone must render the same messages, metadata, and actions');
const incompatibleOverview = renderedText(overviewView.render({
	list: Object.assign({}, listResult, { schema: 1 }), entries: []
}));
assert.ok(incompatibleOverview.some(function(value) { return /schema/i.test(value); }));
const incompatibleSms = smsView.render({
	list: Object.assign({}, listResult, { schema: 1 }), entries: []
});
const incompatibleSmsDot = findNodes(incompatibleSms, function(node) {
	return hasClass(node, 'l850gl-mm-sms-status-dot');
})[0];

assert.ok(hasClass(incompatibleSmsDot, 'is-unavailable'),
	'an incompatible or unavailable SMS snapshot must never show a green status dot');

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
	assert.doesNotMatch(source, /\b(?:localStorage|sessionStorage)\b/);
});

const uiCss = read('htdocs/luci-static/resources/l850gl-mm/l850gl-mm.css');
const overviewSource = read('htdocs/luci-static/resources/view/l850gl-mm/overview.js');
for (const forbidden of [
	'ports', 'drivers', 'device_path', 'dbus_path', 'sysfs',
	'ip_address', 'gateway', 'dns', 'credentials', 'diagnostics'
]) {
	assert.ok(!overviewSource.includes(forbidden),
		`Overview must not render diagnostic/private field ${forbidden}`);
}
assert.ok(!overviewSource.includes('rescan'));
assert.ok(overviewSource.includes('l850gl-mm-overview-list'));
assert.ok(overviewSource.includes('cbi-section l850gl-mm-overview-section'));
assert.ok(!overviewSource.includes('l850gl-mm-overview-grid'));
assert.ok(!overviewSource.includes('l850gl-mm-overview-column'));
assert.ok(!overviewSource.includes('l850gl-mm-device-title'));
assert.ok(!overviewSource.includes('widgets.activeLabel(identity.model, true)'));
assert.ok(overviewSource.includes('api.getLockStatus(summary.modem_id)'),
	'Overview must use the typed Lock snapshot to distinguish all bands from a lock');
assert.ok(overviewSource.includes('const CARRIER_CACHE_MAX_ENTRIES = 64'),
	'the last-known-good carrier cache must remain inventory bounded');
assert.ok(overviewSource.includes('const CARRIER_CACHE_TTL_MS = 30000'),
	'the last-known-good carrier cache must expire after thirty seconds');
[ 'busy', 'dependency_unavailable', 'not_ready', 'rate_limited', 'timeout' ]
	.forEach(function(state) {
		assert.ok(overviewSource.includes("'" + state + "'"),
			`the reviewed retryable ${state} state must consult the bounded cache`);
	});
assert.ok(overviewSource.includes('delete carrierCache[summary.modem_id]'),
	'non-transient and expired carrier results must evict the current cache entry');
assert.ok(overviewSource.includes("_('any(automatic)')"));
assert.ok(overviewSource.includes("_('Firmware')"));
assert.ok(overviewSource.includes("_('SIMs')"));
assert.ok(!overviewSource.includes("_('Signal Status')"));
assert.ok(!overviewSource.includes("_('Signal quality')"));
assert.ok(!overviewSource.includes("_('Modem state')"));
assert.ok(overviewSource.includes('if (sim.number)'),
	'Overview must omit the SIM Number row when the backend value is empty');
assert.ok(!overviewSource.includes("_('IMSI')"));
assert.ok(!overviewSource.includes("_('Bearer connected')"));
assert.ok(!overviewSource.includes("_('Capabilities')"));
assert.ok(!overviewSource.includes("_('Primary carrier #%d')"));
assert.ok(!overviewSource.includes("title: '3G'") &&
	!overviewSource.includes("title: '4G'"),
'Overview must not reintroduce 3G/4G prefixes in Current bands');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-overview-list\s*\{[\s\S]*?display:\s*block[\s\S]*?width:\s*100%/,
	'Overview must use one full-width vertical list on every viewport');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-overview-section \.l850gl-mm-kv-row\.cbi-value\s*\{[\s\S]*?grid-template-columns:\s*minmax\(8\.5rem, 38%\) minmax\(0, 1fr\)/,
	'Overview must use the standard LuCI desktop label/value proportion');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-overview-section \.l850gl-mm-kv-row\.cbi-value\s*\{[\s\S]*?border-bottom:\s*1px solid var\(--border-color-low/,
	'Overview rows must retain subtle LuCI-themed separators on desktop and mobile');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-overview-section \.l850gl-mm-kv-row\.cbi-value:last-child\s*\{[\s\S]*?border-bottom:\s*0/,
	'the final Overview row must not leave a stray divider below its section');
assert.match(uiCss,
	/\.l850gl-mm-page\.l850gl-mm-lock-page \.l850gl-mm-kv-row\.cbi-value\s*\{[\s\S]*?border-bottom:\s*1px solid var\(--border-color-low/,
	'Lock key/value rows must use the same LuCI-themed separators');
assert.match(uiCss,
	/\.l850gl-mm-page\.l850gl-mm-lock-page \.l850gl-mm-form-row\.cbi-value\s*\{[\s\S]*?border-bottom:\s*1px solid var\(--border-color-low/,
	'Lock mode and band form rows must also use LuCI-themed separators');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-band-lock \.l850gl-mm-kv-row\.cbi-value\s*\{[\s\S]*?grid-template-columns:\s*minmax\(10rem, 12rem\) minmax\(0, 1fr\)[\s\S]*?gap:\s*\.75rem/,
	'Current bands must align with the Requested bands control column on desktop');
assert.ok(!uiCss.includes(
	'.l850gl-mm-page.l850gl-mm-lock-page .l850gl-mm-kv-row.cbi-value:last-child'),
	'the only Lock key/value row in a group must retain its separator');
assert.match(uiCss,
	/\.l850gl-mm-page\.l850gl-mm-sms-page \.l850gl-mm-kv-row\.cbi-value\s*\{[\s\S]*?border-bottom:\s*1px solid var\(--border-color-low/,
	'SMS metadata rows must use LuCI-themed separators on desktop and mobile');
assert.match(uiCss,
	/\.l850gl-mm-page\.l850gl-mm-sms-page \.l850gl-mm-kv-row\.cbi-value:last-child\s*\{[\s\S]*?border-bottom:\s*0/,
	'the final SMS metadata row must not leave a stray divider');
assert.ok(!uiCss.includes('.l850gl-mm-page .l850gl-mm-overview-grid'));
assert.ok(!uiCss.includes('.l850gl-mm-page .l850gl-mm-overview-column'));
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-identifier\s*\{[\s\S]*?word-break:\s*break-all[\s\S]*?user-select:\s*text/,
	'full modem and SIM identifiers must wrap and remain selectable on narrow screens');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-carrier-details\s*\{[\s\S]*?flex-direction:\s*column/,
	'LTE CA details must reflow without a desktop-only table');

const smsSource = read('htdocs/luci-static/resources/view/l850gl-mm/sms.js');
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
assert.ok(smsSource.includes('composeOpen'));
assert.ok(smsSource.includes("_('Write SMS')"));
assert.ok(smsSource.includes('l850gl-mm-compose-toggle'));
assert.ok(smsSource.includes("'aria-expanded': state.composeOpen"));
assert.ok(smsSource.includes('l850gl-mm-conversation-compose'));
assert.ok(smsSource.includes('const lockedRecipient = conversationRecipient(state)'));
assert.ok(smsSource.includes('l850gl-mm-sms-status-dot is-'));
assert.ok(smsSource.includes('controller.redrawHeader()'));
assert.ok(!smsSource.includes('l850gl-mm-device-title'));
assert.match(smsSource,
	/if \(!state\.conversationNumber \|\| state\.selectionMode\)[\s\S]*?l850gl-mm-sms-bulk-delete/,
	'normal conversation mode must omit Delete all while selection keeps Delete');
assert.ok(smsSource.includes(
	"_('Messages are read, sent, and deleted through ModemManager.')"));
assert.ok(smsSource.includes('navigator.clipboard.writeText'));
assert.ok(smsSource.includes("document.execCommand('copy')"));
assert.ok(smsSource.includes('copyableMessage(body)'));
assert.ok(smsSource.includes('/\\+?[0-9]+(?:[.,:/-][0-9]+)*/g'));
for (const removedUiText of [
	"_('Compose SMS')", "_('Request-token capacity')",
	"_('Request-token maximum age (seconds)')", "_('Revision')", "_('Direction')",
	"_('Discharge timestamp')", "_('PDU type')", "_('Delivery state')",
	"_('Message reference')", "_('Storage')", "_('Binary data present')"
]) {
	assert.ok(!smsSource.includes(removedUiText),
		`SMS cards must remove nonessential UI field ${removedUiText}`);
}
assert.ok(smsSource.includes('const SMS_CACHE_MAX = 1024'));
assert.ok(smsSource.includes('state.pageCount >= MAX_SMS_PAGES'));
assert.ok(smsSource.includes('redrawPending'));
assert.ok(smsSource.includes("'focusout'"));
assert.doesNotMatch(smsSource, /\.slice\s*\(\s*0\s*,/,
	'SMS text must never be truncated before display or sending');

const lockSource = read('htdocs/luci-static/resources/view/l850gl-mm/lock.js');
assert.ok(lockSource.includes('ui.showModal'));
assert.ok(lockSource.includes("[ 'any' ]"));
assert.ok(lockSource.includes("_('Band Lock uses ModemManager')"));
assert.ok(lockSource.includes('friendlyBandList(lte)'));
assert.ok(lockSource.includes("currentAutomatic ? 'available' : 'notice'"));
assert.ok(!lockSource.includes("_('Current allowed mode')"));
assert.ok(!lockSource.includes("_('Current preferred mode')"));
assert.ok(!lockSource.includes('Selection reported by ModemManager'));
assert.ok(!lockSource.includes('Current allowed mode families'));
assert.ok(!lockSource.includes("_('Supported LTE bands')"));
assert.ok(!lockSource.includes("_('Capability')"));
assert.ok(!lockSource.includes("_('Scan capability')"));
assert.ok(!lockSource.includes('Band Lock uses ModemManager SetCurrentBands.'));
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-band-checkboxes\s*\{[\s\S]*?grid-template-columns:\s*repeat\(auto-fill/,
	'Band checkboxes must fill each row horizontally before wrapping');
assert.ok(lockSource.includes('l850gl-mm-band-groups'));
assert.ok(lockSource.includes('invertBandSelection'));
assert.ok(lockSource.includes("_('Invert')"));
assert.ok(lockSource.includes('sameBandSet(current, supported)'),
	'automatic or fully unlocked Current bands must be summarized');
assert.ok(lockSource.includes("return /^eutran-[0-9]+$/.test(band)"),
	'only LTE/4G bands must be offered for explicit band lock');
assert.ok(lockSource.includes('l850gl-mm-cell-cards'));
assert.ok(lockSource.includes('l850gl-mm-cell-card'));
assert.ok(lockSource.includes('l850gl-mm-cell-card-field'));
assert.ok(!lockSource.includes('l850gl-mm-cell-table'),
	'scan results must use cards instead of a table');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-cell-cards\s*\{[\s\S]*?grid-template-columns:\s*minmax\(0, 1fr\)/,
	'each scan result must occupy one full-width row on desktop and mobile');
assert.ok(!uiCss.includes('repeat(auto-fit, minmax(18rem, 1fr))'),
	'scan results must not return to two side-by-side desktop cards');
assert.ok(lockSource.includes("_('Tap line to use')"));
assert.ok(lockSource.includes('scanStatusIndicator'));
assert.ok(lockSource.includes('l850gl-mm-scan-status-dot is-'));
assert.ok(lockSource.includes('l850gl-mm-scan-control'));
assert.ok(lockSource.includes('l850gl-mm-cell-lock-layout'));
assert.ok(lockSource.includes('l850gl-mm-cell-status-box'));
assert.ok(!lockSource.includes("_('Use')"),
	'scan selection must use the whole row instead of a separate button');
assert.ok(lockSource.includes("return E('button'"),
	'scan rows must use native keyboard-accessible buttons');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-cell-input-grid\s*\{[\s\S]*?grid-template-columns:\s*repeat\(2, minmax\(0, 1fr\)\)/,
	'EARFCN and PCI inputs must remain side by side on desktop');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-cell-lock-layout\s*\{[\s\S]*?grid-template-columns:\s*repeat\(2, minmax\(0, 1fr\)\)[\s\S]*?align-items:\s*stretch[\s\S]*?max-width:\s*none/,
	'desktop inputs and Lock status must occupy symmetric equal-height columns');
assert.match(uiCss,
	/@media screen and \(max-width: 600px\)[\s\S]*?\.l850gl-mm-page \.l850gl-mm-cell-lock-layout\s*\{[\s\S]*?grid-template-columns:\s*minmax\(0, 1fr\)/,
	'mobile Lock status must move to a full-width row below the inputs');
assert.match(uiCss,
	/@media screen and \(max-width: 600px\)[\s\S]*?\.l850gl-mm-page \.l850gl-mm-cell-input-grid\s*\{[\s\S]*?grid-template-columns:\s*repeat\(2, minmax\(0, 1fr\)\)/,
	'mobile EARFCN and PCI inputs must return to one side-by-side row');
assert.match(uiCss,
	/@media screen and \(max-width: 600px\)[\s\S]*?\.l850gl-mm-page \.l850gl-mm-band-lock \.l850gl-mm-kv-row\.cbi-value\s*\{[\s\S]*?grid-template-columns:\s*minmax\(6\.75rem, 42%\) minmax\(0, 1fr\)/,
	'mobile Current bands must retain its compact responsive label/value row');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-lock-status\s*\{[\s\S]*?display:\s*grid[\s\S]*?grid-template-columns:\s*repeat\(2, minmax\(0, 1fr\)\)[\s\S]*?align-items:\s*center[\s\S]*?text-align:\s*center/,
	'Lock status label and state must each be centered in equal-width halves');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-lock-status\s*\{[\s\S]*?gap:\s*\.25rem/,
	'LOCK and its detail card must match the native label-to-input gap');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-lock-status > \*\s*\{[\s\S]*?justify-self:\s*center/,
	'each Lock status half must center its own content');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-lock-status-number\s*\{[\s\S]*?color:\s*var\(--primary-color, var\(--link-color/,
	'EARFCN and PCI values must use the LuCI theme blue');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-cell-lock-state\.is-locked\s*\{[\s\S]*?background:\s*var\(--error-color/,
	'LOCK must use a compact theme-aware red badge');
assert.ok(!uiCss.includes('width: calc(50% - .375rem)'),
	'the LOCK text must not expand to a detail-cell-sized block');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-cell-lock-state\.is-unlocked\s*\{[\s\S]*?background:\s*var\(--success-color/,
	'UNLOCK must use a compact theme-aware green badge');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-cell-lock-state\s*\{[\s\S]*?display:\s*inline-flex[\s\S]*?padding:\s*0 \.3rem[\s\S]*?font:\s*inherit[\s\S]*?font-weight:\s*600[\s\S]*?line-height:\s*1\.2[\s\S]*?border-radius:\s*\.15rem/,
	'LOCK and UNLOCK badges must remain content-sized and typographically aligned');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-lock-status-detail\s*\{[\s\S]*?display:\s*grid[\s\S]*?grid-template-columns:\s*repeat\(2, minmax\(0, 1fr\)\)[\s\S]*?padding:\s*\.25rem \.65rem[\s\S]*?font:\s*inherit[\s\S]*?text-align:\s*center[\s\S]*?border:\s*1px solid var\(--border-color-medium/,
	'locked EARFCN and PCI must be centered in equal-width lower cells');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-cell-status-box\s*\{[\s\S]*?justify-content:\s*flex-start/,
	'the status box must align its content from the same top edge as the inputs');
assert.doesNotMatch(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-cell-input \.cbi-input-text\s*\{[^}]*min-height:\s*2\.5rem/,
	'Lock inputs must use the compact native LuCI height');
assert.doesNotMatch(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-lock-status-detail\s*\{[^}]*min-height:\s*2\.5rem/,
	'Lock status details must use the compact native LuCI height');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-lock-status-detail > \* \+ \*\s*\{[\s\S]*?border-left:\s*1px solid var\(--border-color-low/,
	'the EARFCN and PCI detail cells must have a subtle themed divider');
for (const selector of [
	'.l850gl-mm-lock-status.is-locked > .l850gl-mm-cell-lock-state',
	'.l850gl-mm-lock-status.is-locked > .l850gl-mm-lock-status-detail',
	'.l850gl-mm-lock-status.is-unlocked > .l850gl-mm-cell-status-title',
	'.l850gl-mm-lock-status.is-unlocked > .l850gl-mm-cell-lock-state'
]) {
	assert.ok(uiCss.includes(selector),
		`${selector} must participate in the full-width status-row rule`);
}
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-lock-status\.is-unlocked > \.l850gl-mm-cell-lock-state\s*\{[\s\S]*?grid-column:\s*1 \/ -1/,
	'locked and unlocked status rows must span the complete box width');
assert.ok(lockSource.includes('redrawPending') && lockSource.includes("'focusout'"),
	'Lock polling must preserve focused mobile editors');
const pciRenderSource = lockSource.slice(
	lockSource.indexOf('function renderPciLock'),
	lockSource.indexOf('function renderDevice'));

assert.ok(lockSource.includes("_('Lock status')"));
assert.ok(lockSource.includes("_('LOCK')"));
assert.ok(lockSource.includes("_('UNLOCK')"));
assert.ok(lockSource.includes('l850gl-mm-cell-lock-state is-locked'));
assert.ok(lockSource.includes('l850gl-mm-cell-lock-state is-unlocked'));
assert.ok(!lockSource.includes('lteBandLabel(observed.band)'),
	'Lock status details must not reintroduce the LTE band');
[ "_('Capability')", "_('Scan capability')", "_('Reason')", "_('Scan reason')", "_('NVM lock state')",
	"_('Configured EARFCN')", "_('Configured PCI')", "_('Configured LTE band')" ]
	.forEach(function(verboseField) {
		assert.ok(!pciRenderSource.includes(verboseField),
			`PCI summary must omit ${verboseField}`);
	});
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
assert.ok(lockSource.includes('replacementIdentityIsValid'));
assert.ok(lockSource.includes("'applied_verified'"));
assert.ok(lockSource.includes("'cleared_verified'"));
assert.ok(lockSource.includes('do not retry until the live modem state confirms'));
assert.match(lockSource, /api\.setBands\([\s\S]*?bands, true\)/);
assert.match(lockSource, /api\.setCellLock\([\s\S]*?true\)/);
assert.match(lockSource, /api\.clearCellLock\([\s\S]*?true\)/);
assert.doesNotMatch(lockSource, /setRadio|setPrimarySimSlot|api\.reset/);
assert.doesNotMatch(lockSource, /raw\s*at|\/dev\/|tty(?:USB|ACM)|cdc-wdm|dbus|sysfs/i);

assert.ok(uiCss.includes([ 'SPDX-License-Identifier', 'Apache-2.0' ].join(': ')));
for (const selector of [
	'.l850gl-mm-page .l850gl-mm-kv-row.cbi-value',
	'.l850gl-mm-page .l850gl-mm-form-row.cbi-value',
	'.l850gl-mm-page .l850gl-mm-actions.cbi-page-actions',
	'.l850gl-mm-page .l850gl-mm-cell-card',
	'.l850gl-mm-page .l850gl-mm-sms-card'
]) {
	assert.ok(uiCss.includes(selector), `shared responsive CSS must style ${selector}`);
}
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-sms-message\s*\{[\s\S]*?border-left:\s*\.25rem solid/,
	'SMS bodies must have a distinct readable visual marker');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-sms-message\s*\{[\s\S]*?width:\s*100%/,
	'SMS bodies must use the full card width below their heading');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-sms-number\s*\{[\s\S]*?cursor:\s*copy/,
	'number-like SMS tokens must have a distinct copy affordance');
assert.match(uiCss,
	/\.l850gl-mm-copy-buffer\s*\{[\s\S]*?opacity:\s*0/,
	'HTTP LuCI sessions must retain a non-disruptive clipboard fallback');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-compose-toggle\s*\{[\s\S]*?white-space:\s*nowrap/,
	'Write SMS must remain a compact control in the filter toolbar');
const loadedCountRule = uiCss.match(
	/\.l850gl-mm-page \.l850gl-mm-sms-loaded-count\s*\{([^}]*)\}/);

assert.ok(loadedCountRule, 'the compact loaded-message badge must retain its shared styling');
assert.doesNotMatch(loadedCountRule[1], /(?:^|[;\s])color\s*:/,
	'the loaded-message badge must inherit the LuCI theme contrast color');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-sms-status-dot\.is-ready,\s*\.l850gl-mm-page \.l850gl-mm-scan-status-dot\.is-ready\s*\{[\s\S]*?background:[\s\S]*?animation:/,
	'the ready SMS cache and cell scan must use the dynamic green status dot');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-sms-toolbar\s*\{[\s\S]*?display:\s*flex/,
	'the folder filter and loaded count must share one compact toolbar');
assert.match(uiCss,
	/@media screen and \(max-width: 600px\)[\s\S]*?\.l850gl-mm-page \.l850gl-mm-sms-toolbar\s*\{[\s\S]*?grid-template-columns:\s*minmax\(0, 1fr\) auto auto/,
	'the phone SMS toolbar must keep filter, loaded count, and Write SMS on one row');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-sms-delete-icon\s*\{[\s\S]*?position:\s*absolute[\s\S]*?width:\s*2\.75rem[\s\S]*?height:\s*2\.75rem/,
	'the top-right trash action must retain an accessible 44px touch target');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-sms-card-selected\s*\{[\s\S]*?background:/,
	'selected SMS cards must have a visible theme-aware state');
assert.ok(uiCss.includes('.l850gl-mm-page .l850gl-mm-sms-trash-glyph::before') &&
	uiCss.includes('.l850gl-mm-page .l850gl-mm-sms-trash-glyph::after'),
	'the delete action must render a theme-aware trash glyph without an emoji asset');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-sms-list-actions\.cbi-page-actions\s*\{[\s\S]*?justify-content:\s*flex-start/,
	'Delete all must remain at the left edge of the message toolbar');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-sms-chat-list \.l850gl-mm-sms-card\s*\{[\s\S]*?width:\s*min\(88%, 46rem\)/,
	'conversation cards must use a compact responsive chat width');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-conversation-compose\s*\{[\s\S]*?grid-template-columns:\s*minmax\(0, 1fr\) auto[\s\S]*?position:\s*sticky/,
	'the conversation composer must remain a responsive bottom chat row');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-conversation-compose \.l850gl-mm-conversation-send\s*\{[\s\S]*?min-height:\s*2\.75rem/,
	'the conversation send action must retain a 44px touch target');
assert.match(uiCss, /@media screen and \(max-width: 600px\)/,
	'the shared stylesheet must include a phone breakpoint');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-scan-actions\.cbi-page-actions\s*\{[\s\S]*?margin-top:\s*\.75rem/,
	'the PCI scan controls must retain space below the lock-status rows');
assert.match(uiCss,
	/\.l850gl-mm-page \.l850gl-mm-scan-control\s*\{[\s\S]*?display:\s*inline-flex[\s\S]*?flex:\s*0 0 auto[\s\S]*?flex-wrap:\s*nowrap/,
	'the scan dot and button must remain together on desktop and phone layouts');
[ 'is-loading', 'is-limited', 'is-unavailable' ].forEach(function(stateClass) {
	assert.ok(uiCss.includes('.l850gl-mm-scan-status-dot.' + stateClass),
		`scan status CSS must expose ${stateClass}`);
});
assert.match(uiCss,
	/@media \(prefers-reduced-motion: reduce\)[\s\S]*?\.l850gl-mm-scan-status-dot\.is-ready[\s\S]*?animation:\s*none/,
	'the dynamic scan dot must respect reduced-motion preferences');
assert.match(uiCss,
	/@media screen and \(max-width: 600px\)[\s\S]*?\.l850gl-mm-page \.l850gl-mm-mode-choices\s*\{[\s\S]*?grid-template-columns:\s*repeat\(3, minmax\(0, 1fr\)\)/,
	'phone mode choices must keep all three options on one row');
assert.doesNotMatch(uiCss, /display\s*:\s*none|visibility\s*:\s*hidden|text-overflow\s*:\s*ellipsis/i,
	'responsive CSS must reflow instead of hiding or truncating information');
assert.doesNotMatch(uiCss, /desktop-only|mobile-only/i,
	'the package must not maintain separate desktop/mobile information trees');

for (const source of [ overviewSource, lockSource, smsSource ]) {
	assert.doesNotMatch(source, /['"]style['"]\s*:/,
		'view markup must not override LuCI themes with inline styles');
	assert.doesNotMatch(source,
		/\b(?:matchMedia|innerWidth|outerWidth|screen\.width|userAgent|maxTouchPoints)\b/,
		'views must render one markup tree independent of viewport or device sniffing');
	assert.doesNotMatch(source, /#[0-9a-f]{3,8}\b|rgba?\s*\(/i,
		'view JavaScript must not hard-code theme colours');
}
assert.doesNotMatch(lockSource, /â|Ã|Â/,
	'Lock UI source must not contain mojibake');

for (const file of frontendSources) {
	const source = fs.readFileSync(file, 'utf8');
	assert.ok(!source.includes("_(' (active)')"));
	assert.ok(!source.includes("_(' (not present)')"));
}
assert.ok(read('htdocs/luci-static/resources/l850gl-mm/widgets.js').includes(
	"' ' + _('(active)')"));

const makefile = read('Makefile');
assert.ok(makefile.includes('PKG_VERSION:=0.6.0'));
assert.ok(makefile.includes('PKG_RELEASE:=5'));
assert.ok(makefile.includes(
	'LUCI_TITLE:=LuCI companion for the L850-GL modem managed by ModemManager'));
assert.ok(makefile.includes(
	'LUCI_DESCRIPTION:=Minimal L850GL MM Overview, Lock, and SMS UI backed by ModemManager'));
assert.ok(makefile.includes(
	'LUCI_URL:=https://github.com/As-tsaqib/luci-app-L850GL-MM'));
for (const dependency of [
	'+luci-base', '+l850gl-mm-bridge', '+modemmanager', '+luci-proto-modemmanager',
	'+kmod-usb-acm', '+kmod-usb-net-cdc-mbim', '+kmod-usb-wdm'
]) {
	assert.ok(makefile.includes(dependency), `Makefile must include ${dependency}`);
}
assert.doesNotMatch(makefile, /@MODEMMANAGER_WITH_(?:MBIM|NETIFD)/);
assert.ok(makefile.includes('PKG_LICENSE:=Apache-2.0'));
assert.ok(makefile.includes('include $(TOPDIR)/feeds/luci/luci.mk'));

const pot = read('po/templates/l850gl-mm.pot');
assert.ok(pot.includes('Project-Id-Version: luci-app-L850GL-MM 0.6.0'));
for (const retired of [
	'Status', 'Advanced', 'Settings', 'Radio power', 'Reset modem', 'Primary SIM slot',
	'Rescan devices', 'Shadow mode', 'eSIM'
]) {
	assert.ok(!pot.includes(`msgid "${retired}"`),
		`translation template must not contain retired msgid: ${retired}`);
}
for (const text of [
	'Overview', 'Lock', 'SMS', 'Band Lock', 'PCI/EARFCN Lock', 'Load more',
	'Write SMS', 'Delete SMS', 'Ready', '%d loaded', '(active)', 'Tap line to use', 'Invert', 'Lock status',
	'LOCK', 'UNLOCK', 'Any Supported bands',
	'EARFCN must be an integer from 0 through 70545.',
	'any(automatic)',
	'Explicit LTE bands', 'Modem Info', 'Modem Status',
	'Band and Cell Status', 'Band Lock uses ModemManager',
	'Active LTE Bands', 'Active LTE Carriers', 'LTE CA Details',
	'Total Bandwidth', 'DL %s MHz · UL %s MHz',
	'Firmware', 'USB Mode', 'Modem voltage', 'SIMs', 'SIM Number', 'IMEI', 'ICCID',
	'Cell scan available', 'Cell scan unavailable',
	'Cell scan temporarily unavailable',
	'Modem info by ModemManager',
	'Messages are read, sent, and deleted through ModemManager.',
	'Copy number', 'Number copied.',
	'Unable to copy the number. Select it manually instead.',
	'Back', 'Conversation', 'Delete all', 'Delete selected SMS',
	'Hold a message to select it.', 'Selected'
]) {
	assert.ok(pot.includes(`msgid "${text}"`),
		`translation template must contain: ${text}`);
}
for (const removedText of [
	'Selection reported by ModemManager', 'Current allowed mode families',
	'Supported LTE bands', 'Current allowed mode', 'Current preferred mode',
	'Compose SMS', 'Request-token capacity', 'Request-token maximum age (seconds)',
	'Messaging cache', 'Loaded messages', 'Capability', 'Scan capability',
	'Capabilities', 'Primary carrier #%d', 'Secondary carrier #%d',
	'EARFCN must be an unsigned LTE channel number.',
	'Primary LTE Band', 'Secondary LTE Bands', 'None',
	'LTE Carrier Aggregation', 'B%d, EARFCN %d, PCI %d, DL/UL %s/%s MHz',
	'Revision', 'IMSI', 'Bearer connected', 'Modem state',
	'Signal Status', 'Serving cell status', 'Locked', 'Unlocked',
	'Direction', 'Discharge timestamp', 'PDU type', 'Delivery state',
	'Message reference', 'Storage', 'Binary data present',
	'A concise ModemManager snapshot. Network configuration and connection intent remain owned by netifd.',
	'Messages are read, sent, and deleted through ModemManager. Recipient numbers and message text remain only in this authorized view and are never written to application logs.',
	'Band Lock uses ModemManager SetCurrentBands. PCI/EARFCN Lock is an explicit expert build path; only an exact live-validated hardware and firmware tuple can use its fixed command state machine.'
]) {
	assert.ok(!pot.includes(`msgid "${removedText}"`),
		`translation template must remove obsolete UI text: ${removedText}`);
}
assert.strictEqual(listFiles(appRoot).filter(function(file) {
	return file.endsWith('.lua');
}).length, 0, 'legacy Lua controllers and models are forbidden');

const retiredVendorBrand = [ 'fibo', 'com' ].join('');
listFiles(appRoot).forEach(function(file) {
	const source = fs.readFileSync(file, 'utf8').toLowerCase();

	assert.ok(!source.includes(retiredVendorBrand),
		`${path.relative(appRoot, file)} must not retain the retired vendor branding`);
});

testOverviewLoadMerge()
	.then(testOverviewCarrierLastKnownGood)
	.then(testSmsInteractions)
	.then(testSmsBulkDeletion)
	.then(testSmsDeleteAllPreparation)
	.then(testFocusedFolderPolling)
	.then(function() {
	console.log('luci-app-l850gl-mm static checks: OK');
}).catch(function(error) {
	console.error(error && error.stack ? error.stack : error);
	process.exitCode = 1;
});
