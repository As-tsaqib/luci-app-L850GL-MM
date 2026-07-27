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
		const details = [
			widgets.badge(capability.state, capability.state),
			E('span', {}, [
				_('Mutation available'), ': ', widgets.display(capability.mutable)
			])
		];

		if (capability.reason) {
			details.push(E('span', { 'style': 'overflow-wrap:anywhere' }, [
				_('Reason'), ': ', capability.reason
			]));
		}

		return [
			entry[0],
			E('div', {
				'style': 'display:flex;flex-direction:column;align-items:flex-start;gap:.3em;min-width:0'
			}, details)
		];
	});
}

function friendlyCurrentBands(bands) {
	const groups = [
		{ key: 'utran', title: '3G', entries: [] },
		{ key: 'eutran', title: '4G', entries: [] },
		{ key: 'other', title: _('Other bands'), entries: [] }
	];
	const seen = Object.create(null);

	if (Array.isArray(bands) && bands.length === 1 && bands[0] === 'any')
		return _('Automatic');

	(bands || []).forEach(function(band) {
		const match = /^(utran|eutran)-([0-9]+)$/.exec(band);
		const label = match ? 'B' + String(Number(match[2])) :
			(band === 'any' ? _('Automatic') : band);
		const group = match ? groups[match[1] === 'utran' ? 0 : 1] : groups[2];
		const identity = group.key + ':' + label;

		if (seen[identity])
			return;
		seen[identity] = true;
		group.entries.push({
			label: label,
			number: match ? Number(match[2]) : null
		});
	});
	groups.forEach(function(group) {
		group.entries.sort(function(left, right) {
			if (left.number != null && right.number != null)
				return left.number - right.number;
			return left.label < right.label ? -1 : (left.label > right.label ? 1 : 0);
		});
	});
	const summaries = groups.filter(function(group) {
		return group.entries.length;
	}).map(function(group) {
		return '%s: %s'.format(group.title, group.entries.map(function(entry) {
			return entry.label;
		}).join(', '));
	});

	return summaries.length ? summaries.join(' | ') : [];
}

function signalMetric(value, unit) {
	return value == null ? null : '%s %s'.format(value, unit);
}

function servingCellLabel(serving) {
	if (serving.state === 'available')
		return _('Available');
	switch (serving.reason) {
	case 'refresh-pending':
		return _('Refresh pending');
	case 'standard-cell-info-unavailable':
		return _('Standard CellInfo unavailable');
	case 'standard-cell-info-malformed':
		return _('Malformed CellInfo rejected');
	case 'stale':
		return _('Stale');
	case 'device-gone':
		return _('Device removed');
	default:
		return _('Unavailable');
	}
}

function overviewGroup(title, rows, extra) {
	return E('div', {
		'class': 'cbi-section-node fibocom-overview-card',
		'style': 'min-width:0;max-width:100%;padding:.75em;box-sizing:border-box;overflow-wrap:anywhere'
	}, [
		E('h4', {}, [ title ]),
		widgets.keyValueTable(rows)
	].concat(extra || []));
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
	const modemInfoRows = [
		[ _('Manufacturer'), identity.manufacturer ],
		[ _('Model'), identity.model ],
		[ _('Revision'), identity.revision ]
	];
	const modemStatusRows = [
		[ _('Modem state'), widgets.badge(modem.state, modem.state) ],
		[ _('Power'), widgets.badge(modem.power, modem.power) ],
		[ _('SIM present'), sim.present ],
		[ _('SIM lock'), sim.lock ],
		[ _('Operator'), network.operator ],
		[ _('Registration'), widgets.badge(network.registration, network.registration) ],
		[ _('Roaming'), network.roaming ],
		[ _('Access technology'), network.access ],
		[ _('Bearer connected'), bearer.connected ],
		[ _('Data interface'), bearer.interface ?
			E('code', {}, [ bearer.interface ]) : null ]
	];
	const bandAndCellRows = [
		[ _('Current bands'), friendlyCurrentBands(overview.current_bands) ],
		[ _('Serving cell status'), widgets.badge(serving.state,
			servingCellLabel(serving)) ]
	];
	const signalRows = [
		[ _('Signal quality'), widgets.progress(signal.quality) ],
		[ _('RSRP'), signalMetric(signal.rsrp, 'dBm') ],
		[ _('RSRQ'), signalMetric(signal.rsrq, 'dB') ],
		[ _('SINR'), signalMetric(signal.sinr, 'dB') ]
	];

	if (serving.state === 'available') {
		bandAndCellRows.push([ _('Serving EARFCN'), serving.earfcn ]);
		bandAndCellRows.push([ _('Serving PCI'), serving.pci ]);
	}

	const children = [
		E('h3', {}, [ widgets.activeLabel(identity.model, true) ]),
		E('div', {
			'class': 'fibocom-overview-grid',
			'style': 'display:grid;grid-template-columns:repeat(auto-fit,minmax(16rem,1fr));gap:1em;align-items:start;width:100%'
		}, [
			E('div', {
				'class': 'fibocom-overview-column',
				'style': 'display:flex;flex-direction:column;gap:1em;min-width:0'
			}, [
				overviewGroup(_('Modem Info'), modemInfoRows),
				overviewGroup(_('Modem Status'), modemStatusRows)
			]),
			E('div', {
				'class': 'fibocom-overview-column',
				'style': 'display:flex;flex-direction:column;gap:1em;min-width:0'
			}, [
				overviewGroup(_('Band and Cell Status'), bandAndCellRows),
				overviewGroup(_('Signal Status'), signalRows, [
					E('h5', {}, [ _('Capabilities') ]),
					widgets.keyValueTable(capabilityRows(overview.capabilities))
				])
			])
		])
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
