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
assert.deepStrictEqual(listFiles(resources).filter(function(file) {
	return file.endsWith('.css');
}).map(function(file) {
	return path.relative(resources, file).split(path.sep).join('/');
}), [ 'fibocom/fibocom.css' ],
'the package must install one shared stylesheet instead of desktop/mobile variants');
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

		return hasClass(candidate, 'fibocom-kv-row') && values[0] === label;
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
	assert.ok(hasClass(node, 'fibocom-page'), `${pageClass} must load the shared layout scope`);
	assert.ok(hasClass(node, pageClass), `${pageClass} must expose its scoped page class`);
	const stylesheets = findNodes(node, function(candidate) {
		return candidate.tag === 'link' && candidate.attributes.rel === 'stylesheet' &&
			candidate.attributes.href === '/luci-static/resources/fibocom/fibocom.css';
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

const widgets = evaluate('htdocs/luci-static/resources/fibocom/widgets.js', { baseclass });
const sampleKeyValues = widgets.keyValueList([ [ 'Alpha', 'Beta' ] ]);
const sampleKeyValueRows = findNodes(sampleKeyValues, function(node) {
	return hasClass(node, 'fibocom-kv-row');
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
assertPageStructure(overviewNode, 'fibocom-overview-page');
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
assertPageStructure(lockNode, 'fibocom-lock-page');
const renderedLock = renderedText(lockNode);
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
assert.strictEqual(findKeyValueRows(lockNode, 'Allowed mode').length, 0,
	'the selected allowed mode must not be duplicated above its controls');
assert.strictEqual(findKeyValueRows(lockNode, 'Preferred mode').length, 0,
	'the selected preferred mode must not be duplicated above its controls');
assert.strictEqual(findKeyValueRows(lockNode, 'Reason').length, 0,
	'Band Lock must omit the verbose capability reason row');
assert.strictEqual(findKeyValueRows(lockNode, 'Selection reported by ModemManager').length, 0,
	'Band Lock must omit the internal selection-report row');
const modePolicyPanel = findNodes(lockNode, function(node) {
	return hasClass(node, 'fibocom-mode-policy');
})[0];
const bandLockPanel = findNodes(lockNode, function(node) {
	return hasClass(node, 'fibocom-band-lock');
})[0];

assert.ok(modePolicyPanel && bandLockPanel);
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
const lockStatusRows = findKeyValueRows(renderedAvailableLock, 'Lock status');

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
const clearLockStatusRows = findKeyValueRows(renderedClearLock, 'Lock status');

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
assert.ok(hasClass(mode4gOnly, 'cbi-input-radio'),
	'mode choices must use the native LuCI radio class');
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
assert.ok(initialBandCheckboxes.every(function(node) {
	return hasClass(node, 'cbi-input-checkbox');
}), 'band choices must use the native LuCI checkbox class');
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
	return node.tag === 'button' && node.attributes.type === 'button' &&
		hasClass(node, 'cbi-section-node') && node.attributes.style == null;
}), 'each scan result must be a theme-neutral semantic LuCI card button');
const lockActionButtons = findNodes(interactiveLock, function(node) {
	return node.tag === 'button' && !hasClass(node, 'fibocom-cell-card');
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
assert.strictEqual(cellInputGrid.attributes.style, undefined,
	'EARFCN and PCI layout must be owned by the shared responsive stylesheet');
assert.ok(hasClass(cellActions, 'cbi-page-actions') && hasClass(cellActions, 'fibocom-actions'),
	'Apply and Clear actions must use the shared LuCI action layout');
assert.strictEqual(selectedScanCards.length, 1,
	'the selected scan card must receive a visible selected state');
assert.ok(renderedText(interactiveLock).some(function(value) {
	return value.includes('Selected cell (EARFCN 1325, PCI 0)');
}), 'the row action must show which cell was copied');
assertLabelTargets(interactiveLock, 'Lock');
const renderedSms = smsView.render({
	list: listResult,
	entries: [ { summary: summary, messages: smsResult } ]
});
assert.strictEqual(renderedSms.tag, 'div');
assertPageStructure(renderedSms, 'fibocom-sms-page');
assert.ok(renderedText(renderedSms).includes('Load more'));
assert.strictEqual(findKeyValueRows(renderedSms, 'Revision').length, 0,
	'the SMS summary must omit the technical cache revision');
assert.strictEqual(findKeyValueRows(renderedSms, 'Request-token capacity').length, 0);
assert.strictEqual(findKeyValueRows(renderedSms,
	'Request-token maximum age (seconds)').length, 0);
const collapsedComposer = findNodes(renderedSms, function(node) {
	return node.tag === 'details' && hasClass(node, 'fibocom-compose');
})[0];

assert.ok(collapsedComposer, 'Write SMS must use a native collapsible details panel');
assert.strictEqual(collapsedComposer.attributes.open, null,
	'Write SMS must be collapsed by default');
assert.ok(findNodes(collapsedComposer, function(node) {
	return node.tag === 'summary' && renderedText(node).includes('Write SMS');
}).length === 1, 'the collapsed composer banner must be named Write SMS');
collapsedComposer.attributes.toggle({ target: { open: true } });
const openComposerNode = smsView.render({
	list: listResult,
	entries: [ { summary: summary, messages: smsResult } ]
});
const openComposer = findNodes(openComposerNode, function(node) {
	return node.tag === 'details' && hasClass(node, 'fibocom-compose');
})[0];

assert.strictEqual(openComposer.attributes.open, '',
	'Write SMS open state must survive a polling redraw');
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
	return hasClass(node, 'fibocom-sms-card');
})[0];
const receivedRows = findNodes(receivedCard, function(node) {
	return hasClass(node, 'fibocom-kv-row');
});

assert.deepStrictEqual(receivedRows.map(function(node) {
	return renderedText(node)[0];
}), [ 'State', 'From', 'Timestamp' ],
'received cards must show only the essential SMS fields');
assert.strictEqual(findNodes(receivedCard, function(node) {
	return hasClass(node, 'label') && hasClass(node, 'success');
}).length, 1, 'received SMS state must be green');
assert.strictEqual(findNodes(receivedCard, function(node) {
	return hasClass(node, 'fibocom-sms-message');
}).length, 1, 'the SMS body must use its dedicated readable marker');
assert.strictEqual(findNodes(receivedCard, function(node) {
	return hasClass(node, 'fibocom-sms-body-title') &&
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
	return hasClass(node, 'fibocom-sms-card');
})[0];
const sentRows = findNodes(sentCard, function(node) {
	return hasClass(node, 'fibocom-kv-row');
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
	return node.tag === 'button' && hasClass(node, 'fibocom-sms-number');
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
	return hasClass(node, 'fibocom-sms-message');
})[0];
const inboundNumericButton = findNodes(inboundNumericBody, function(node) {
	return node.tag === 'button' && hasClass(node, 'fibocom-sms-number');
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
	return hasClass(node, 'fibocom-sms-card');
})[0];
const outboundNumericButton = findNodes(outboundNumericCard, function(node) {
	return node.tag === 'button' && hasClass(node, 'fibocom-sms-number');
})[0];

assert.ok(renderedText(findNodes(outboundNumericCard, function(node) {
	return hasClass(node, 'fibocom-sms-message');
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
	const module = evaluate('htdocs/luci-static/resources/view/fibocom/sms.js', {
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
		return hasClass(candidate, 'fibocom-sms-card');
	});
}

function smsCardWithText(node, text) {
	return smsCards(node).find(function(card) {
		return renderedText(card).includes(text);
	});
}

function smsBulkButton(node) {
	return findNodes(node, function(candidate) {
		return candidate.tag === 'button' && hasClass(candidate, 'fibocom-sms-bulk-delete');
	})[0];
}

function successfulDeleteResult(smsId) {
	return {
		schema: 3,
		generated_at: 2,
		ok: true,
		modem_id: summary.modem_id,
		generation: summary.generation,
		messaging_generation: 7,
		sms_id: smsId,
		deleted: true
	};
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

		global.window = timers.window;
		let harness = createSmsInteractionHarness(conversationMessages);
		let card = smsCardWithText(harness.root, 'Exact inbound');
		let prevented = 0;
		const opened = card.attributes.click({
			target: smsPlainTarget(),
			preventDefault: function() { prevented++; }
		});

		assert.ok(opened instanceof Promise, 'a quick card tap must start in-tab navigation');
		await opened;
		const conversationHeader = findNodes(harness.root, function(node) {
			return hasClass(node, 'fibocom-conversation-header');
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
		assert.ok(chatCards.some(function(node) { return hasClass(node, 'fibocom-sms-chat-inbound'); }));
		assert.ok(chatCards.some(function(node) { return hasClass(node, 'fibocom-sms-chat-outbound'); }));

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

		assert.ok(hasClass(selectedCard, 'fibocom-sms-card-selected'));
		assert.ok(hasClass(selectedCard, 'is-selected'));
		assert.strictEqual(selectedCard.attributes['aria-pressed'], 'true');
		assert.strictEqual(selectedCard.attributes['aria-selected'], 'true');
		assert.strictEqual(findNodes(selectedCard, function(node) {
			return hasClass(node, 'fibocom-sms-selection-check');
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
			return hasClass(node, 'fibocom-conversation-header');
		}).length, 0, 'the long-press synthetic click must not open chat');
		assert.ok(hasClass(smsCardWithText(harness.root, 'Hold this message'),
			'fibocom-sms-card-selected'));

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
			return hasClass(node, 'fibocom-sms-card-selected');
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
			return hasClass(node, 'fibocom-conversation-header');
		}).length, 0, 'an embedded-control click must not open chat');
		const copyButton = findNodes(card, function(node) {
			return node.tag === 'button' && hasClass(node, 'fibocom-sms-number');
		})[0];
		const trashButton = findNodes(card, function(node) {
			return node.tag === 'button' && hasClass(node, 'fibocom-sms-delete-icon');
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
			'fibocom-sms-card-selected'), 'Space must select a focused message');
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
			return hasClass(node, 'fibocom-conversation-header');
		}).length, 1, 'Enter must open the focused conversation');
		const content = findNodes(harness.root, function(node) {
			return node.attributes && node.attributes.id === 'fibocom-sms';
		})[0];
		await content.attributes.keydown({
			key: 'Escape',
			preventDefault: function() { prevented++; }
		});
		assert.strictEqual(findNodes(harness.root, function(node) {
			return hasClass(node, 'fibocom-conversation-header');
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
			schema: 3,
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
	let redraws = 0;
	const compose = {};
	const folderSelect = {
		tagName: 'SELECT',
		closest: function() { return null; }
	};
	const composeInput = {
		tagName: 'INPUT',
		closest: function(selector) {
			return selector === '.fibocom-compose' ? compose : null;
		}
	};
	const documentDescriptor = Object.getOwnPropertyDescriptor(global, 'document');
	const pollingApi = {
		listModems: function() { return Promise.resolve(listResult); },
		listSms: function() { return Promise.resolve(smsResult); }
	};
	const pollingDom = {
		content: function(node, replacement) {
			redraws++;
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
		'htdocs/luci-static/resources/view/fibocom/sms.js', {
			dom: pollingDom, poll: polling, ui: inert, view: view,
			api: pollingApi, widgets: widgets
		});
	const pollingNode = pollingView.render({
		list: listResult,
		entries: [ { summary: summary, messages: smsResult } ]
	});
	const content = findNodes(pollingNode, function(node) {
		return node.attributes.id === 'fibocom-sms';
	})[0];

	content.contains = function(node) {
		return node === folderSelect || node === composeInput || node === compose;
	};
	Object.defineProperty(global, 'document', {
		configurable: true,
		value: { activeElement: folderSelect }
	});

	return pollCallback().then(function() {
		assert.strictEqual(redraws, 1,
			'a focused Folder select must not defer a ten-second SMS polling redraw');
		global.document.activeElement = composeInput;
		return pollCallback();
	}).then(function() {
		assert.strictEqual(redraws, 1,
			'a focused Write SMS field must still protect the draft from polling redraws');
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
					schema: 3,
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
		'htdocs/luci-static/resources/view/fibocom/sms.js', {
			dom: interactiveDom, poll: inert, ui: inert, view: view,
			api: numericSendApi, widgets: widgets
		});
	const numericSendNode = numericSendView.render({
		list: listResult,
		entries: [ { summary: summary, messages: smsResult } ]
	});
	const recipientInput = findNodes(numericSendNode, function(node) {
		return node.tag === 'input' && node.attributes.type === 'tel';
	})[0];
	const messageInput = findNodes(numericSendNode, function(node) {
		return node.tag === 'textarea';
	})[0];
	const composeForm = findNodes(numericSendNode, function(node) {
		return node.tag === 'form' && hasClass(node, 'fibocom-compose-form');
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
		const module = evaluate(`htdocs/luci-static/resources/view/fibocom/${name}.js`,
			viewDependencies);

		return normalizeDom(module.render(snapshot));
	});
}

function renderFreshScannedLock(width) {
	return withViewport(width, function() {
		const module = evaluate('htdocs/luci-static/resources/view/fibocom/lock.js', {
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
	entries: [ { summary: summary, overview: overviewResult } ]
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

const uiCss = read('htdocs/luci-static/resources/fibocom/fibocom.css');
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
assert.match(uiCss,
	/\.fibocom-page \.fibocom-overview-grid\s*\{[\s\S]*?grid-template-columns:\s*repeat\(2, minmax\(0, 1fr\)\)/,
	'Overview must use two balanced desktop columns');
assert.match(uiCss,
	/@media screen and \(max-width: 850px\)[\s\S]*?\.fibocom-page \.fibocom-overview-grid\s*\{[\s\S]*?grid-template-columns:\s*minmax\(0, 1fr\)/,
	'Overview groups must reflow to one column without dropping fields');

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
assert.ok(smsSource.includes("E('details'"));
assert.ok(smsSource.includes('composeOpen'));
assert.ok(smsSource.includes("_('Write SMS')"));
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

const lockSource = read('htdocs/luci-static/resources/view/fibocom/lock.js');
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
assert.ok(!lockSource.includes('Band Lock uses ModemManager SetCurrentBands.'));
assert.match(uiCss,
	/\.fibocom-page \.fibocom-band-checkboxes\s*\{[\s\S]*?grid-template-columns:\s*repeat\(auto-fill/,
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
assert.match(uiCss,
	/\.fibocom-page \.fibocom-cell-cards\s*\{[\s\S]*?grid-template-columns:\s*repeat\(auto-fit, minmax\(18rem, 1fr\)\)/,
	'scan cards must form a responsive desktop and mobile grid');
assert.ok(lockSource.includes("_('Tap line to use')"));
assert.ok(!lockSource.includes("_('Use')"),
	'scan selection must use the whole row instead of a separate button');
assert.ok(lockSource.includes("return E('button'"),
	'scan rows must use native keyboard-accessible buttons');
assert.match(uiCss,
	/\.fibocom-page \.fibocom-cell-input-grid\s*\{[\s\S]*?grid-template-columns:\s*repeat\(2, minmax\(0, 1fr\)\)/,
	'EARFCN and PCI inputs must remain side by side on mobile');
assert.ok(lockSource.includes('redrawPending') && lockSource.includes("'focusout'"),
	'Lock polling must preserve focused mobile editors');
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

assert.ok(uiCss.includes('SPDX-License-Identifier: Apache-2.0'));
for (const selector of [
	'.fibocom-page .fibocom-kv-row.cbi-value',
	'.fibocom-page .fibocom-form-row.cbi-value',
	'.fibocom-page .fibocom-actions.cbi-page-actions',
	'.fibocom-page .fibocom-cell-card',
	'.fibocom-page .fibocom-sms-card'
]) {
	assert.ok(uiCss.includes(selector), `shared responsive CSS must style ${selector}`);
}
assert.match(uiCss,
	/\.fibocom-page \.fibocom-sms-message\s*\{[\s\S]*?border-left:\s*\.25rem solid/,
	'SMS bodies must have a distinct readable visual marker');
assert.match(uiCss,
	/\.fibocom-page \.fibocom-sms-message\s*\{[\s\S]*?width:\s*100%/,
	'SMS bodies must use the full card width below their heading');
assert.match(uiCss,
	/\.fibocom-page \.fibocom-sms-number\s*\{[\s\S]*?cursor:\s*copy/,
	'number-like SMS tokens must have a distinct copy affordance');
assert.match(uiCss,
	/\.fibocom-copy-buffer\s*\{[\s\S]*?opacity:\s*0/,
	'HTTP LuCI sessions must retain a non-disruptive clipboard fallback');
assert.match(uiCss,
	/\.fibocom-page \.fibocom-compose-summary\s*\{[\s\S]*?cursor:\s*pointer/,
	'Write SMS must expose an obvious collapsible summary control');
assert.match(uiCss,
	/\.fibocom-page \.fibocom-sms-delete-icon\s*\{[\s\S]*?position:\s*absolute[\s\S]*?width:\s*2\.75rem[\s\S]*?height:\s*2\.75rem/,
	'the top-right trash action must retain an accessible 44px touch target');
assert.match(uiCss,
	/\.fibocom-page \.fibocom-sms-card-selected\s*\{[\s\S]*?background:/,
	'selected SMS cards must have a visible theme-aware state');
assert.ok(uiCss.includes('.fibocom-page .fibocom-sms-trash-glyph::before') &&
	uiCss.includes('.fibocom-page .fibocom-sms-trash-glyph::after'),
	'the delete action must render a theme-aware trash glyph without an emoji asset');
assert.match(uiCss,
	/\.fibocom-page \.fibocom-sms-list-actions\.cbi-page-actions\s*\{[\s\S]*?justify-content:\s*flex-start/,
	'Delete all must remain at the left edge of the message toolbar');
assert.match(uiCss,
	/\.fibocom-page \.fibocom-sms-chat-list \.fibocom-sms-card\s*\{[\s\S]*?width:\s*min\(88%, 46rem\)/,
	'conversation cards must use a compact responsive chat width');
assert.match(uiCss, /@media screen and \(max-width: 600px\)/,
	'the shared stylesheet must include a phone breakpoint');
assert.match(uiCss,
	/\.fibocom-page \.fibocom-scan-actions\.cbi-page-actions\s*\{[\s\S]*?margin-top:\s*\.75rem/,
	'the PCI scan controls must retain space below the lock-status rows');
assert.match(uiCss,
	/@media screen and \(max-width: 600px\)[\s\S]*?\.fibocom-page \.fibocom-mode-choices\s*\{[\s\S]*?grid-template-columns:\s*repeat\(3, minmax\(0, 1fr\)\)/,
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
	'Write SMS', 'Delete SMS', '(active)', 'Tap line to use', 'Invert', 'Lock status',
	'Locked', 'Unlocked', 'Any Supported bands',
	'Explicit LTE bands', 'Modem Info', 'Modem Status',
	'Band and Cell Status', 'Signal Status', 'Band Lock uses ModemManager',
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
	'Direction', 'Discharge timestamp', 'PDU type', 'Delivery state',
	'Message reference', 'Storage', 'Binary data present',
	'Messages are read, sent, and deleted through ModemManager. Recipient numbers and message text remain only in this authorized view and are never written to application logs.',
	'Band Lock uses ModemManager SetCurrentBands. PCI/EARFCN Lock is an explicit expert build path; only an exact live-validated hardware and firmware tuple can use its fixed command state machine.'
]) {
	assert.ok(!pot.includes(`msgid "${removedText}"`),
		`translation template must remove obsolete UI text: ${removedText}`);
}
assert.strictEqual(listFiles(appRoot).filter(function(file) {
	return file.endsWith('.lua');
}).length, 0, 'legacy Lua controllers and models are forbidden');

testSmsInteractions()
	.then(testSmsBulkDeletion)
	.then(testSmsDeleteAllPreparation)
	.then(testFocusedFolderPolling)
	.then(function() {
	console.log('luci-app-fibocom static checks: OK');
}).catch(function(error) {
	console.error(error && error.stack ? error.stack : error);
	process.exitCode = 1;
});
