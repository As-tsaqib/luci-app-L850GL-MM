// SPDX-FileCopyrightText: 2026 As Tsaqib
// SPDX-License-Identifier: Apache-2.0
/* global api */

'use strict';
'require dom';
'require poll';
'require view';
'require fibocom.api as api';
'require fibocom.widgets as widgets';

function transportResult(error) {
	return {
		transport_error: widgets.display(error && error.message, _('RPC transport failure'))
	};
}

function loadSnapshots() {
	return api.list().then(function(listResult) {
		const listError = widgets.responseError(listResult);

		if (listError)
			return { list: listResult, entries: [] };

		const requests = widgets.devices(listResult).filter(function(summary) {
			return summary != null && typeof summary === 'object' && typeof summary.device_id === 'string';
		}).map(function(summary) {
			return Promise.all([
				api.status(summary.device_id).catch(transportResult),
				api.capabilities(summary.device_id).catch(transportResult)
			]).then(function(results) {
				return {
					summary: summary,
					status: results[0],
					capabilities: results[1]
				};
			});
		});

		return Promise.all(requests).then(function(entries) {
			return { list: listResult, entries: entries };
		});
	}).catch(function(error) {
		return { list: transportResult(error), entries: [] };
	});
}

function renderDevice(entry) {
	const statusError = widgets.responseError(entry.status);
	const capabilityError = widgets.responseError(entry.capabilities);
	const status = widgets.device(entry.status);
	const record = Object.assign({}, entry.summary, status);
	const presence = record.present === false ? _('Missing') : _('Present');
	const topology = widgets.display(record.topology_status, _('Unknown'));
	const usbId = record.vid || record.pid ? '%s:%s'.format(
		widgets.display(record.vid, '????'), widgets.display(record.pid, '????')) : null;
	const children = [
		E('h3', {}, [ widgets.display(record.model || record.profile, _('Fibocom modem')) ]),
		widgets.keyValueTable([
			[ _('Device ID'), E('code', {}, [ widgets.display(record.device_id) ]) ],
			[ _('Generation'), record.generation ],
			[ _('Presence'), widgets.badge(presence, record.present === false ? 'missing' : 'present') ],
			[ _('Model'), record.model ],
			[ _('Profile'), record.profile ],
			[ _('Composition'), record.composition ],
			[ _('USB ID'), usbId ? E('code', {}, [ usbId ]) : null ],
			[ _('Topology'), widgets.badge(topology, topology) ],
			[ _('Cache updated'), record.updated_at || record.last_seen ],
			[ _('Shadow mode'), record.shadow_mode != null ? record.shadow_mode : entry.status.shadow_mode ]
		], _('No cached device details are available.'))
	];

	if (statusError)
		children.push(widgets.errorPanel({ transport_error: statusError }));
	else {
		children.push(E('h4', {}, [ _('Port roles') ]));
		children.push(widgets.table([
			_('Role'), _('Node'), _('USB interface'), _('Driver'), _('State')
		], widgets.portRows(entry.status), _('No port roles are present in the cached snapshot.')));
	}

	children.push(E('h4', {}, [ _('Capabilities') ]));

	if (capabilityError)
		children.push(widgets.errorPanel({ transport_error: capabilityError }));
	else
		children.push(widgets.table([
			_('Feature'), _('State'), _('Reason')
		], widgets.capabilityRows(entry.capabilities), _('No capabilities were reported.')));

	return E('div', { 'class': 'cbi-section' }, children);
}

function renderSnapshots(snapshot) {
	const error = widgets.responseError(snapshot.list);

	if (error)
		return widgets.errorPanel({ transport_error: error });

	if (!snapshot.entries.length)
		return E('div', { 'class': 'alert-message notice' }, [
			_('No supported Fibocom modem has been discovered.')
		]);

	return E('div', {}, snapshot.entries.map(renderDevice));
}

return view.extend({
	load: loadSnapshots,

	render: function(snapshot) {
		const status = E('div', { 'id': 'fibocom-status' }, [ renderSnapshots(snapshot) ]);

		poll.add(function() {
			return loadSnapshots().then(function(next) {
				dom.content(status, renderSnapshots(next));
			});
		}, 5);

		return E('div', { 'class': 'cbi-map' }, [
			E('h2', {}, [ _('Fibocom Modem Status') ]),
			E('div', { 'class': 'cbi-map-descr' }, [
				_('This page polls cached typed snapshots. It does not send AT or bearer commands.')
			]),
			status
		]);
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
