// SPDX-FileCopyrightText: 2026 As Tsaqib
// SPDX-License-Identifier: Apache-2.0

'use strict';
'require baseclass';

function isObject(value) {
	return value != null && typeof value === 'object' && !Array.isArray(value);
}

function display(value, fallback) {
	if (value == null || value === '')
		return fallback != null ? fallback : '—';

	if (value === true)
		return _('Yes');

	if (value === false)
		return _('No');

	if (Array.isArray(value)) {
		const primitives = value.filter(function(item) {
			return item == null || [ 'string', 'number', 'boolean' ].indexOf(typeof item) !== -1;
		});

		return primitives.length ? primitives.map(function(item) {
			return display(item);
		}).join(', ') : (fallback != null ? fallback : '—');
	}

	if (typeof value === 'string' || typeof value === 'number')
		return String(value);

	return fallback != null ? fallback : '—';
}

function responseError(result) {
	if (result == null)
		return _('No response was received from fibocomd.');

	if (result instanceof Error)
		return display(result.message, _('Unknown transport error'));

	if (!isObject(result))
		return null;

	if (result.transport_error != null)
		return display(result.transport_error, _('Unknown transport error'));

	if (result.error == null)
		return null;

	if (typeof result.error === 'string')
		return result.error;

	if (isObject(result.error))
		return display(result.error.message || result.error.reason || result.error.code,
			_('The request failed.'));

	return _('The request failed.');
}

function stateClass(state) {
	switch (String(state || '').toLowerCase()) {
	case 'available':
	case 'complete':
	case 'connected':
	case 'healthy':
	case 'ok':
	case 'online':
	case 'present':
	case 'ready':
	case 'supported':
		return 'label success';

	case 'absent':
	case 'ambiguous':
	case 'conflict':
	case 'error':
	case 'failed':
	case 'missing':
		return 'label danger';

	case 'degraded':
	case 'incomplete':
	case 'partial':
	case 'pending':
	case 'scanning':
	case 'unknown':
	case 'warning':
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
	const available = rows.filter(function(row) {
		return row.length > 1 && row[1] != null && row[1] !== '';
	});
	const node = table([ _('Property'), _('Value') ], available, emptyMessage);
	const bodyRows = node.querySelectorAll ? node.querySelectorAll('tr.tr:not(.table-titles)') : [];

	for (let i = 0; i < bodyRows.length; i++) {
		const first = bodyRows[i].querySelector ? bodyRows[i].querySelector('td') : null;

		if (first)
			first.style.width = '33%';
	}

	return node;
}

function devices(result) {
	if (Array.isArray(result))
		return result;

	return isObject(result) && Array.isArray(result.devices) ? result.devices : [];
}

function device(result) {
	if (!isObject(result))
		return {};

	return isObject(result.device) ? result.device : result;
}

function diagnostics(result) {
	if (!isObject(result))
		return {};

	return isObject(result.diagnostics) ? result.diagnostics : result;
}

function portRows(result) {
	const record = device(result);
	const topology = isObject(record.topology) ? record.topology : {};
	const ports = record.ports != null ? record.ports : topology.ports;
	const interfaces = Array.isArray(record.interfaces) ? record.interfaces : topology.interfaces;
	const normalized = [];

	if (Array.isArray(interfaces)) {
		interfaces.forEach(function(iface) {
			if (!isObject(iface))
				return;

			[
				[ 'ttys', 'tty' ],
				[ 'wdms', 'wdm' ],
				[ 'netdevs', 'netdev' ]
			].forEach(function(kind) {
				if (!Array.isArray(iface[kind[0]]))
					return;

				iface[kind[0]].forEach(function(port) {
					if (!isObject(port))
						return;

					normalized.push(Object.assign({
						role: iface.role || kind[1],
						interface_number: iface.number,
						driver: iface.driver
					}, port));
				});
			});
		});
	}

	if (!normalized.length && Array.isArray(ports)) {
		ports.forEach(function(port) {
			if (isObject(port))
				normalized.push(port);
		});
	}
	else if (!normalized.length && isObject(ports)) {
		Object.keys(ports).sort().forEach(function(role) {
			const value = ports[role];

			if (Array.isArray(value)) {
				value.forEach(function(port) {
					if (isObject(port))
						normalized.push(Object.assign({ role: role }, port));
					else
						normalized.push({ role: role, node: port });
				});
			}
			else if (isObject(value))
				normalized.push(Object.assign({ role: role }, value));
			else if (value == null || value === '')
				normalized.push({ role: role, present: false });
			else
				normalized.push({ role: role, node: value });
		});
	}

	return normalized.map(function(port) {
		const state = port.present === false ? 'missing' : (port.state || 'present');

		return [
			display(port.role || port.kind || port.type),
			display(port.node || port.name || port.device),
			display(port.interface || port.interface_number || port.ifnum),
			display(port.driver),
			badge(display(port.state || (port.present === false ? _('Missing') : _('Present'))), state)
		];
	});
}

function capabilityRows(result) {
	if (!isObject(result))
		return [];

	const capabilities = isObject(result.capabilities) ? result.capabilities : result;

	return Object.keys(capabilities).filter(function(feature) {
		return feature !== 'schema' && feature !== 'shadow_mode' && feature !== 'device_id' &&
			isObject(capabilities[feature]);
	}).sort().map(function(feature) {
		const capability = capabilities[feature];
		const state = capability.state || (capability.available === true ? 'available' :
			(capability.available === false ? 'unavailable' : 'unknown'));

		return [
			E('code', {}, [ feature ]),
			badge(display(state), state),
			display(capability.reason)
		];
	});
}

function dependencyRows(result) {
	const record = diagnostics(result);
	const dependencies = record.dependencies;
	const normalized = [];

	if (Array.isArray(dependencies)) {
		dependencies.forEach(function(dependency) {
			if (isObject(dependency))
				normalized.push(dependency);
		});
	}
	else if (isObject(dependencies)) {
		Object.keys(dependencies).sort().forEach(function(name) {
			const value = dependencies[name];

			if (isObject(value))
				normalized.push(Object.assign({ name: name }, value));
		});
	}

	return normalized.map(function(dependency) {
		const state = dependency.state || (dependency.available === true ? 'available' :
			(dependency.available === false ? 'missing' : 'unknown'));

		return [
			display(dependency.name || dependency.component),
			badge(display(state), state),
			display(dependency.version),
			display(dependency.reason)
		];
	});
}

function errorPanel(error) {
	return E('div', { 'class': 'alert-message danger' }, [
		E('strong', {}, [ _('Unable to load Fibocom status:') ]),
		' ',
		display(responseError(error) || error, _('Unknown error'))
	]);
}

function schemaNotice(result) {
	if (!isObject(result) || result.schema == null || Number(result.schema) === 1)
		return null;

	return E('div', { 'class': 'alert-message warning' }, [
		_('The daemon returned API schema %s, while this interface supports schema 1.')
			.format(display(result.schema))
	]);
}

return baseclass.extend({
	badge: badge,
	capabilityRows: capabilityRows,
	dependencyRows: dependencyRows,
	device: device,
	devices: devices,
	diagnostics: diagnostics,
	display: display,
	errorPanel: errorPanel,
	keyValueTable: keyValueTable,
	portRows: portRows,
	responseError: responseError,
	schemaNotice: schemaNotice,
	table: table
});
