// SPDX-FileCopyrightText: 2026 As Tsaqib
// SPDX-License-Identifier: Apache-2.0

'use strict';
'require baseclass';
'require rpc';

const callListModems = rpc.declare({
	object: 'fibocom.mm',
	method: 'list_modems',
	reject: true,
	expect: { '': {} }
});

const callGetOverview = rpc.declare({
	object: 'fibocom.mm',
	method: 'get_overview',
	params: [ 'modem_id' ],
	reject: true,
	expect: { '': {} }
});

const callGetStatus = rpc.declare({
	object: 'fibocom.mm',
	method: 'get_status',
	params: [ 'modem_id' ],
	reject: true,
	expect: { '': {} }
});

const callGetCapabilities = rpc.declare({
	object: 'fibocom.mm',
	method: 'get_capabilities',
	params: [ 'modem_id' ],
	reject: true,
	expect: { '': {} }
});

return baseclass.extend({
	SCHEMA_VERSION: 1,

	listModems: function() {
		return callListModems();
	},

	getOverview: function(modemId) {
		return callGetOverview(modemId);
	},

	getStatus: function(modemId) {
		return callGetStatus(modemId);
	},

	getCapabilities: function(modemId) {
		return callGetCapabilities(modemId);
	}
});
