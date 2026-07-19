// SPDX-FileCopyrightText: 2026 As Tsaqib
// SPDX-License-Identifier: Apache-2.0
/* global api */

'use strict';
'require dom';
'require poll';
'require ui';
'require view';
'require fibocom.api as api';
'require fibocom.widgets as widgets';

function renderInventory(result) {
	const error = widgets.responseError(result);

	if (error)
		return widgets.errorPanel({ transport_error: error });

	const devices = widgets.devices(result);
	const rows = devices.filter(function(device) {
		return device != null && typeof device === 'object';
	}).map(function(device) {
		const presence = device.present === false ? _('Missing') : _('Present');
		const topology = widgets.display(device.topology_status, _('Unknown'));
		const usbId = device.vid || device.pid ? '%s:%s'.format(
			widgets.display(device.vid, '????'), widgets.display(device.pid, '????')) : '—';

		return [
			widgets.display(device.model || device.profile, _('Unmatched device')),
			E('code', {}, [ widgets.display(device.device_id) ]),
			widgets.badge(presence, device.present === false ? 'missing' : 'present'),
			widgets.display(device.composition, _('Unknown')),
			E('code', {}, [ usbId ]),
			widgets.badge(topology, topology),
			widgets.display(device.generation)
		];
	});
	const notice = widgets.schemaNotice(result);
	const content = [];

	if (notice)
		content.push(notice);

	content.push(widgets.table([
		_('Modem'),
		_('Device ID'),
		_('Presence'),
		_('Composition'),
		_('USB ID'),
		_('Topology'),
		_('Generation')
	], rows, _('No supported Fibocom modem has been discovered.')));

	return E('div', {}, content);
}

return view.extend({
	load: function() {
		return api.list().catch(function(error) {
			return { transport_error: widgets.display(error && error.message, _('RPC transport failure')) };
		});
	},

	handleRescan: function(event) {
		const button = event.currentTarget;

		button.disabled = true;
		button.classList.add('spinning');

		return api.rescan('manual', 'manual', 'change').then(function(result) {
			const error = widgets.responseError(result);

			if (error)
				throw new Error(error);

			ui.addNotification(null, E('p', {}, [
				_('The Fibocom inventory rescan was scheduled successfully.')
			]), 'info');
		}).catch(function(error) {
			ui.addNotification(null, E('p', {}, [
				_('Unable to schedule a Fibocom inventory rescan: %s')
					.format(widgets.display(error && error.message, _('Unknown error')))
			]), 'error');
		}).finally(function() {
			button.disabled = false;
			button.classList.remove('spinning');
		});
	},

	render: function(result) {
		const inventory = E('div', { 'id': 'fibocom-inventory' }, [ renderInventory(result) ]);

		poll.add(function() {
			return api.list().then(function(next) {
				dom.content(inventory, renderInventory(next));
			}).catch(function(error) {
				dom.content(inventory, widgets.errorPanel(error));
			});
		}, 5);

		const actions = [];

		if (L.hasViewPermission()) {
			actions.push(E('button', {
				'class': 'cbi-button cbi-button-action',
				'click': ui.createHandlerFn(this, 'handleRescan')
			}, [ _('Rescan devices') ]));
		}

		return E('div', { 'class': 'cbi-map' }, [
			E('h2', {}, [ _('Fibocom Modem') ]),
			E('div', { 'class': 'cbi-map-descr' }, [
				_('Inventory is read from the fibocomd cache. Opening this page never probes a modem port.')
			]),
			E('div', { 'class': 'alert-message warning' }, [
				E('strong', {}, [ _('Shadow mode is active.') ]),
				' ',
				_('fibocomd observes USB topology but does not claim, configure, or dial the modem.')
			]),
			E('div', { 'class': 'cbi-section' }, [
				E('h3', {}, [ _('Detected devices') ]),
				inventory
			]),
			E('div', { 'class': 'cbi-page-actions' }, actions)
		]);
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
