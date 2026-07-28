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
			return Promise.all([
				api.getOverview(summary.modem_id).catch(transportResult),
				api.getCarrierInfo(summary.modem_id, summary.generation)
					.catch(transportResult)
			]).then(function(results) {
				return { summary: summary, overview: results[0], carrier: results[1] };
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

		return [ entry[0], widgets.badge(capability.state, capability.state) ];
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

function identifierValue(value) {
	if (!value)
		return _('Unavailable');
	return E('code', {
		'class': 'fibocom-identifier',
		'dir': 'ltr',
		'tabindex': '0',
		'title': _('Select to copy')
	}, [ value ]);
}

function usbModeLabel(mode) {
	switch (mode) {
	case 'mbim':
		return 'MBIM';
	case 'ncm':
		return 'NCM';
	default:
		return _('Unknown');
	}
}

function lteBandLabel(band) {
	return 'B%d'.format(band);
}

function carrierDetail(role, carrier) {
	return E('div', { 'class': 'fibocom-carrier-detail' }, [
		E('strong', {}, [ role ]), ': ',
		_('B%d, EARFCN %d, PCI %d, DL/UL %s/%s MHz').format(
			carrier.band, carrier.earfcn, carrier.pci,
			carrier.dl_bandwidth_mhz, carrier.ul_bandwidth_mhz)
	]);
}

function carrierRows(result, summary) {
	const labels = [
		_('Active LTE Bands'),
		_('Primary LTE Band'),
		_('Secondary LTE Bands'),
		_('Active LTE Carriers'),
		_('LTE CA Details')
	];

	if (widgets.carrierInfoError(result, summary)) {
		return labels.map(function(label) {
			return [ label, widgets.badge(_('Unavailable'), 'unavailable') ];
		});
	}

	const details = [ carrierDetail(
		_('Primary carrier #%d').format(result.primary.index), result.primary) ];

	result.secondary.forEach(function(carrier) {
		details.push(carrierDetail(
			_('Secondary carrier #%d').format(carrier.index), carrier));
	});
	return [
		[ labels[0], result.active_bands.map(lteBandLabel).join(' + ') ],
		[ labels[1], lteBandLabel(result.primary.band) ],
		[ labels[2], result.secondary.length ?
			result.secondary.map(function(carrier) {
				return lteBandLabel(carrier.band);
			}).join(', ') : _('None') ],
		[ labels[3], result.active_carriers ],
		[ labels[4], E('div', { 'class': 'fibocom-carrier-details' }, details) ]
	];
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
		'class': 'cbi-section-node fibocom-overview-card'
	}, [
		E('h4', { 'class': 'fibocom-card-title' }, [ title ]),
		widgets.keyValueList(rows)
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
		[ _('USB Mode'), usbModeLabel(overview.usb_mode) ],
		[ _('IMEI'), identifierValue(identity.imei) ]
	];
	const modemStatusRows = [
		[ _('Modem state'), widgets.badge(modem.state, modem.state) ],
		[ _('Power'), widgets.badge(modem.power, modem.power) ],
		[ _('SIM present'), sim.present ],
		[ _('SIM lock'), sim.lock ],
		[ _('SIM Number'), identifierValue(sim.number) ],
		[ _('IMSI'), identifierValue(sim.imsi) ],
		[ _('ICCID'), identifierValue(sim.iccid) ],
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
		[ _('Serving cell status'), widgets.badge(servingCellLabel(serving),
			serving.state) ]
	];
	const activeCarrierRows = carrierRows(entry.carrier, summary);
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
		E('h3', { 'class': 'fibocom-device-title' }, [
			widgets.activeLabel(identity.model, true)
		]),
		E('div', { 'class': 'fibocom-overview-grid' }, [
			E('div', { 'class': 'fibocom-overview-column' }, [
				overviewGroup(_('Modem Info'), modemInfoRows),
				overviewGroup(_('Modem Status'), modemStatusRows)
			]),
			E('div', { 'class': 'fibocom-overview-column' }, [
				overviewGroup(_('Band and Cell Status'), bandAndCellRows, [
					E('h5', { 'class': 'fibocom-card-title fibocom-subsection-title' }, [
						_('LTE Carrier Aggregation')
					]),
					widgets.keyValueList(activeCarrierRows)
				]),
				overviewGroup(_('Signal Status'), signalRows, [
					E('h5', { 'class': 'fibocom-card-title' }, [ _('Capabilities') ]),
					widgets.keyValueList(capabilityRows(overview.capabilities))
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
		return E('div', { 'class': 'cbi-map fibocom-page fibocom-overview-page' }, [
			widgets.stylesheet(),
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
