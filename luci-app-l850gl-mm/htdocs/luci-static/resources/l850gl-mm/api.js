// SPDX-FileCopyrightText: 2026 As Tsaqib
// SPDX-License-Identifier: Apache-2.0

'use strict';
'require baseclass';
'require rpc';

function declare(object, method, params) {
	const specification = {
		object: object,
		method: method,
		reject: true,
		expect: { '': {} }
	};

	if (params)
		specification.params = params;
	return rpc.declare(specification);
}

const callListModems = declare('l850gl.mm', 'list_modems');
const callGetOverview = declare('l850gl.mm', 'get_overview', [ 'modem_id' ]);
const callGetLockStatus = declare('l850gl.mm', 'get_lock_status', [ 'modem_id' ]);
const callSetBands = declare('l850gl.mm', 'set_bands', [
	'modem_id', 'generation', 'bands', 'confirm'
]);
const callSetModes = declare('l850gl.mm', 'set_modes', [
	'modem_id', 'generation', 'allowed', 'preferred', 'confirm'
]);
const callListSms = declare('l850gl.mm', 'list_sms', [
	'modem_id', 'folder', 'limit', 'cursor'
]);
const callSendSms = declare('l850gl.mm', 'send_sms', [
	'modem_id', 'generation', 'messaging_generation', 'recipient', 'text', 'client_token'
]);
const callDeleteSms = declare('l850gl.mm', 'delete_sms', [
	'modem_id', 'generation', 'messaging_generation', 'sms_id', 'confirm'
]);

const callCellScan = declare('l850gl.mm.l850', 'cell_scan', [
	'modem_id', 'generation'
]);
const callGetCarrierInfo = declare('l850gl.mm.l850', 'get_carrier_info', [
	'modem_id', 'generation'
]);
const callCellLockStatus = declare('l850gl.mm.l850', 'cell_lock_status', [
	'modem_id', 'generation'
]);
const callSetCellLock = declare('l850gl.mm.l850', 'set_cell_lock', [
	'modem_id', 'generation', 'earfcn', 'pci', 'confirm'
]);
const callClearCellLock = declare('l850gl.mm.l850', 'clear_cell_lock', [
	'modem_id', 'generation', 'confirm'
]);

return baseclass.extend({
	SCHEMA_VERSION: 4,

	listModems: function() {
		return callListModems();
	},

	getOverview: function(modemId) {
		return callGetOverview(modemId);
	},

	getLockStatus: function(modemId) {
		return callGetLockStatus(modemId);
	},

	setBands: function(modemId, generation, bands, confirm) {
		return callSetBands(modemId, generation, bands, confirm);
	},

	setModes: function(modemId, generation, allowed, preferred, confirm) {
		return callSetModes(modemId, generation, allowed, preferred, confirm);
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
	},

	cellScan: function(modemId, generation) {
		return callCellScan(modemId, generation);
	},

	getCarrierInfo: function(modemId, generation) {
		return callGetCarrierInfo(modemId, generation);
	},

	cellLockStatus: function(modemId, generation) {
		return callCellLockStatus(modemId, generation);
	},

	setCellLock: function(modemId, generation, earfcn, pci, confirm) {
		return callSetCellLock(modemId, generation, earfcn,
			pci == null ? undefined : pci, confirm);
	},

	clearCellLock: function(modemId, generation, confirm) {
		return callClearCellLock(modemId, generation, confirm);
	}
});
