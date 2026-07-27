// SPDX-FileCopyrightText: 2026 As Tsaqib
// SPDX-License-Identifier: Apache-2.0

'use strict';
'require baseclass';

const SCHEMA_VERSION = 2;

function isObject(value) {
	return value != null && typeof value === 'object' && !Array.isArray(value);
}

function object(value) {
	return isObject(value) ? value : {};
}

function isUnsigned(value) {
	return Number.isSafeInteger(value) && value >= 0;
}

function isFiniteNumber(value) {
	return typeof value === 'number' && Number.isFinite(value);
}

function isString(value, maximum) {
	return typeof value === 'string' && value.length <= (maximum || 512) &&
		value.indexOf('\0') === -1;
}

function isStringArray(value, maximum) {
	return Array.isArray(value) && value.length <= (maximum || 256) &&
		value.every(function(entry) { return isString(entry, 160); });
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
			return [ 'string', 'number', 'boolean' ].indexOf(typeof item) !== -1;
		});

		return values.length ? values.map(function(item) {
			return display(item);
		}).join(', ') : (fallback != null ? fallback : '—');
	}
	if (typeof value === 'string' || typeof value === 'number')
		return String(value);
	return fallback != null ? fallback : '—';
}

function isCompatible(result) {
	return isObject(result) && result.schema === SCHEMA_VERSION &&
		typeof result.ok === 'boolean';
}

function envelopeError(result) {
	if (!isCompatible(result))
		return _('Incompatible API schema. This interface requires schema 2.');
	if (!isUnsigned(result.generated_at))
		return _('Malformed schema 2 response from the Fibocom bridge.');
	if (result.ok === false) {
		if (!isObject(result.error) || !isString(result.error.code, 64) ||
		    !isString(result.error.message, 256) ||
		    typeof result.error.retryable !== 'boolean')
			return _('Malformed schema 2 error response from the Fibocom bridge.');
		return result.error.message ? '%s (%s)'.format(
			result.error.message, result.error.code) : result.error.code;
	}
	return null;
}

function responseError(result) {
	if (result == null)
		return _('No response was received from the Fibocom bridge.');
	if (result instanceof Error)
		return display(result.message, _('Unknown transport error'));
	if (!isObject(result))
		return _('Malformed response from the Fibocom bridge.');
	if (result.transport_error != null)
		return display(result.transport_error, _('Unknown transport error'));
	return envelopeError(result);
}

function modemSummaryIsValid(summary) {
	return isObject(summary) && isString(summary.modem_id, 80) &&
		isUnsigned(summary.generation) && isString(summary.manufacturer, 160) &&
		isString(summary.model, 160) && isString(summary.revision, 160) &&
		isString(summary.state, 64) && isString(summary.power, 64);
}

function listError(result) {
	const error = responseError(result);

	if (error)
		return error;
	if (!Array.isArray(result.modems) || result.modems.length > 64 ||
	    !result.modems.every(modemSummaryIsValid))
		return _('Malformed modem inventory in the schema 2 response.');
	return null;
}

function identityMatches(result, summary) {
	return isObject(result) && isObject(summary) &&
		result.modem_id === summary.modem_id &&
		result.generation === summary.generation;
}

function capabilityIsValid(capability) {
	return isObject(capability) && isString(capability.state, 64) &&
		typeof capability.mutable === 'boolean' &&
		(capability.reason == null || isString(capability.reason, 160));
}

function overviewError(result, summary) {
	const error = responseError(result);
	const identity = object(result && result.identity);
	const modem = object(result && result.modem);
	const sim = object(result && result.sim);
	const network = object(result && result.network);
	const signal = object(result && result.signal);
	const bearer = object(result && result.bearer);
	const serving = object(result && result.serving_cell);
	const capabilities = object(result && result.capabilities);

	if (error)
		return error;
	if (!identityMatches(result, summary) ||
	    !isString(identity.manufacturer, 160) || !isString(identity.model, 160) ||
	    !isString(identity.revision, 160) || !isString(modem.state, 64) ||
	    !isString(modem.power, 64) || typeof sim.present !== 'boolean' ||
	    !isString(sim.lock, 64) || !isString(network.operator, 160) ||
	    !isString(network.registration, 64) || typeof network.roaming !== 'boolean' ||
	    !isStringArray(network.access, 32) || !isFiniteNumber(signal.quality) ||
	    signal.quality < 0 || signal.quality > 100 || typeof signal.recent !== 'boolean' ||
	    ![ 'rsrp', 'rsrq', 'sinr' ].every(function(name) {
		    return signal[name] == null || isFiniteNumber(signal[name]);
	    }) || typeof bearer.connected !== 'boolean' ||
	    !isString(bearer.interface, 64) || !isStringArray(result.current_bands, 256) ||
	    !isString(serving.state, 64) ||
	    (serving.state === 'available' &&
		(!isUnsigned(serving.earfcn) || !isUnsigned(serving.pci) ||
		 serving.pci > 503)) ||
	    !capabilityIsValid(capabilities.sms) ||
	    !capabilityIsValid(capabilities.band_lock) ||
	    !capabilityIsValid(capabilities.pci_lock) ||
	    !isStringArray(result.warnings, 32))
		return _('Malformed Overview data in the schema 2 response.');
	return null;
}

function lockError(result, summary) {
	const error = responseError(result);
	const modes = object(result && result.current_modes);

	if (error)
		return error;
	if (!identityMatches(result, summary) ||
	    !isStringArray(result.supported_bands, 256) ||
	    !isStringArray(result.current_bands, 256) ||
	    !isString(result.band_selection, 32) || typeof modes.known !== 'boolean' ||
	    !isStringArray(modes.allowed, 8) || !isString(modes.preferred, 32) ||
	    !capabilityIsValid(result.band_lock) || !capabilityIsValid(result.pci_lock))
		return _('Malformed Lock data in the schema 2 response.');
	return null;
}

function smsMessageIsValid(message) {
	if (!isObject(message) || !isString(message.sms_id, 80) ||
	    !isString(message.direction, 32) || !isString(message.state, 32) ||
	    !isString(message.folder, 32) || !isString(message.number, 64) ||
	    !isString(message.text, 16384) || !isString(message.timestamp, 64) ||
	    !isString(message.discharge_timestamp, 64) || !isString(message.storage, 32) ||
	    !isString(message.pdu_type, 64) || typeof message.text_truncated !== 'boolean' ||
	    typeof message.has_binary_data !== 'boolean')
		return false;
	if (message.message_reference != null && !isUnsigned(message.message_reference))
		return false;
	if (message.delivery_state != null &&
	    !isFiniteNumber(message.delivery_state) && !isString(message.delivery_state, 64) &&
	    !(isObject(message.delivery_state) &&
		isString(message.delivery_state.name, 64) &&
		(isUnsigned(message.delivery_state.code) ||
		 isString(message.delivery_state.code, 32))))
		return false;
	return true;
}

function smsError(result, summary, maximumMessages) {
	const error = responseError(result);
	const maximum = maximumMessages || 100;

	if (error)
		return error;
	if (!identityMatches(result, summary) || !isUnsigned(result.messaging_generation) ||
	    !isUnsigned(result.revision) || !isString(result.cache_state, 64) ||
	    !isUnsigned(result.dedupe_capacity) || !isUnsigned(result.dedupe_window_seconds) ||
	    !Array.isArray(result.messages) || result.messages.length > maximum ||
	    !result.messages.every(smsMessageIsValid) ||
	    typeof result.has_more !== 'boolean' || !isString(result.next_cursor, 80) ||
	    (result.has_more && result.next_cursor === ''))
		return _('Malformed SMS data in the schema 2 response.');
	return null;
}

function mutationAllowed(envelope, identity, modemId, generation) {
	return responseError(envelope) == null && envelope.ok === true &&
		isObject(identity) && identity.modem_id === modemId &&
		identity.generation === generation;
}

function modems(result) {
	return listError(result) == null ? result.modems : [];
}

function stateClass(state) {
	switch (String(state || '').toLowerCase()) {
	case 'available':
	case 'applied_verified':
	case 'cleared_verified':
	case 'connected':
	case 'home':
	case 'on':
	case 'ready':
	case 'registered':
		return 'label success';
	case 'failed':
	case 'unsupported_build':
	case 'unsupported_firmware':
	case 'verification_mismatch':
		return 'label danger';
	case 'busy':
	case 'outcome_unknown':
	case 'roaming':
	case 'searching':
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

function table(headers, rows, emptyMessage) {
	const width = Math.max(headers.length, 1);
	const children = [ E('tr', { 'class': 'tr table-titles' }, headers.map(function(header) {
		return E('th', { 'class': 'th' }, [ header ]);
	})) ];

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
				return E('td', { 'class': 'td' }, [
					cell != null && typeof cell === 'object' && cell.nodeType != null ?
						cell : display(cell)
				]);
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
	if (!isStringArray(warnings, 32) || !warnings.length)
		return null;
	return E('div', { 'class': 'alert-message warning' }, [
		E('strong', {}, [ _('Action required') ]),
		E('ul', {}, warnings.map(function(warning) {
			return E('li', {}, [ warning ]);
		}))
	]);
}

function errorPanel(error) {
	return E('div', { 'class': 'alert-message danger' }, [
		E('strong', {}, [ _('Unable to load Fibocom modem information:') ]),
		' ', display(typeof error === 'string' ? error : responseError(error), _('Unknown error'))
	]);
}

function activeLabel(label, active) {
	return display(label) + (active ? ' ' + _('(active)') : '');
}

return baseclass.extend({
	SCHEMA_VERSION: SCHEMA_VERSION,
	activeLabel: activeLabel,
	badge: badge,
	display: display,
	errorPanel: errorPanel,
	identityMatches: identityMatches,
	isCompatible: isCompatible,
	isObject: isObject,
	keyValueTable: keyValueTable,
	listError: listError,
	lockError: lockError,
	modems: modems,
	mutationAllowed: mutationAllowed,
	object: object,
	overviewError: overviewError,
	progress: progress,
	responseError: responseError,
	smsError: smsError,
	table: table,
	warningList: warningList
});
