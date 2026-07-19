// SPDX-FileCopyrightText: 2026 As Tsaqib
// SPDX-License-Identifier: Apache-2.0

'use strict';
'require baseclass';
'require rpc';

const callList = rpc.declare({
	object: 'fibocom',
	method: 'list',
	reject: true,
	expect: { '': {} }
});

const callStatus = rpc.declare({
	object: 'fibocom',
	method: 'status',
	params: [ 'device_id' ],
	reject: true,
	expect: { '': {} }
});

const callCapabilities = rpc.declare({
	object: 'fibocom',
	method: 'capabilities',
	params: [ 'device_id' ],
	reject: true,
	expect: { '': {} }
});

const callDiagnostics = rpc.declare({
	object: 'fibocom',
	method: 'diagnostics',
	params: [ 'device_id' ],
	reject: true,
	expect: { '': {} }
});

const callRescan = rpc.declare({
	object: 'fibocom',
	method: 'rescan',
	params: [ 'reason', 'subsystem', 'action' ],
	nobatch: true,
	reject: true,
	expect: { '': {} }
});

return baseclass.extend({
	SCHEMA_VERSION: 1,

	list: function() {
		return callList();
	},

	status: function(deviceId) {
		return callStatus(deviceId);
	},

	capabilities: function(deviceId) {
		return callCapabilities(deviceId);
	},

	diagnostics: function(deviceId) {
		return callDiagnostics(deviceId);
	},

	rescan: function(reason, subsystem, action) {
		return callRescan(reason, subsystem, action);
	}
});
