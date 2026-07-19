// SPDX-FileCopyrightText: 2026 As Tsaqib
// SPDX-License-Identifier: Apache-2.0

'use strict';
'require baseclass';

function isObject(value) {
	return value != null && typeof value === 'object' && !Array.isArray(value);
}

function object(value) {
	return isObject(value) ? value : {};
}

function display(value, fallback) {
	if (value == null || value === '')
		return fallback != null ? fallback : '—';

	if (value === true)
		return _('Yes');

	if (value === false)
		return _('No');

	if (Array.isArray(value)) {
		const values = value.filter(function(item) {
			return item == null || [ 'string', 'number', 'boolean' ].indexOf(typeof item) !== -1;
		});

		return values.length ? values.map(function(item) {
			return display(item);
		}).join(', ') : (fallback != null ? fallback : '—');
	}

	if (typeof value === 'string' || typeof value === 'number')
		return String(value);

	return fallback != null ? fallback : '—';
}

function displayBoolean(value) {
	return typeof value === 'boolean' ? display(value) : '—';
}

function displayUnsigned(value) {
	if (typeof value === 'number')
		return Number.isSafeInteger(value) && value >= 0 ? String(value) : '—';

	if (typeof value === 'string' && /^(?:0|[1-9][0-9]*)$/.test(value))
		return value;

	return '—';
}

function responseError(result) {
	if (result == null)
		return _('No response was received from the Fibocom bridge.');

	if (result instanceof Error)
		return display(result.message, _('Unknown transport error'));

	if (!isObject(result))
		return null;

	if (result.transport_error != null)
		return display(result.transport_error, _('Unknown transport error'));

	if (result.ok !== false && result.error == null)
		return null;

	if (typeof result.error === 'string')
		return result.error;

	if (isObject(result.error)) {
		const message = display(result.error.message || result.error.code, _('The request failed.'));

		return result.error.code && result.error.message ? '%s (%s)'.format(
			message, display(result.error.code)) : message;
	}

	return _('The request failed.');
}

function stateClass(state) {
	switch (String(state || '').toLowerCase()) {
	case 'available':
	case 'connected':
	case 'enabled':
	case 'fresh':
	case 'home':
	case 'online':
	case 'present':
	case 'ready':
	case 'registered':
	case 'supported':
	case 'up':
		return 'label success';

	case 'absent':
	case 'denied':
	case 'error':
	case 'failed':
	case 'missing':
	case 'unsupported':
		return 'label danger';

	case 'busy':
	case 'degraded':
	case 'disconnected':
	case 'roaming':
	case 'searching':
	case 'stale':
	case 'unavailable':
	case 'unknown':
		return 'label warning';

	default:
		return 'label';
	}
}

function badge(label, state) {
	return E('span', { 'class': stateClass(state != null ? state : label) }, [ display(label) ]);
}

function renderCell(cell) {
	if (cell != null && typeof cell === 'object' && cell.nodeType != null)
		return cell;

	return display(cell);
}

function table(headers, rows, emptyMessage) {
	const width = Math.max(headers.length, 1);
	const children = [
		E('tr', { 'class': 'tr table-titles' }, headers.map(function(header) {
			return E('th', { 'class': 'th' }, [ header ]);
		}))
	];

	if (!rows.length) {
		children.push(E('tr', { 'class': 'tr placeholder' }, [
			E('td', { 'class': 'td', 'colspan': width }, [
				E('em', {}, [ emptyMessage || _('No information is available.') ])
			])
		]));
	}
	else {
		rows.forEach(function(row) {
			children.push(E('tr', { 'class': 'tr' }, row.map(function(cell) {
				return E('td', { 'class': 'td' }, [ renderCell(cell) ]);
			})));
		});
	}

	return E('table', { 'class': 'table' }, children);
}

function keyValueTable(rows, emptyMessage) {
	return table([ _('Property'), _('Value') ], rows.filter(function(row) {
		return row.length > 1 && row[1] != null && row[1] !== '';
	}), emptyMessage);
}

function modems(result) {
	return isObject(result) && Array.isArray(result.modems) ? result.modems : [];
}

function dependencyRows(result) {
	const dependencies = object(object(result).dependencies);

	return [ 'modemmanager', 'netifd_proto', 'fibocom_plugin', 'mbim' ].filter(function(name) {
		return dependencies[name] != null;
	}).map(function(name) {
		const dependency = dependencies[name];
		const details = object(dependency);
		const state = isObject(dependency) ?
			(details.state || (details.available === true ? 'available' :
				(details.available === false ? 'unavailable' : 'unknown'))) : dependency;

		return [
			name.replace(/_/g, ' '),
			badge(display(state), state),
			display(details.version),
			display(details.reason)
		];
	});
}

function capabilityRows(result) {
	const capabilities = object(object(result).capabilities);

	return Object.keys(capabilities).sort().filter(function(name) {
		return isObject(capabilities[name]);
	}).map(function(name) {
		const capability = capabilities[name];
		const state = capability.state || (capability.available === true ? 'available' :
			(capability.available === false ? 'unavailable' : 'unknown'));

		return [
			E('code', {}, [ name ]),
			badge(display(state), state),
			display(capability.mutable),
			display(capability.reason)
		];
	});
}

function portRows(result) {
	const ports = Array.isArray(result) ? result : object(result).ports;

	if (!Array.isArray(ports))
		return [];

	return ports.filter(isObject).map(function(port) {
		return [
			display(port.role),
			E('code', {}, [ display(port.name || port.port) ]),
			display(port.type),
			port.primary == null ? '—' : display(port.primary)
		];
	});
}

function simSlotRows(result) {
	const slots = object(result).slots;

	if (!Array.isArray(slots))
		return [];

	return slots.filter(isObject).map(function(slot, index) {
		const number = slot.slot != null ? slot.slot : slot.number;

		return [
			display(number, index + 1),
			badge(display(slot.present, _('Unknown')), slot.present === true ? 'present' :
				(slot.present === false ? 'absent' : 'unknown')),
			displayBoolean(slot.primary)
		];
	});
}

function signalRows(result) {
	const signal = object(result);
	const technologies = [ 'gsm', 'umts', 'lte', 'nr5g', 'cdma1x', 'evdo' ];
	const metrics = [ 'rssi', 'rscp', 'ecio', 'rsrp', 'rsrq', 'snr', 'sinr', 'error_rate' ];
	const rows = [];

	technologies.forEach(function(technology) {
		const values = object(signal[technology]);

		metrics.forEach(function(metric) {
			if (values[metric] != null)
				rows.push([ technology.toUpperCase(), metric.toUpperCase(), values[metric] ]);
		});
	});

	return rows;
}

function cellRows(result) {
	const cell = object(result);
	const cells = Array.isArray(cell.cells) ? cell.cells : [];

	return cells.filter(isObject).map(function(entry) {
		return [
			display(entry.serving === true ? _('Serving') : entry.type),
			display(entry.operator || entry.operator_code),
			display(entry.tac || entry.lac),
			display(entry.cid || entry.cell_id),
			display(entry.physical_cell_id || entry.pci),
			display(entry.earfcn || entry.frequency),
			display(entry.rsrp),
			display(entry.rsrq)
		];
	});
}

function bearerRows(result) {
	const bearers = Array.isArray(result) ? result : [];

	return bearers.filter(isObject).map(function(bearer) {
		const ipv4 = object(bearer.ipv4);
		const ipv6 = object(bearer.ipv6);
		const stats = object(bearer.stats);
		const inferredFamilies = [];

		if (Object.keys(ipv4).length)
			inferredFamilies.push('ipv4');
		if (Object.keys(ipv6).length)
			inferredFamilies.push('ipv6');
		const addresses = bearer.addresses || [ ipv4.address, ipv6.address ].filter(function(value) {
			return value != null && value !== '';
		});
		const gateways = [ ipv4.gateway, ipv6.gateway ].filter(function(value) {
			return value != null && value !== '';
		});
		const dns = bearer.dns || [].concat(ipv4.dns || [], ipv6.dns || []);
		const mtu = bearer.mtu != null ? bearer.mtu :
			(ipv4.mtu != null ? ipv4.mtu : ipv6.mtu);

		return [
			badge(display(bearer.connected, _('Unknown')), bearer.connected === true ? 'connected' :
				(bearer.connected === false ? 'disconnected' : 'unknown')),
			displayBoolean(bearer.suspended),
			displayBoolean(bearer.multiplexed),
			E('code', {}, [ display(bearer.interface) ]),
			display(bearer.ip_families || bearer.ip_family || inferredFamilies),
			display(addresses),
			display(gateways),
			display(dns),
			display(mtu),
			displayUnsigned(stats.duration),
			displayUnsigned(stats.rx_bytes),
			displayUnsigned(stats.tx_bytes)
		];
	});
}

function progress(value) {
	const quality = Math.max(0, Math.min(100, Number(value)));

	if (!Number.isFinite(quality))
		return display(value);

	return E('div', {
		'class': 'cbi-progressbar',
		'title': _('%d%% signal quality').format(quality)
	}, [ E('div', { 'style': 'width: %d%%'.format(quality) }) ]);
}

function warningList(warnings) {
	if (!Array.isArray(warnings))
		return null;

	const entries = warnings.filter(function(warning) {
		return typeof warning === 'string' || typeof warning === 'number';
	});

	return entries.length ? E('div', { 'class': 'alert-message warning' }, [
		E('strong', {}, [ _('Modem warning') ]),
		E('ul', {}, entries.map(function(warning) {
			return E('li', {}, [ display(warning) ]);
		}))
	]) : null;
}

function errorPanel(error) {
	return E('div', { 'class': 'alert-message danger' }, [
		E('strong', {}, [ _('Unable to load Fibocom modem information:') ]),
		' ',
		display(responseError(error) || error, _('Unknown error'))
	]);
}

function schemaNotice(result) {
	if (!isObject(result) || result.schema == null || Number(result.schema) === 1)
		return null;

	return E('div', { 'class': 'alert-message warning' }, [
		_('The bridge returned API schema %s, while this interface supports schema 1.')
			.format(display(result.schema))
	]);
}

return baseclass.extend({
	badge: badge,
	bearerRows: bearerRows,
	capabilityRows: capabilityRows,
	cellRows: cellRows,
	dependencyRows: dependencyRows,
	display: display,
	errorPanel: errorPanel,
	isObject: isObject,
	keyValueTable: keyValueTable,
	modems: modems,
	object: object,
	portRows: portRows,
	progress: progress,
	responseError: responseError,
	schemaNotice: schemaNotice,
	signalRows: signalRows,
	simSlotRows: simSlotRows,
	table: table,
	warningList: warningList
});
