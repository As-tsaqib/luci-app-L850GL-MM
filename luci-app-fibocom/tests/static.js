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
	'set_bands', 'set_modes'
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

assert.strictEqual(api.SCHEMA_VERSION, 3);
assert.deepStrictEqual(declarations.map(function(call) {
	return `${call.object}.${call.method}`;
}), [
	'fibocom.mm.list_modems',
	'fibocom.mm.get_overview',
	'fibocom.mm.get_lock_status',
	'fibocom.mm.set_bands',
	'fibocom.mm.set_modes',
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
assert.deepStrictEqual(declarations[10].params,
	[ 'modem_id', 'generation', 'earfcn', 'pci', 'confirm' ]);
assert.deepStrictEqual(declarations[11].params,
	[ 'modem_id', 'generation', 'confirm' ]);
assert.deepStrictEqual(api.listModems().arguments, []);
assert.deepStrictEqual(api.getOverview('fibocom-test').arguments, [ 'fibocom-test' ]);
assert.deepStrictEqual(api.getLockStatus('fibocom-test').arguments, [ 'fibocom-test' ]);
assert.deepStrictEqual(api.setBands('fibocom-test', 4, [ 'eutran-1' ], true).arguments,
	[ 'fibocom-test', 4, [ 'eutran-1' ], true ]);
assert.deepStrictEqual(api.setModes('fibocom-test', 4, '3g|4g', '4g', true).arguments,
	[ 'fibocom-test', 4, '3g|4g', '4g', true ]);
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

const widgets = evaluate('htdocs/luci-static/resources/fibocom/widgets.js', { baseclass });
assert.strictEqual(widgets.isCompatible({ schema: 3, ok: true }), true);
assert.strictEqual(widgets.isCompatible({ schema: 1, ok: true }), false);
assert.strictEqual(widgets.isCompatible({ schema: 3, ok: 'yes' }), false);
assert.deepStrictEqual(widgets.modems({ schema: 3, ok: true, modems: [] }), []);
assert.deepStrictEqual(widgets.modems({ schema: 1, ok: true, modems: [] }), []);
assert.match(widgets.responseError({ schema: 1, ok: true }), /schema/i);
assert.match(widgets.responseError({ schema: 3, ok: true }), /malformed/i,
	'a partial success response must fail closed');
assert.strictEqual(widgets.mutationAllowed({ schema: 3, generated_at: 1, ok: true }, {
	modem_id: 'fibocom-test', generation: 4
}, 'fibocom-test', 4), true);
assert.strictEqual(widgets.mutationAllowed({ schema: 2, generated_at: 1, ok: true }, {
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
	schema: 3,
	generated_at: 1,
	ok: true,
	modems: [ summary ]
};
const overviewResult = {
	schema: 3,
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
const lockResult = {
	schema: 3,
	generated_at: 1,
	ok: true,
	modem_id: 'fibocom-test',
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
	schema: 3,
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
	schema: 3,
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
const availableExpertResult = {
	schema: 3,
	generated_at: 1,
	ok: true,
	modem_id: 'fibocom-test',
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
	serving_cell: {
		state: 'available', reason: 'standard-cell-info', earfcn: 1325,
		pci: 504, band: 3
	}
}), summary), /malformed/i,
'an out-of-range serving PCI must fail frontend validation');
assert.strictEqual(widgets.overviewError(Object.assign({}, overviewResult, {
	serving_cell: { state: 'unavailable', reason: 'refresh-pending' }
}), summary), null, 'a typed unavailable Serving Cell state must remain renderable');
assert.strictEqual(widgets.lockError(lockResult, summary), null);
assert.match(widgets.lockError(Object.assign({}, lockResult, {
	mode_policy: Object.assign({}, lockResult.mode_policy, {
		allowed: '4g', preferred: '4g'
	})
}), summary), /malformed/i,
'an inconsistent persistent mode policy must fail closed');

const overviewView = evaluate(
	'htdocs/luci-static/resources/view/fibocom/overview.js', viewDependencies);
const lockView = evaluate(
	'htdocs/luci-static/resources/view/fibocom/lock.js', viewDependencies);
const smsView = evaluate(
	'htdocs/luci-static/resources/view/fibocom/sms.js', viewDependencies);

const overviewNode = overviewView.render({
	list: listResult,
	entries: [ { summary: summary, overview: overviewResult } ]
});
assert.strictEqual(overviewNode.tag, 'div');
const renderedOverview = renderedText(overviewNode);
assert.strictEqual(findNodes(overviewNode, function(node) {
	return node.attributes && typeof node.attributes.class === 'string' &&
		node.attributes.class.split(/\s+/).includes('fibocom-overview-card');
}).length, 4, 'Overview must render exactly four responsive information groups');
const overviewColumns = findNodes(overviewNode, function(node) {
	return node.attributes && typeof node.attributes.class === 'string' &&
		node.attributes.class.split(/\s+/).includes('fibocom-overview-column');
});

assert.strictEqual(overviewColumns.length, 2,
	'Overview must use two wide desktop columns instead of four narrow columns');
const leftOverviewColumn = renderedText(overviewColumns[0]);
const rightOverviewColumn = renderedText(overviewColumns[1]);
const modemInfoIndex = leftOverviewColumn.indexOf('Modem Info');
const modemStatusIndex = leftOverviewColumn.indexOf('Modem Status');
const bandCellIndex = rightOverviewColumn.indexOf('Band and Cell Status');
const signalStatusIndex = rightOverviewColumn.indexOf('Signal Status');
const capabilitiesIndex = rightOverviewColumn.indexOf('Capabilities');

assert.ok(modemInfoIndex !== -1 && modemStatusIndex !== -1 &&
	modemInfoIndex < modemStatusIndex,
	'Modem Status must be stacked below Modem Info');
assert.ok(bandCellIndex !== -1 && signalStatusIndex !== -1 &&
	bandCellIndex < signalStatusIndex,
	'Signal Status must be stacked below Band and Cell Status');
assert.strictEqual(leftOverviewColumn.includes('Capabilities'), false,
	'Capabilities must not remain below Modem Status');
assert.ok(capabilitiesIndex !== -1 && signalStatusIndex < capabilitiesIndex,
	'Capabilities must be rendered below Signal Status');
[ 'Modem Info', 'Modem Status', 'Band and Cell Status', 'Signal Status',
	'3G: B1 | 4G: B1, B3' ]
	.forEach(function(expected) {
		assert.ok(renderedOverview.some(function(value) { return value.includes(expected); }),
			`Overview must render the ${expected} group or friendly band label`);
	});
[ 'utran-', 'eutran-' ].forEach(function(internalPrefix) {
	assert.ok(!renderedOverview.some(function(value) { return value.includes(internalPrefix); }),
		`Overview must not expose the internal ${internalPrefix} band prefix`);
});
const lockNode = lockView.render({
	list: listResult,
	entries: [ { summary: summary, lock: lockResult, expert: expertResult } ]
});
assert.strictEqual(lockNode.tag, 'div');
const renderedLock = renderedText(lockNode);
[ '3G: B1', '4G: B1, B3, B8' ].forEach(function(expected) {
	assert.ok(renderedLock.some(function(value) { return value.includes(expected); }),
		`Lock must render the friendly band summary ${expected}`);
});
[ 'utran-', 'eutran-' ].forEach(function(internalPrefix) {
	assert.ok(!renderedLock.some(function(value) { return value.includes(internalPrefix); }),
		`Lock must not expose the internal ${internalPrefix} band prefix`);
});
const renderedBandGroups = findNodes(lockNode, function(node) {
	return node.attributes && typeof node.attributes.class === 'string' &&
		node.attributes.class.split(/\s+/).includes('fibocom-band-group');
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
const automaticLockNode = lockView.render({
	list: listResult,
	entries: [ { summary: summary, lock: Object.assign({}, lockResult, {
		band_selection: 'automatic',
		current_bands: lockResult.supported_bands
	}), expert: expertResult } ]
});
const automaticCurrentBandRows = findNodes(automaticLockNode, function(node) {
	const values = renderedText(node);

	return node.tag === 'tr' && values[0] === 'Current bands';
});

assert.strictEqual(automaticCurrentBandRows.length, 1);
assert.deepStrictEqual(renderedText(automaticCurrentBandRows[0]),
	[ 'Current bands', 'Any Supported bands' ],
	'automatic band selection must not list every supported band');
const explicitAllLockNode = lockView.render({
	list: listResult,
	entries: [ { summary: summary, lock: Object.assign({}, lockResult, {
		band_selection: 'explicit',
		current_bands: lockResult.supported_bands
	}), expert: expertResult } ]
});
const explicitAllCurrentBandRows = findNodes(explicitAllLockNode, function(node) {
	const values = renderedText(node);

	return node.tag === 'tr' && values[0] === 'Current bands';
});

assert.strictEqual(explicitAllCurrentBandRows.length, 1);
assert.deepStrictEqual(renderedText(explicitAllCurrentBandRows[0]),
	[ 'Current bands', 'Any Supported bands' ],
	'an explicit set containing every supported band must be presented as unlocked');
const renderedAvailableLock = lockView.render({
	list: listResult,
	entries: [ { summary: summary, lock: Object.assign({}, lockResult, {
		pci_lock: { state: 'available', mutable: true,
			reason: 'live-validated-l850-command-state-machine' }
	}), expert: availableExpertResult } ]
});
assert.strictEqual(renderedAvailableLock.tag, 'div');
const lockStatusRows = findNodes(renderedAvailableLock, function(node) {
	const values = renderedText(node);

	return node.tag === 'tr' && values[0] === 'Lock status';
});

assert.strictEqual(lockStatusRows.length, 1);
[ 'Locked', 'B3 · EARFCN 1325 · PCI 0' ].forEach(function(value) {
	assert.ok(renderedText(lockStatusRows[0]).includes(value),
		`PCI lock summary must include ${value}`);
});
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
const clearLockStatusRows = findNodes(renderedClearLock, function(node) {
	const values = renderedText(node);

	return node.tag === 'tr' && values[0] === 'Lock status';
});

assert.strictEqual(clearLockStatusRows.length, 1);
assert.ok(renderedText(clearLockStatusRows[0]).includes('Unlocked'));

const scanCells = [
	{ type: 4, serving: true, earfcn: 1325, pci: 0, band: 3, rsrp: -90, rsrq: -10 },
	{ type: 5, serving: false, earfcn: 1650, pci: 42, band: 3, rsrp: -97, rsrq: -13 }
];
const interactiveDom = {
	content: function(node, replacement) { node.children = [ replacement ]; }
};
const interactiveApi = {
	cellScan: function(modemId, generation) {
		assert.strictEqual(modemId, summary.modem_id);
		assert.strictEqual(generation, summary.generation);
		const result = {
			schema: 3,
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
	'htdocs/luci-static/resources/view/fibocom/lock.js', {
		dom: interactiveDom, poll: inert, ui: inert, view: view,
		api: interactiveApi, widgets: widgets
	});
const interactiveLock = interactiveLockView.render({
	list: listResult,
	entries: [ { summary: summary, lock: Object.assign({}, lockResult, {
		pci_lock: { state: 'available', mutable: true,
			reason: 'live-validated-l850-command-state-machine' }
	}), expert: availableExpertResult } ]
});
const mode4gOnly = findNodes(interactiveLock, function(node) {
	return node.tag === 'input' && node.attributes.id ===
		'fibocom-allowed-mode-0-2';
})[0];

assert.ok(mode4gOnly, 'persistent mode selection must render a 4G-only radio');
mode4gOnly.attributes.change({ target: { checked: true, value: '4g' } });
const preferredNone = findNodes(interactiveLock, function(node) {
	return node.tag === 'input' && node.attributes.id ===
		'fibocom-preferred-mode-0-0';
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
	return node.tag === 'input' && /^fibocom-band-0-[0-9]+$/.test(node.attributes.id);
});
const invertButton = findNodes(interactiveLock, function(node) {
	return node.tag === 'button' && renderedText(node).includes('Invert');
})[0];

assert.ok(invertButton, 'Band Lock must render an Invert button');
assert.strictEqual(initialBandCheckboxes.filter(function(node) {
	return node.attributes.checked != null;
}).length, 2);
invertButton.attributes.click();
const invertedBandCheckboxes = findNodes(interactiveLock, function(node) {
	return node.tag === 'input' && /^fibocom-band-0-[0-9]+$/.test(node.attributes.id);
});
const invertedCheckedBands = invertedBandCheckboxes.filter(function(node) {
	return node.attributes.checked != null;
});

assert.strictEqual(invertedCheckedBands.length, 1,
	'Invert must flip every supported explicit LTE band locally');
assert.strictEqual(invertedCheckedBands[0].attributes.id, 'fibocom-band-0-2');
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
		node.attributes.class.split(/\s+/).includes('fibocom-cell-card');
});
const scanResultList = findNodes(interactiveLock, function(node) {
	return node.attributes && typeof node.attributes.class === 'string' &&
		node.attributes.class.split(/\s+/).includes('fibocom-cell-cards');
})[0];
const compactScanText = renderedText(scanResultList);
const scanCardFields = findNodes(scanResultList, function(node) {
	return node.attributes && typeof node.attributes.class === 'string' &&
		node.attributes.class.split(/\s+/).includes('fibocom-cell-card-field');
});
const scanHint = findNodes(interactiveLock, function(node) {
	return node.attributes && typeof node.attributes.class === 'string' &&
		node.attributes.class.split(/\s+/).includes('fibocom-scan-hint');
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
	return node.attributes.style.includes('border-radius:.55em') &&
		node.attributes.style.includes('box-shadow:');
}), 'each scan result must have a distinct card boundary');
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
assert.ok(scanHint.attributes.style.includes('color:#1e90ff'),
	'the scan-row instruction must be blue');
assert.strictEqual(scanCards[0].attributes.role, 'button');
assert.strictEqual(scanCards[0].attributes.tabindex, 0);
scanCards[0].attributes.click();
const selectedEarfcn = findNodes(interactiveLock, function(node) {
	return node.tag === 'input' && node.attributes.id === 'fibocom-earfcn-0';
})[0];
const selectedPci = findNodes(interactiveLock, function(node) {
	return node.tag === 'input' && node.attributes.id === 'fibocom-pci-0';
})[0];
const cellInputGrid = findNodes(interactiveLock, function(node) {
	return node.attributes && typeof node.attributes.class === 'string' &&
		node.attributes.class.split(/\s+/).includes('fibocom-cell-input-grid');
})[0];
const cellActions = findNodes(interactiveLock, function(node) {
	return node.attributes && typeof node.attributes.class === 'string' &&
		node.attributes.class.split(/\s+/).includes('fibocom-cell-actions');
})[0];
const selectedScanCards = findNodes(interactiveLock, function(node) {
	return node.attributes && typeof node.attributes.class === 'string' &&
		node.attributes.class.split(/\s+/).includes('fibocom-cell-card') &&
		node.attributes['aria-pressed'] === 'true';
});

assert.strictEqual(selectedEarfcn.attributes.value, '1325');
assert.strictEqual(selectedPci.attributes.value, '0',
	'the row action must preserve PCI zero');
assert.ok(cellInputGrid.attributes.style.includes('margin:.9em 0 1em') &&
	cellInputGrid.attributes.style.includes('clear:both'),
	'EARFCN and PCI inputs must be visually separated from the mutation actions');
assert.ok(cellActions.attributes.style.includes('clear:both') &&
	cellActions.attributes.style.includes('margin-top:.75em'),
	'Apply and Clear actions must occupy their own row below the inputs');
assert.strictEqual(selectedScanCards.length, 1,
	'the selected scan card must receive a visible selected state');
assert.ok(renderedText(interactiveLock).some(function(value) {
	return value.includes('Selected cell (EARFCN 1325, PCI 0)');
}), 'the row action must show which cell was copied');
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
assert.ok(overviewSource.includes('fibocom-overview-grid'));
assert.ok(overviewSource.includes('grid-template-columns:repeat(auto-fit,minmax(16rem,1fr))'),
	'Overview groups must collapse responsively without dropping fields');

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
assert.ok(lockSource.includes('grid-template-columns:repeat(auto-fill'),
	'Band checkboxes must fill each row horizontally before wrapping');
assert.ok(lockSource.includes('fibocom-band-groups'));
assert.ok(lockSource.includes('invertBandSelection'));
assert.ok(lockSource.includes("_('Invert')"));
assert.ok(lockSource.includes('sameBandSet(current, supported)'),
	'automatic or fully unlocked Current bands must be summarized');
assert.ok(lockSource.includes("return /^eutran-[0-9]+$/.test(band)"),
	'only LTE/4G bands must be offered for explicit band lock');
assert.ok(lockSource.includes('fibocom-cell-cards'));
assert.ok(lockSource.includes('fibocom-cell-card'));
assert.ok(lockSource.includes('fibocom-cell-card-field'));
assert.ok(!lockSource.includes('fibocom-cell-table'),
	'scan results must use cards instead of a table');
assert.ok(lockSource.includes('grid-template-columns:repeat(auto-fit,minmax(18rem,1fr))'),
	'scan cards must form a responsive desktop and mobile grid');
assert.ok(lockSource.includes("_('Tap line to use')"));
assert.ok(!lockSource.includes("_('Use')"),
	'scan selection must use the whole row instead of a separate button');
assert.ok(lockSource.includes("'keydown': function(event)"),
	'scan rows must support keyboard activation');
assert.ok(lockSource.includes('grid-template-columns:repeat(2,minmax(0,1fr))'),
	'EARFCN and PCI inputs must remain side by side on mobile');
const pciRenderSource = lockSource.slice(
	lockSource.indexOf('function renderPciLock'),
	lockSource.indexOf('function renderDevice'));

assert.ok(pciRenderSource.includes("_('Lock status')"));
[ "_('Reason')", "_('Scan reason')", "_('NVM lock state')",
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

for (const file of frontendSources) {
	const source = fs.readFileSync(file, 'utf8');
	assert.ok(!source.includes("_(' (active)')"));
	assert.ok(!source.includes("_(' (not present)')"));
}
assert.ok(read('htdocs/luci-static/resources/fibocom/widgets.js').includes(
	"' ' + _('(active)')"));

const makefile = read('Makefile');
for (const dependency of [
	'+luci-base', '+fibocom-mm-bridge', '+modemmanager', '+luci-proto-modemmanager',
	'+kmod-usb-acm', '+kmod-usb-net-cdc-mbim', '+kmod-usb-wdm'
]) {
	assert.ok(makefile.includes(dependency), `Makefile must include ${dependency}`);
}
assert.doesNotMatch(makefile, /@MODEMMANAGER_WITH_(?:MBIM|NETIFD)/);
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
	'Compose SMS', 'Delete SMS', '(active)', 'Tap line to use', 'Invert', 'Lock status',
	'Locked', 'Unlocked', 'Any Supported bands', 'Supported LTE bands',
	'Explicit LTE bands', 'Modem Info', 'Modem Status',
	'Band and Cell Status', 'Signal Status'
]) {
	assert.ok(pot.includes(`msgid "${text}"`),
		`translation template must contain: ${text}`);
}
assert.strictEqual(listFiles(appRoot).filter(function(file) {
	return file.endsWith('.lua');
}).length, 0, 'legacy Lua controllers and models are forbidden');

console.log('luci-app-fibocom static checks: OK');
