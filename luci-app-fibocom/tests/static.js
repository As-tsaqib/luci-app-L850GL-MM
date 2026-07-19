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
const menuRoot = menu['admin/network/fibocom'];
const aclGroup = acl['luci-app-fibocom'];

assert.deepStrictEqual(menuRoot.depends.acl, [ 'luci-app-fibocom' ]);
assert.strictEqual(menuRoot.action.type, 'firstchild');

const expectedViews = [ 'overview', 'status', 'settings', 'diagnostics' ];
expectedViews.forEach(function(name) {
	const entry = menu[`admin/network/fibocom/${name}`];

	assert.ok(entry, `menu entry for ${name} must exist`);
	assert.strictEqual(entry.action.type, 'view');
	assert.strictEqual(entry.action.path, `fibocom/${name}`);
	assert.ok(fs.existsSync(path.join(resources, 'view/fibocom', `${name}.js`)),
		`${name}.js must exist`);
});

assert.deepStrictEqual(Object.keys(aclGroup.read), [ 'ubus' ]);
assert.deepStrictEqual(Object.keys(aclGroup.write), [ 'ubus' ]);
assert.deepStrictEqual(Object.keys(aclGroup.read.ubus), [ 'fibocom' ]);
assert.deepStrictEqual(Object.keys(aclGroup.write.ubus), [ 'fibocom' ]);
assert.deepStrictEqual(aclGroup.read.ubus.fibocom.slice().sort(),
	[ 'capabilities', 'diagnostics', 'list', 'status' ]);
assert.deepStrictEqual(aclGroup.write.ubus.fibocom, [ 'rescan' ]);

const aclSource = fs.readFileSync(aclPath, 'utf8');
[ 'cgi-io', 'file', 'uci', 'exec' ].forEach(function(forbidden) {
	assert.ok(!aclSource.includes(`"${forbidden}"`), `ACL must not grant ${forbidden}`);
});

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
	[ 'fibocom', 'fibocom', 'fibocom', 'fibocom', 'fibocom' ]);
assert.deepStrictEqual(declarations.map(function(call) { return call.method; }),
	[ 'list', 'status', 'capabilities', 'diagnostics', 'rescan' ]);
assert.ok(declarations.every(function(call) { return call.reject === true; }),
	'ubus and ACL errors must reject instead of looking like empty cached data');
assert.deepStrictEqual(declarations[1].params, [ 'device_id' ]);
assert.deepStrictEqual(declarations[2].params, [ 'device_id' ]);
assert.deepStrictEqual(declarations[3].params, [ 'device_id' ]);
assert.deepStrictEqual(declarations[4].params, [ 'reason', 'subsystem', 'action' ]);
assert.strictEqual(declarations[4].nobatch, true);
assert.deepStrictEqual(api.rescan('manual', 'manual', 'change').arguments,
	[ 'manual', 'manual', 'change' ]);

global._ = function(value) { return value; };
global.E = function(tag, attributes, children) {
	return {
		tag,
		nodeType: 1,
		attributes: attributes || {},
		children: children || [],
		querySelectorAll: function() { return []; }
	};
};
global.L = {
	hasViewPermission: function() { return true; },
	url: function() { return Array.from(arguments).join('/'); }
};

if (!String.prototype.format) {
	Object.defineProperty(String.prototype, 'format', {
		value: function() {
			const values = arguments;
			let offset = 0;

			return String(this).replace(/%s/g, function() {
				return String(values[offset++]);
			});
		}
	});
}

const widgets = evaluate('htdocs/luci-static/resources/fibocom/widgets.js', { baseclass });
assert.deepStrictEqual(widgets.devices({ schema: 1, devices: [] }), []);
assert.strictEqual(widgets.devices({ devices: [ { device_id: 'sha256:test' } ] })[0].device_id,
	'sha256:test');
assert.strictEqual(widgets.responseError({ schema: 1 }), null);
assert.strictEqual(widgets.responseError({ error: { reason: 'not_found' } }), 'not_found');
assert.strictEqual(widgets.portRows({
	ports: {
		at_primary: { node: 'ttyACM0', interface_number: '02', driver: 'cdc_acm', present: true }
	}
})[0][0], 'at_primary');
const detailedPort = widgets.portRows({
	ports: { wdm: 'cdc-wdm0' },
	interfaces: [ {
		number: '00',
		driver: 'cdc_mbim',
		role: 'mbim-control',
		wdms: [ { name: 'cdc-wdm0', interface_number: '00', driver: 'cdc_mbim' } ],
		ttys: [],
		netdevs: []
	} ]
})[0];
assert.strictEqual(detailedPort[1], 'cdc-wdm0');
assert.strictEqual(detailedPort[2], '00');
assert.strictEqual(detailedPort[3], 'cdc_mbim');
assert.strictEqual(widgets.portRows({
	ports: {
		at_primary: 'ttyACM0',
		ignored: [ 'ttyACM1' ],
		netdevs: [ 'wwan0' ]
	}
}).length, 3);
assert.strictEqual(widgets.portRows({
	interfaces: [ {
		number: '02',
		role: 'at-primary',
		driver: 'cdc_acm',
		ttys: [ { name: 'ttyACM0' } ],
		wdms: [],
		netdevs: []
	} ]
})[0][1], 'ttyACM0');

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
});

const overviewView = evaluate('htdocs/luci-static/resources/view/fibocom/overview.js', viewDependencies);
const statusView = evaluate('htdocs/luci-static/resources/view/fibocom/status.js', viewDependencies);
const settingsView = evaluate('htdocs/luci-static/resources/view/fibocom/settings.js', viewDependencies);
const diagnosticsView = evaluate('htdocs/luci-static/resources/view/fibocom/diagnostics.js', viewDependencies);
const deviceSummary = {
	device_id: 'sha256:test',
	generation: 3,
	present: true,
	profile: 'fibocom-l850-gl',
	composition: 'mbim',
	vid: '2cb7',
	pid: '0007',
	topology_status: 'complete'
};
const statusResult = Object.assign({
	schema: 1,
	shadow_mode: true,
	ports: [
		{ role: 'at_primary', node: 'ttyACM0', interface_number: '02', driver: 'cdc_acm' }
	]
}, deviceSummary);
const capabilityResult = {
	schema: 1,
	shadow_mode: true,
	device_id: 'sha256:test',
	capabilities: {
		connect: { state: 'unavailable', reason: 'shadow_mode' }
	}
};
const diagnosticResult = {
	schema: 1,
	shadow_mode: true,
	daemon: { state: 'running', device_count: 1 },
	profile: {
		id: 'fibocom-l850-gl',
		display_name: 'Fibocom L850-GL',
		schema_validated: true,
		hardware_validated: false
	},
	devices: [ Object.assign({ interfaces: statusResult.interfaces || [] }, deviceSummary) ],
	reconcile: { scan_id: 4, device_count: 1 }
};

assert.strictEqual(overviewView.render({ schema: 1, shadow_mode: true, devices: [ deviceSummary ] }).tag,
	'div');
assert.strictEqual(overviewView.render({ transport_error: 'connection lost' }).tag, 'div');
assert.strictEqual(statusView.render({
	list: { schema: 1, shadow_mode: true, devices: [ deviceSummary ] },
	entries: [ { summary: deviceSummary, status: statusResult, capabilities: capabilityResult } ]
}).tag, 'div');
assert.strictEqual(statusView.render({ list: { transport_error: 'permission denied' }, entries: [] }).tag,
	'div');
assert.strictEqual(settingsView.render().tag, 'div');
assert.strictEqual(diagnosticsView.render({
	list: { schema: 1, shadow_mode: true, devices: [ deviceSummary ] },
	daemon: diagnosticResult,
	devices: [ { summary: deviceSummary, result: diagnosticResult.devices[0] } ]
}).tag, 'div');
assert.strictEqual(diagnosticsView.render({
	list: { transport_error: 'connection lost' },
	daemon: { transport_error: 'connection lost' },
	devices: []
}).tag, 'div');

[ overviewView, statusView, settingsView, diagnosticsView ].forEach(function(module) {
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
	assert.doesNotMatch(source, /\b(?:exec_direct|cgi-io|mbimcli|uqmi|lpac)\b/i);
	assert.doesNotMatch(source, /JSON\.stringify\s*\(/,
		`${path.relative(appRoot, file)} must render allowlisted typed fields, not raw JSON`);
});

assert.ok(read('htdocs/luci-static/resources/view/fibocom/settings.js')
	.includes("L.url('admin/network/network')"));
assert.ok(read('htdocs/luci-static/resources/view/fibocom/overview.js')
	.includes("api.rescan('manual', 'manual', 'change')"));
assert.strictEqual(listFiles(appRoot).filter(function(file) { return file.endsWith('.lua'); }).length, 0,
	'legacy Lua controllers and models are forbidden');

const makefile = read('Makefile');
assert.ok(makefile.includes('PKG_LICENSE:=Apache-2.0'));
assert.ok(makefile.includes('include $(TOPDIR)/feeds/luci/luci.mk'));

console.log('luci-app-fibocom static checks: OK');
