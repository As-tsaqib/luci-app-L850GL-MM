// SPDX-FileCopyrightText: 2026 As Tsaqib
// SPDX-License-Identifier: Apache-2.0
/* global api */

'use strict';
'require dom';
'require poll';
'require view';
'require l850gl-mm.api as api';
'require l850gl-mm.widgets as widgets';

const CARRIER_CACHE_MAX_ENTRIES = 64;
const CARRIER_CACHE_TTL_MS = 30000;
const carrierCache = Object.create(null);

function transportResult(error) {
	return {
		transport_error: widgets.display(error && error.message, _('RPC transport failure'))
	};
}

function clearCarrierCache() {
	Object.keys(carrierCache).forEach(function(modemId) {
		delete carrierCache[modemId];
	});
}

function pruneCarrierCache(summaries) {
	const live = Object.create(null);

	if (!Array.isArray(summaries) || summaries.length > CARRIER_CACHE_MAX_ENTRIES) {
		clearCarrierCache();
		return;
	}
	summaries.forEach(function(summary) {
		live[summary.modem_id] = summary.generation;
	});
	Object.keys(carrierCache).forEach(function(modemId) {
		if (live[modemId] !== carrierCache[modemId].generation)
			delete carrierCache[modemId];
	});
}

function carrierTransientError(result, summary) {
	const error = widgets.object(result && result.error);
	const hasModemId = widgets.isObject(result) &&
		Object.prototype.hasOwnProperty.call(result, 'modem_id');
	const hasGeneration = widgets.isObject(result) &&
		Object.prototype.hasOwnProperty.call(result, 'generation');

	if (!widgets.isObject(result) || result.schema !== widgets.SCHEMA_VERSION ||
	    !Number.isSafeInteger(result.generated_at) || result.generated_at < 0 ||
	    result.ok !== false || [ 'busy', 'rate_limited' ].indexOf(result.state) === -1 ||
	    !widgets.isObject(error) || error.code !== result.state ||
	    typeof error.message !== 'string' || error.message.length > 256 ||
	    error.message.indexOf('\0') !== -1 || error.retryable !== true ||
	    hasModemId !== hasGeneration)
		return false;
	if (hasModemId && !widgets.identityMatches(result, summary))
		return false;
	if (result.state === 'rate_limited') {
		return hasModemId && Number.isSafeInteger(result.retry_after_ms) &&
			result.retry_after_ms >= 1 && result.retry_after_ms <= 5000;
	}
	return !Object.prototype.hasOwnProperty.call(result, 'retry_after_ms');
}

function rememberCarrier(result, summary, receivedAtMs) {
	if (carrierCache[summary.modem_id] == null &&
	    Object.keys(carrierCache).length >= CARRIER_CACHE_MAX_ENTRIES) {
		const oldestModemId = Object.keys(carrierCache).sort(function(left, right) {
			return carrierCache[left].received_at_ms - carrierCache[right].received_at_ms;
		})[0];

		delete carrierCache[oldestModemId];
	}
	carrierCache[summary.modem_id] = {
		generation: summary.generation,
		received_at_ms: receivedAtMs,
		result: result
	};
}

function selectCarrier(result, summary) {
	const now = Date.now();
	const cached = carrierCache[summary.modem_id];

	if (widgets.carrierInfoError(result, summary) == null) {
		rememberCarrier(result, summary, now);
		return result;
	}
	if (carrierTransientError(result, summary) && cached != null &&
	    cached.generation === summary.generation) {
		const age = now - cached.received_at_ms;

		if (Number.isSafeInteger(age) && age >= 0 && age <= CARRIER_CACHE_TTL_MS)
			return cached.result;
	}
	delete carrierCache[summary.modem_id];
	return result;
}

function loadSnapshots() {
	return api.listModems().then(function(listResult) {
		if (widgets.listError(listResult)) {
			clearCarrierCache();
			return { list: listResult, entries: [] };
		}
		const summaries = widgets.modems(listResult);

		pruneCarrierCache(summaries);
		return Promise.all(summaries.map(function(summary) {
			return Promise.all([
				api.getOverview(summary.modem_id).catch(transportResult),
				api.getLockStatus(summary.modem_id).catch(transportResult),
				api.getCarrierInfo(summary.modem_id, summary.generation)
					.catch(transportResult)
			]).then(function(results) {
				return {
					summary: summary,
					overview: results[0],
					lock: results[1],
					carrier: selectCarrier(results[2], summary)
				};
			});
		})).then(function(entries) {
			return { list: listResult, entries: entries };
		});
	}).catch(function(error) {
		clearCarrierCache();
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

function lteBands(bands) {
	const seen = Object.create(null);
	const values = [];

	if (!Array.isArray(bands))
		return null;

	for (let index = 0; index < bands.length; index++) {
		const band = bands[index];
		const match = /^eutran-([0-9]+)$/.exec(band);
		const number = match ? Number(match[1]) : 0;

		if (!match) {
			if (/^eutran-/.test(band))
				return null;
			continue;
		}
		if (number < 1 || number > 85 || seen[number])
			return null;
		seen[number] = true;
		values.push(number);
	}
	return values.sort(function(left, right) { return left - right; });
}

function sameBands(left, right) {
	return left.length === right.length && left.every(function(band, index) {
		return band === right[index];
	});
}

function friendlyCurrentBands(lock, summary) {
	if (widgets.lockError(lock, summary))
		return _('Unavailable');
	if ([ 'automatic', 'explicit' ].indexOf(lock.band_selection) === -1)
		return _('Unavailable');
	if (lock.band_selection === 'automatic')
		return lock.current_bands.length === 1 && lock.current_bands[0] === 'any' ?
			_('any(automatic)') : _('Unavailable');
	if (lock.current_bands.indexOf('any') !== -1 ||
	    lock.supported_bands.indexOf('any') !== -1)
		return _('Unavailable');

	const current = lteBands(lock.current_bands);
	const supported = lteBands(lock.supported_bands);

	if (current === null || supported === null || current.length === 0 ||
	    supported.length === 0 || current.some(function(band) {
		return supported.indexOf(band) === -1;
	}))
		return _('Unavailable');
	if (sameBands(current, supported))
		return _('any(automatic)');
	return current.length ? current.map(function(band) {
		return 'B%d'.format(band);
	}).join(', ') : _('Unavailable');
}

function signalMetric(value, unit) {
	return value == null ? null : '%s %s'.format(value, unit);
}

function identifierValue(value) {
	if (!value)
		return _('Unavailable');
	return E('code', {
		'class': 'l850gl-mm-identifier',
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
	const detail = _('B%d, EARFCN %d, PCI %d, DL/UL %s/%s MHz').format(
			carrier.band, carrier.earfcn, carrier.pci,
			carrier.dl_bandwidth_mhz, carrier.ul_bandwidth_mhz);

	return E('div', { 'class': 'l850gl-mm-carrier-detail' }, role ? [
		E('strong', {}, [ role ]), ': ', detail
	] : [ detail ]);
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

	const details = [ carrierDetail(null, result.primary) ];

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
		[ labels[4], E('div', { 'class': 'l850gl-mm-carrier-details' }, details) ]
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
		'class': 'cbi-section l850gl-mm-overview-section'
	}, [
		E('div', { 'class': 'cbi-title' }, [
			E('h3', {}, [ title ])
		]),
		widgets.keyValueList(rows)
	].concat(extra || []));
}

function renderDevice(entry) {
	const error = widgets.overviewError(entry.overview, entry.summary);
	const summary = widgets.object(entry.summary);

	if (error) {
		return E('div', { 'class': 'cbi-section' }, [
			E('h3', {}, [ widgets.display(summary.model, _('L850-GL modem')) ]),
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
	const carrierAvailable = widgets.carrierInfoError(entry.carrier, summary) == null;
	const serving = overview.serving_cell.state === 'available' || !carrierAvailable ?
		overview.serving_cell : {
			state: 'available',
			reason: 'l850-gtcainfo',
			earfcn: entry.carrier.primary.earfcn,
			pci: entry.carrier.primary.pci,
			band: entry.carrier.primary.band,
			rsrp: null,
			rsrq: null
		};
	const warning = widgets.warningList(overview.warnings.map(warningText));
	const modemInfoRows = [
		[ _('Manufacturer'), identity.manufacturer ],
		[ _('Model'), identity.model ],
		[ _('Firmware'), identity.revision ],
		[ _('USB Mode'), usbModeLabel(overview.usb_mode) ],
		[ _('IMEI'), identifierValue(identity.imei) ]
	];
	const modemStatusRows = [
		[ _('Modem state'), widgets.badge(modem.state, modem.state) ],
		[ _('Power'), widgets.badge(modem.power, modem.power) ],
		[ _('SIM present'), sim.present ],
		[ _('SIM lock'), sim.lock ],
		[ _('Access technology'), network.access ],
		[ _('Data interface'), bearer.interface ?
			E('code', {}, [ bearer.interface ]) : null ]
	];

	if (modem.voltage_mv != null)
		modemStatusRows.splice(2, 0,
			[ _('Modem voltage'), '%d mV'.format(modem.voltage_mv) ]);
	const simRows = [
		[ _('Operator'), network.operator ],
		[ _('Registration'), widgets.badge(network.registration, network.registration) ],
		[ _('Roaming'), network.roaming ]
	];

	if (sim.number)
		simRows.push([ _('SIM Number'), identifierValue(sim.number) ]);
	simRows.push([ _('ICCID'), identifierValue(sim.iccid) ]);
	const bandAndCellRows = [
		[ _('Current bands'), friendlyCurrentBands(entry.lock, summary) ],
		[ _('Serving cell status'), widgets.badge(servingCellLabel(serving),
			serving.state) ]
	];
	const activeCarrierRows = carrierRows(entry.carrier, summary);
	const signalRows = [
		[ _('RSRP'), signalMetric(signal.rsrp, 'dBm') ],
		[ _('RSRQ'), signalMetric(signal.rsrq, 'dB') ],
		[ _('SINR'), signalMetric(signal.sinr, 'dB') ]
	];

	if (serving.state === 'available') {
		bandAndCellRows.push([ _('Serving EARFCN'), serving.earfcn ]);
		bandAndCellRows.push([ _('Serving PCI'), serving.pci ]);
	}

	const children = [
		E('div', { 'class': 'l850gl-mm-overview-list' }, [
			overviewGroup(_('Modem Info'), modemInfoRows),
			overviewGroup(_('Modem Status'), modemStatusRows, [
				E('h4', { 'class': 'l850gl-mm-subsection-title' }, [
					_('SIMs')
				]),
				widgets.keyValueList(simRows)
			]),
			overviewGroup(_('Band and Cell Status'), bandAndCellRows, [
				E('h4', { 'class': 'l850gl-mm-subsection-title' }, [
					_('LTE Carrier Aggregation')
				]),
				widgets.keyValueList(activeCarrierRows.concat(signalRows))
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
			_('No L850-GL modem is currently exported by ModemManager.')
		]);
	}
	return E('div', {}, snapshot.entries.map(renderDevice));
}

return view.extend({
	load: loadSnapshots,

	render: function(snapshot) {
		const content = E('div', { 'id': 'l850gl-mm-overview' }, [ renderSnapshots(snapshot) ]);

		poll.add(function() {
			return loadSnapshots().then(function(next) {
				dom.content(content, renderSnapshots(next));
			});
		}, 10);
		return E('div', { 'class': 'cbi-map l850gl-mm-page l850gl-mm-overview-page' }, [
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
