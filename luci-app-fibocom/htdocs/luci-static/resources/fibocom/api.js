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

const callListModems = declare('fibocom.mm', 'list_modems');
const callGetOverview = declare('fibocom.mm', 'get_overview', [ 'modem_id' ]);
const callGetLockStatus = declare('fibocom.mm', 'get_lock_status', [ 'modem_id' ]);
const callSetBands = declare('fibocom.mm', 'set_bands', [
	'modem_id', 'generation', 'bands', 'confirm'
]);
const callListSms = declare('fibocom.mm', 'list_sms', [
	'modem_id', 'folder', 'limit', 'cursor'
]);
const callSendSms = declare('fibocom.mm', 'send_sms', [
	'modem_id', 'generation', 'messaging_generation', 'recipient', 'text', 'client_token'
]);
const callDeleteSms = declare('fibocom.mm', 'delete_sms', [
	'modem_id', 'generation', 'messaging_generation', 'sms_id', 'confirm'
]);

const callCellScan = declare('fibocom.mm.l850', 'cell_scan', [
	'modem_id', 'generation'
]);
const callCellLockStatus = declare('fibocom.mm.l850', 'cell_lock_status', [
	'modem_id', 'generation'
]);
const callSetCellLock = declare('fibocom.mm.l850', 'set_cell_lock', [
	'modem_id', 'generation', 'earfcn', 'pci', 'confirm'
]);
const callClearCellLock = declare('fibocom.mm.l850', 'clear_cell_lock', [
	'modem_id', 'generation', 'confirm'
]);

return baseclass.extend({
	SCHEMA_VERSION: 2,

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
