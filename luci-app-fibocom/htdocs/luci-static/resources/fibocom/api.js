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

const callSetBands = rpc.declare({
	object: 'fibocom.mm',
	method: 'set_bands',
	params: [ 'modem_id', 'generation', 'bands', 'confirm' ],
	reject: true,
	expect: { '': {} }
});

const callSetRadio = rpc.declare({
	object: 'fibocom.mm',
	method: 'set_radio',
	params: [ 'modem_id', 'generation', 'enabled', 'confirm' ],
	reject: true,
	expect: { '': {} }
});

const callReset = rpc.declare({
	object: 'fibocom.mm',
	method: 'reset',
	params: [ 'modem_id', 'generation', 'confirm' ],
	reject: true,
	expect: { '': {} }
});

const callSetPrimarySimSlot = rpc.declare({
	object: 'fibocom.mm',
	method: 'set_primary_sim_slot',
	params: [ 'modem_id', 'generation', 'slot', 'confirm' ],
	reject: true,
	expect: { '': {} }
});

const callListSms = rpc.declare({
	object: 'fibocom.mm',
	method: 'list_sms',
	params: [ 'modem_id', 'folder', 'limit', 'cursor' ],
	reject: true,
	expect: { '': {} }
});

const callSendSms = rpc.declare({
	object: 'fibocom.mm',
	method: 'send_sms',
	params: [
		'modem_id', 'generation', 'messaging_generation',
		'recipient', 'text', 'client_token'
	],
	reject: true,
	expect: { '': {} }
});

const callDeleteSms = rpc.declare({
	object: 'fibocom.mm',
	method: 'delete_sms',
	params: [
		'modem_id', 'generation', 'messaging_generation',
		'sms_id', 'confirm'
	],
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
	},

	setBands: function(modemId, generation, bands, confirm) {
		return callSetBands(modemId, generation, bands, confirm);
	},

	setRadio: function(modemId, generation, enabled, confirm) {
		return callSetRadio(modemId, generation, enabled, confirm);
	},

	reset: function(modemId, generation, confirm) {
		return callReset(modemId, generation, confirm);
	},

	setPrimarySimSlot: function(modemId, generation, slot, confirm) {
		return callSetPrimarySimSlot(modemId, generation, slot, confirm);
	},

	listSms: function(modemId, folder, limit, cursor) {
		return callListSms(modemId, folder, limit, cursor);
	},

	sendSms: function(modemId, generation, messagingGeneration, recipient, text, clientToken) {
		return callSendSms(
			modemId, generation, messagingGeneration, recipient, text, clientToken);
	},

	deleteSms: function(modemId, generation, messagingGeneration, smsId, confirm) {
		return callDeleteSms(modemId, generation, messagingGeneration, smsId, confirm);
	}
});
