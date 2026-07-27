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
	return api.listModems().then(function(listResult) {
		if (widgets.listError(listResult))
			return { list: listResult, entries: [] };
		return Promise.all(widgets.modems(listResult).map(function(summary) {
			return api.getOverview(summary.modem_id).catch(transportResult).then(function(overview) {
				return { summary: summary, overview: overview };
			});
		})).then(function(entries) {
			return { list: listResult, entries: entries };
		});
	}).catch(function(error) {
		return { list: transportResult(error), entries: [] };
	});
}

function warningText(code) {
	switch (code) {
	case 'sim-snapshot-incomplete':
		return _('The SIM snapshot is still loading. Check again after the next refresh.');
	case 'bearer-snapshot-incomplete':
		return _('The bearer snapshot is still loading. Check again after the next refresh.');
	case 'netifd-modemmanager-protocol-unavailable':
		return _('The netifd ModemManager protocol was not found; verify the modemmanager netifd package.');
	case 'mbim-composition-required':
		return _('This modem is not in the supported MBIM composition; mutations are disabled.');
	case 'mutations-disabled-hardware-attestation':
		return _('The exact L850-GL 2cb7:0007 hardware could not be attested; mutations are disabled.');
	default:
		return _('The bridge reported an unrecognized warning.');
	}
}

function capabilityRows(capabilities) {
	return [
		[ _('SMS'), capabilities.sms ],
		[ _('Band Lock'), capabilities.band_lock ],
		[ _('PCI/EARFCN Lock'), capabilities.pci_lock ]
	].map(function(entry) {
		const capability = widgets.object(entry[1]);

		return [
			entry[0],
			widgets.badge(capability.state, capability.state),
			capability.mutable,
			capability.reason
		];
	});
}

function renderDevice(entry) {
	const error = widgets.overviewError(entry.overview, entry.summary);
	const summary = widgets.object(entry.summary);

	if (error) {
		return E('div', { 'class': 'cbi-section' }, [
			E('h3', {}, [ widgets.display(summary.model, _('Fibocom modem')) ]),
			widgets.errorPanel(error)
		]);
	}

	const overview = entry.overview;
	const identity = overview.identity;
	const modem = overview.modem;
	const sim = overview.sim;
	const network = overview.network;
	const signal = overview.signal;
	const bearer = overview.bearer;
	const serving = overview.serving_cell;
	const warning = widgets.warningList(overview.warnings.map(warningText));
	const rows = [
		[ _('Manufacturer'), identity.manufacturer ],
		[ _('Model'), identity.model ],
		[ _('Revision'), identity.revision ],
		[ _('Modem state'), widgets.badge(modem.state, modem.state) ],
		[ _('Power'), widgets.badge(modem.power, modem.power) ],
		[ _('SIM present'), sim.present ],
		[ _('SIM lock'), sim.lock ],
		[ _('Operator'), network.operator ],
		[ _('Registration'), widgets.badge(network.registration, network.registration) ],
		[ _('Roaming'), network.roaming ],
		[ _('Access technology'), network.access ],
		[ _('Signal quality'), widgets.progress(signal.quality) ],
		[ _('RSRP'), signal.rsrp ],
		[ _('RSRQ'), signal.rsrq ],
		[ _('SINR'), signal.sinr ],
		[ _('Bearer connected'), bearer.connected ],
		[ _('Data interface'), bearer.interface ?
			E('code', {}, [ bearer.interface ]) : null ],
		[ _('Current bands'), overview.current_bands ]
	];

	if (serving.state === 'available') {
		rows.push([ _('Serving EARFCN'), serving.earfcn ]);
		rows.push([ _('Serving PCI'), serving.pci ]);
	}

	const children = [
		E('h3', {}, [ widgets.activeLabel(identity.model, true) ]),
		widgets.keyValueTable(rows, _('No overview fields are available for this modem.')),
		E('h4', {}, [ _('Capabilities') ]),
		widgets.table([
			_('Feature'), _('State'), _('Mutation available'), _('Reason')
		], capabilityRows(overview.capabilities))
	];

	if (warning)
		children.push(warning);
	return E('div', { 'class': 'cbi-section' }, children);
}

function renderSnapshots(snapshot) {
	const error = widgets.listError(snapshot.list);

	if (error)
		return widgets.errorPanel(error);
	if (!snapshot.entries.length) {
		return E('div', { 'class': 'alert-message notice' }, [
			_('No Fibocom modem is currently exported by ModemManager.')
		]);
	}
	return E('div', {}, snapshot.entries.map(renderDevice));
}

return view.extend({
	load: loadSnapshots,

	render: function(snapshot) {
		const content = E('div', { 'id': 'fibocom-overview' }, [ renderSnapshots(snapshot) ]);

		poll.add(function() {
			return loadSnapshots().then(function(next) {
				dom.content(content, renderSnapshots(next));
			});
		}, 10);
		return E('div', { 'class': 'cbi-map' }, [
			E('h2', {}, [ _('Overview') ]),
			E('div', { 'class': 'cbi-map-descr' }, [
				_('A concise ModemManager snapshot. Network configuration and connection intent remain owned by netifd.')
			]),
			content
		]);
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
