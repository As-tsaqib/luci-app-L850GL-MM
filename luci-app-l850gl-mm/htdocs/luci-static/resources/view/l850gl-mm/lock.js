// SPDX-FileCopyrightText: 2026 As Tsaqib
// SPDX-License-Identifier: Apache-2.0
/* global api, ui */

'use strict';
'require dom';
'require poll';
'require ui';
'require view';
'require l850gl-mm.api as api';
'require l850gl-mm.widgets as widgets';

const deviceStates = Object.create(null);

function transportResult(error) {
	return {
		transport_error: widgets.display(error && error.message, _('RPC transport failure'))
	};
}

function stateFor(modemId) {
	if (!Object.prototype.hasOwnProperty.call(deviceStates, modemId)) {
		deviceStates[modemId] = {
			generation: null,
			modeAllowed: '3g|4g',
			modePreferred: 'none',
			modeDirty: false,
			bandAutomatic: false,
			selectedBands: Object.create(null),
			bandDirty: false,
			earfcn: '',
			pci: '',
			busy: null,
			scan: null,
			result: null
		};
	}
	return deviceStates[modemId];
}

function pruneDeviceStates(summaries) {
	const present = Object.create(null);

	summaries.forEach(function(summary) { present[summary.modem_id] = true; });
	Object.keys(deviceStates).forEach(function(modemId) {
		if (!present[modemId])
			delete deviceStates[modemId];
	});
}

function loadExpertStatus(summary, lock) {
	if (widgets.lockError(lock, summary) || lock.pci_lock.state === 'unsupported_build')
		return Promise.resolve(null);
	return api.cellLockStatus(summary.modem_id, summary.generation).catch(transportResult);
}

function loadSnapshots() {
	return api.listModems().then(function(listResult) {
		if (widgets.listError(listResult))
			return { list: listResult, entries: [] };
		const summaries = widgets.modems(listResult);

		pruneDeviceStates(summaries);
		return Promise.all(summaries.map(function(summary) {
			return api.getLockStatus(summary.modem_id).catch(transportResult).then(function(lock) {
				return loadExpertStatus(summary, lock).then(function(expert) {
					return { summary: summary, lock: lock, expert: expert };
				});
			});
		})).then(function(entries) {
			return { list: listResult, entries: entries };
		});
	}).catch(function(error) {
		return { list: transportResult(error), entries: [] };
	});
}

function canonicalBands(values, includeAny) {
	const seen = Object.create(null);
	const output = [];

	if (!Array.isArray(values))
		return output;
	values.forEach(function(value) {
		if (typeof value !== 'string' || !/^[a-z0-9][a-z0-9-]{0,47}$/.test(value) ||
		    (!includeAny && value === 'any') || seen[value])
			return;
		seen[value] = true;
		output.push(value);
	});
	return output;
}

function synchronizeState(entry, state) {
	const lock = widgets.object(entry.lock);
	const generationChanged = state.generation !== lock.generation;

	if (generationChanged) {
		state.generation = lock.generation;
		state.modeDirty = false;
		state.bandDirty = false;
		state.busy = null;
		state.scan = null;
		state.result = null;
	}
	if (!state.modeDirty && !state.busy) {
		const policy = widgets.object(lock.mode_policy);

		state.modeAllowed = [ '3g', '4g', '3g|4g' ].indexOf(policy.allowed) !== -1 ?
			policy.allowed : '3g|4g';
		state.modePreferred = [ 'none', '3g', '4g' ].indexOf(policy.preferred) !== -1 ?
			policy.preferred : 'none';
		if (state.modeAllowed !== '3g|4g')
			state.modePreferred = 'none';
	}
	if (!state.bandDirty && !state.busy) {
		const supported = canonicalBands(lock.supported_bands, false);
		const current = canonicalBands(lock.current_bands, true);
		const supportedSet = Object.create(null);

		supported.forEach(function(band) { supportedSet[band] = true; });
		state.bandAutomatic = lock.band_selection === 'automatic' ||
			current.indexOf('any') !== -1;
		state.selectedBands = Object.create(null);
		current.forEach(function(band) {
			if (supportedSet[band])
				state.selectedBands[band] = true;
		});
	}
}

function modeMutationContext(entry) {
	const summary = widgets.object(entry.summary);
	const lock = widgets.object(entry.lock);
	const capability = widgets.object(lock.mode_policy);

	if (widgets.lockError(lock, summary) || capability.state !== 'available' ||
	    capability.mutable !== true ||
	    !widgets.mutationAllowed(lock, lock, summary.modem_id, summary.generation))
		return null;
	return { modemId: summary.modem_id, generation: summary.generation };
}

function statusPanel(result) {
	return result ? E('div', { 'class': 'alert-message ' + result.kind }, [ result.message ]) : null;
}

function bandMutationContext(entry) {
	const summary = widgets.object(entry.summary);
	const lock = widgets.object(entry.lock);
	const capability = widgets.object(lock.band_lock);

	if (widgets.lockError(lock, summary) || capability.state !== 'available' ||
	    capability.mutable !== true ||
	    !widgets.mutationAllowed(lock, lock, summary.modem_id, summary.generation))
		return null;
	return { modemId: summary.modem_id, generation: summary.generation };
}

function expertStatus(entry) {
	const summary = widgets.object(entry.summary);
	const lock = widgets.object(entry.lock);
	const fallback = widgets.object(lock.pci_lock);
	const expert = widgets.object(entry.expert);
	const scan = widgets.object(expert.scan);

	if (fallback.state === 'unsupported_build')
		return { state: 'unsupported_build', mutable: false, reason: fallback.reason };
	if (widgets.responseError(entry.expert))
		return { state: fallback.state || 'unavailable', mutable: false, reason: fallback.reason };
	if (!widgets.mutationAllowed(expert, expert, summary.modem_id, summary.generation) ||
	    typeof expert.state !== 'string' || expert.state.length > 64 ||
	    typeof expert.mutable !== 'boolean' || typeof expert.reason !== 'string' ||
	    expert.reason.length > 160 || typeof scan.state !== 'string' ||
	    scan.state.length > 64 || typeof scan.available !== 'boolean' ||
	    typeof scan.reason !== 'string' || scan.reason.length > 160 ||
	    scan.source !== 'modemmanager' ||
	    (scan.retry_after_ms != null &&
		(!Number.isSafeInteger(scan.retry_after_ms) || scan.retry_after_ms < 0)))
		return { state: 'unavailable', mutable: false, reason: 'malformed-expert-response' };
	if ([ 'available', 'busy' ].indexOf(expert.state) !== -1 &&
	    lockObservation(expert) === null)
		return { state: 'unavailable', mutable: false, reason: 'malformed-lock-observation' };
	return expert;
}

function replacementIdentityIsValid(result) {
	return widgets.isObject(result) && result.replacement === true &&
		/^[0-9a-f]{32}$/.test(result.modem_id) &&
		Number.isSafeInteger(result.generation) && result.generation > 0;
}

function lockObservation(status) {
	const lock = widgets.object(status.lock);

	if ([ 'clear', 'configured_exact', 'configured_earfcn' ].indexOf(lock.state) === -1 ||
	    typeof lock.enabled !== 'boolean' || lock.postcondition_verified !== false ||
	    lock.source !== 'l850-nvm-via-modemmanager')
		return null;
	if (lock.enabled && (!Number.isSafeInteger(lock.earfcn) || lock.earfcn < 0 ||
	    !Number.isSafeInteger(lock.band) || lock.band < 1 || lock.band > 85))
		return null;
	if (lock.state === 'configured_exact' &&
	    (!Number.isSafeInteger(lock.pci) || lock.pci < 0 || lock.pci > 503))
		return null;
	if (lock.state !== 'configured_exact' && lock.pci != null)
		return null;
	return lock;
}

function expertMutationContext(entry) {
	const summary = widgets.object(entry.summary);
	const expert = expertStatus(entry);

	if (expert.mutable !== true || [ 'available', 'scan_ready' ].indexOf(expert.state) === -1)
		return null;
	return { modemId: summary.modem_id, generation: summary.generation };
}

function expertScanContext(entry) {
	const summary = widgets.object(entry.summary);
	const expert = expertStatus(entry);
	const scan = widgets.object(expert.scan);

	if (scan.available !== true || scan.state !== 'available' ||
	    scan.source !== 'modemmanager')
		return null;
	return { modemId: summary.modem_id, generation: summary.generation };
}

function scanStatusIndicator(scan, state, canScan) {
	let status = 'unavailable';
	let label = _('Cell scan unavailable');

	if (state.busy === 'scan' || scan.state === 'busy') {
		status = 'loading';
		label = _('Cell scan in progress…');
	} else if (state.busy || scan.state === 'rate_limited' ||
		   scan.state === 'not_ready' || scan.retry_after_ms > 0) {
		status = 'limited';
		label = _('Cell scan temporarily unavailable');
	} else if (canScan) {
		status = 'ready';
		label = _('Cell scan available');
	}
	return E('span', {
		'class': 'l850gl-mm-scan-status-dot is-' + status,
		'role': 'status',
		'aria-live': 'polite',
		'aria-label': label,
		'title': label
	}, []);
}

function currentEntry(controller, modemId) {
	const entries = widgets.object(controller.snapshot).entries;

	return Array.isArray(entries) ? entries.find(function(entry) {
		return widgets.object(entry.summary).modem_id === modemId;
	}) : null;
}

function mutationFailureMessage(result, operation) {
	const error = widgets.object(widgets.object(result).error);
	const details = widgets.responseError(result) || _('The request failed.');

	if (error.code === 'outcome_unknown') {
		return {
			kind: 'warning',
			message: _('%s may have completed, but its outcome is unknown. Refresh and do not retry until the live modem state confirms that a new request is safe.')
				.format(operation)
		};
	}
	if ([ 'stale_generation', 'stale_identity', 'device_gone', 'not_found' ]
		.indexOf(error.code) !== -1) {
		return {
			kind: 'warning',
			message: _('The modem identity or generation changed during %s. Review the refreshed modem before trying again.')
				.format(operation)
		};
	}
	return {
		kind: error.code === 'busy' ? 'warning' : 'danger',
		message: _('%s failed: %s').format(operation, details)
	};
}

function performBandMutation(controller, entry, state, bands) {
	const displayed = bandMutationContext(entry);
	const latest = currentEntry(controller, entry.summary.modem_id);
	const context = latest && bandMutationContext(latest);

	if (state.busy)
		return Promise.resolve();
	if (!displayed || !context || displayed.generation !== context.generation) {
		state.result = {
			kind: 'warning',
			message: _('The modem generation or band capability changed. Refresh before applying bands.')
		};
		controller.redraw();
		return controller.refresh(true);
	}
	state.busy = 'bands';
	state.result = { kind: 'notice', message: _('Band change in progress…') };
	controller.redraw();
	return api.setBands(context.modemId, context.generation, bands, true).then(function(result) {
		let error = widgets.responseError(result);

		if (!error && (!widgets.mutationAllowed(result, result,
			context.modemId, context.generation) || result.accepted !== true))
			error = _('The bridge returned an incomplete band mutation response.');
		state.busy = null;
		if (error)
			state.result = mutationFailureMessage(result, _('Band change'));
		else {
			state.bandDirty = false;
			state.result = { kind: 'success', message: _('Band change accepted.') };
		}
		return controller.refresh(true);
	}).catch(function(error) {
		state.busy = null;
		state.result = {
			kind: 'warning',
			message: _('The band-change outcome is unknown because RPC failed: %s. Refresh and do not retry until the live modem state confirms the result.')
				.format(widgets.display(error && error.message, _('Unknown transport error')))
		};
		return controller.refresh(true);
	});
}

function performModeMutation(controller, entry, state) {
	const displayed = modeMutationContext(entry);
	const latest = currentEntry(controller, entry.summary.modem_id);
	const context = latest && modeMutationContext(latest);

	if (state.busy)
		return Promise.resolve();
	if (!displayed || !context || displayed.generation !== context.generation) {
		state.result = {
			kind: 'warning',
			message: _('The modem generation or mode-policy capability changed. Refresh before applying modes.')
		};
		controller.redraw();
		return controller.refresh(true);
	}
	state.busy = 'modes';
	state.result = { kind: 'notice', message: _('Mode policy change in progress…') };
	controller.redraw();
	return api.setModes(context.modemId, context.generation,
		state.modeAllowed, state.modePreferred, true).then(function(result) {
		let error = widgets.responseError(result);

		if (!error && (!widgets.mutationAllowed(result, result,
			context.modemId, context.generation) || result.accepted !== true ||
		    result.operation !== 'set_modes' || result.persisted !== true ||
		    [ 'reloaded', 'pending', 'failed', 'outcome_unknown' ]
			.indexOf(result.activation) === -1))
			error = _('The bridge returned an incomplete mode-policy mutation response.');
		state.busy = null;
		if (error)
			state.result = mutationFailureMessage(result, _('Mode policy change'));
		else {
			state.modeDirty = false;
			if (result.activation === 'reloaded')
				state.result = { kind: 'success', message: _('Mode policy saved and netifd reload completed.') };
			else if (result.activation === 'pending')
				state.result = { kind: 'warning', message: _('Mode policy was saved, but netifd reload is pending.') };
			else
				state.result = { kind: 'warning', message: _('Mode policy was saved, but the netifd activation result is not confirmed. Refresh before retrying.') };
		}
		return controller.refresh(true);
	}).catch(function(error) {
		state.busy = null;
		state.result = {
			kind: 'warning',
			message: _('The mode-policy response was interrupted during network reload: %s. Refresh before retrying because the persistent intent may already be saved.')
				.format(widgets.display(error && error.message, _('Unknown transport error')))
		};
		return controller.refresh(true);
	});
}

function confirmMutation(title, warning, actionLabel, action) {
	ui.showModal(title, [
		E('div', { 'class': 'alert-message warning' }, [ warning ]),
		E('p', {}, [
			_('The request is bound to the currently displayed opaque modem identity and generation.')
		]),
		E('div', { 'class': 'right' }, [
			E('button', { 'class': 'btn', 'type': 'button', 'click': ui.hideModal }, [ _('Cancel') ]),
			' ',
			E('button', {
				'class': 'btn cbi-button-action important',
				'type': 'button',
				'click': function() { ui.hideModal(); return action(); }
			}, [ actionLabel ])
		])
	]);
}

function modeChoices(controller, state, index, name, choices, selected,
	canMutate, onChange, additionallyDisabled) {
	return E('div', { 'class': 'l850gl-mm-mode-choices' }, choices.map(function(choice, choiceIndex) {
		const id = 'l850gl-mm-' + name + '-' + index + '-' + choiceIndex;

		return E('label', {
			'for': id,
			'class': 'l850gl-mm-choice'
		}, [
			E('input', {
				'id': id, 'class': 'cbi-input-radio', 'type': 'radio',
				'name': 'l850gl-mm-' + name + '-policy-' + index,
				'value': choice.value,
				'checked': selected === choice.value ? '' : null,
				'disabled': !canMutate || state.busy || additionallyDisabled ? '' : null,
				'change': function(event) {
					if (event.target.checked)
						onChange(event.target.value);
				}
			}), choice.label
		]);
	}));
}

function renderModePolicy(controller, entry, state, index) {
	const policy = widgets.object(entry.lock.mode_policy);
	const canMutate = modeMutationContext(entry) !== null;
	const allowedTitleId = 'l850gl-mm-allowed-mode-title-' + index;
	const preferredTitleId = 'l850gl-mm-preferred-mode-title-' + index;
	const allowedChoices = [
		{ value: '3g|4g', label: _('3G / 4G') },
		{ value: '3g', label: _('3G only') },
		{ value: '4g', label: _('4G only') }
	];
	const preferredChoices = [
		{ value: 'none', label: _('No preference') },
		{ value: '3g', label: _('Prefer 3G') },
		{ value: '4g', label: _('Prefer 4G') }
	];
	const children = [
		E('h4', { 'class': 'l850gl-mm-panel-title' }, [ _('Allowed and preferred mode') ])
	];

	if (!canMutate)
		children.push(E('div', { 'class': 'alert-message notice' }, [
			_('Persistent mode selection is unavailable: %s').format(
				widgets.display(policy.reason, _('unknown reason')))
		]));
	children.push(E('div', { 'class': 'cbi-value l850gl-mm-form-row' }, [
		E('div', {
			'id': allowedTitleId,
			'class': 'cbi-value-title'
		}, [ _('Allowed mode') ]),
		E('div', {
			'class': 'cbi-value-field',
			'role': 'group',
			'aria-labelledby': allowedTitleId
		}, [
			modeChoices(controller, state, index, 'allowed-mode',
				allowedChoices, state.modeAllowed, canMutate, function(value) {
					state.modeAllowed = value;
					if (value !== '3g|4g')
						state.modePreferred = 'none';
					state.modeDirty = true;
					controller.redraw();
				}, false)
		])
	]));
	children.push(E('div', { 'class': 'cbi-value l850gl-mm-form-row' }, [
		E('div', {
			'id': preferredTitleId,
			'class': 'cbi-value-title'
		}, [ _('Preferred mode') ]),
		E('div', {
			'class': 'cbi-value-field',
			'role': 'group',
			'aria-labelledby': preferredTitleId
		}, [
			modeChoices(controller, state, index, 'preferred-mode',
				preferredChoices, state.modePreferred, canMutate, function(value) {
					state.modePreferred = value;
					state.modeDirty = true;
					controller.redraw();
				}, state.modeAllowed !== '3g|4g')
		])
	]));
	children.push(E('div', {
		'class': 'cbi-page-actions l850gl-mm-actions l850gl-mm-mode-actions'
	}, [
		E('button', {
			'class': 'btn cbi-button cbi-button-action', 'type': 'button',
			'disabled': !canMutate || state.busy || !state.modeDirty ? '' : null,
			'click': function() {
				confirmMutation(_('Apply mode selection'),
					_('Changing allowed or preferred modes is persistent and reloads the netifd network configuration. Mobile WAN may disconnect while the modem re-registers. Keep an alternate management path available.'),
					_('Apply modes'), function() {
						return performModeMutation(controller, entry, state);
					});
			}
		}, [ state.busy === 'modes' ? _('Applying…') : _('Apply mode selection') ])
	]));
	return E('div', {
		'class': 'cbi-section l850gl-mm-panel l850gl-mm-mode-policy'
	}, children);
}

function bandLabel(band) {
	const match = /^(?:eutran|utran)-([0-9]+)$/.exec(band);

	return match ? 'B' + String(Number(match[1])) :
		(band === 'any' ? _('Automatic') : band);
}

function groupedBands(bands) {
	const groups = [
		{ key: 'utran', title: '3G', entries: [] },
		{ key: 'eutran', title: '4G', entries: [] },
		{ key: 'other', title: _('Other bands'), entries: [] }
	];

	(bands || []).forEach(function(band) {
		const match = /^(utran|eutran)-([0-9]+)$/.exec(band);
		const group = match ? groups[match[1] === 'utran' ? 0 : 1] : groups[2];

		group.entries.push({
			value: band,
			label: bandLabel(band),
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
	return groups.filter(function(group) { return group.entries.length; });
}

function friendlyBandSummary(bands) {
	if (Array.isArray(bands) && bands.length === 1 && bands[0] === 'any')
		return _('Automatic');
	return groupedBands(bands).map(function(group) {
		return '%s: %s'.format(group.title, group.entries.map(function(entry) {
			return entry.label;
		}).join(', '));
	}).join(' | ');
}

function friendlyBandList(bands) {
	const labels = [];

	groupedBands(bands).forEach(function(group) {
		group.entries.forEach(function(entry) { labels.push(entry.label); });
	});
	return labels.join(', ');
}

function sameBandSet(left, right) {
	if (!Array.isArray(left) || !Array.isArray(right) || left.length !== right.length)
		return false;
	const expected = Object.create(null);

	right.forEach(function(band) { expected[band] = true; });
	return left.every(function(band) { return expected[band] === true; });
}

function currentBandSummary(lock, current, supported) {
	if (lock.band_selection === 'automatic' || current.indexOf('any') !== -1 ||
	    sameBandSet(current, supported))
		return _('Any Supported bands');
	const lte = current.filter(function(band) { return /^eutran-[0-9]+$/.test(band); });

	return lte.length ? friendlyBandList(lte) : friendlyBandSummary(current);
}

function currentBandIsAutomatic(lock, current, supported) {
	return lock.band_selection === 'automatic' || current.indexOf('any') !== -1 ||
		sameBandSet(current, supported);
}

function bandCheckboxGrid(controller, state, index, supported, canMutate) {
	return E('div', { 'class': 'l850gl-mm-band-groups' }, groupedBands(supported).map(function(group) {
		return E('div', {
			'class': 'cbi-section-node l850gl-mm-band-group'
		}, [
			E('div', { 'class': 'l850gl-mm-band-group-title' }, [ group.title ]),
			E('div', { 'class': 'l850gl-mm-band-checkboxes' }, group.entries.map(function(entry) {
				const band = entry.value;
				const bandIndex = supported.indexOf(band);
				const id = 'l850gl-mm-band-' + index + '-' + bandIndex;

				return E('label', {
					'for': id,
					'class': 'l850gl-mm-band-choice'
				}, [
					E('input', {
						'id': id, 'class': 'cbi-input-checkbox', 'type': 'checkbox',
						'checked': state.selectedBands[band] ? '' : null,
						'disabled': !canMutate || state.busy || state.bandAutomatic ? '' : null,
						'change': function(event) {
							state.selectedBands[band] = event.target.checked;
							state.bandDirty = true;
							controller.redraw();
						}
					}), entry.label
				]);
			}))
		]);
	}));
}

function invertBandSelection(controller, state, supported) {
	if (state.busy || state.bandAutomatic)
		return;
	supported.forEach(function(band) {
		state.selectedBands[band] = !state.selectedBands[band];
	});
	state.bandDirty = true;
	controller.redraw();
}

function renderBandLock(controller, entry, state, index) {
	const lock = entry.lock;
	const supported = canonicalBands(lock.supported_bands, false);
	const lockableBands = supported.filter(function(band) {
		return /^eutran-[0-9]+$/.test(band);
	});
	const current = canonicalBands(lock.current_bands, true);
	const selected = Object.keys(state.selectedBands).filter(function(band) {
		return state.selectedBands[band] && lockableBands.indexOf(band) !== -1;
	});
	const canMutate = bandMutationContext(entry) !== null;
	const currentSummary = currentBandSummary(lock, current, supported);
	const currentAutomatic = currentBandIsAutomatic(lock, current, supported);
	const automaticId = 'l850gl-mm-band-any-' + index;
	const explicitId = 'l850gl-mm-band-explicit-' + index;
	const requestedTitleId = 'l850gl-mm-requested-bands-title-' + index;
	const children = [
		E('h4', { 'class': 'l850gl-mm-panel-title' }, [ _('Band Lock') ]),
		widgets.keyValueList([
			[ _('Current bands'), widgets.badge(currentSummary,
				currentAutomatic ? 'available' : 'notice') ]
		])
	];

	if (!canMutate) {
		children.push(E('div', { 'class': 'alert-message notice' }, [
			_('Band mutation is unavailable for this snapshot. The bridge will not use a vendor-command fallback.')
		]));
	}
	children.push(E('div', { 'class': 'cbi-value l850gl-mm-form-row' }, [
		E('div', {
			'id': requestedTitleId,
			'class': 'cbi-value-title'
		}, [ _('Requested bands') ]),
		E('div', {
			'class': 'cbi-value-field',
			'role': 'group',
			'aria-labelledby': requestedTitleId
		}, [
			E('div', { 'class': 'l850gl-mm-choice-list' }, [
				E('label', { 'for': automaticId, 'class': 'l850gl-mm-choice' }, [
					E('input', {
						'id': automaticId, 'name': 'l850gl-mm-band-policy-' + index,
						'class': 'cbi-input-radio', 'type': 'radio',
						'checked': state.bandAutomatic ? '' : null,
						'disabled': !canMutate || state.busy ? '' : null,
						'change': function() {
							state.bandAutomatic = true;
							state.bandDirty = true;
							controller.redraw();
						}
					}), ' ', _('Any supported band (automatic)')
				]),
				E('label', { 'for': explicitId, 'class': 'l850gl-mm-choice' }, [
					E('input', {
						'id': explicitId, 'name': 'l850gl-mm-band-policy-' + index,
						'class': 'cbi-input-radio', 'type': 'radio',
						'checked': !state.bandAutomatic ? '' : null,
						'disabled': !canMutate || state.busy ? '' : null,
						'change': function() {
							state.bandAutomatic = false;
							state.bandDirty = true;
							controller.redraw();
						}
					}), ' ', _('Explicit LTE bands')
				])
			]),
			bandCheckboxGrid(controller, state, index, lockableBands, canMutate)
		])
	]));
	children.push(E('div', {
		'class': 'cbi-page-actions l850gl-mm-actions l850gl-mm-band-actions'
	}, [
		E('button', {
			'class': 'btn cbi-button cbi-button-neutral', 'type': 'button',
			'disabled': !canMutate || state.busy || state.bandAutomatic ||
				!lockableBands.length ? '' : null,
			'click': function() {
				invertBandSelection(controller, state, lockableBands);
			}
		}, [ _('Invert') ]),
		E('button', {
			'class': 'btn cbi-button cbi-button-action', 'type': 'button',
			'disabled': !canMutate || state.busy || !state.bandDirty ||
				(!state.bandAutomatic && !selected.length) ? '' : null,
			'click': function() {
				const bands = state.bandAutomatic ? [ 'any' ] : selected;

				confirmMutation(_('Apply band selection'),
					_('Changing bands can immediately detach the mobile network and interrupt this router’s WAN connection. Keep an alternate management path available.'),
					_('Apply bands'), function() {
						return performBandMutation(controller, entry, state, bands, true);
					});
			}
		}, [ state.busy === 'bands' ? _('Applying…') : _('Apply band selection') ])
	]));
	return E('div', {
		'class': 'cbi-section l850gl-mm-panel l850gl-mm-band-lock'
	}, children);
}

function validateCellInput(state) {
	if (!/^(?:0|[1-9][0-9]{0,4})$/.test(state.earfcn) ||
	    Number(state.earfcn) > 70545)
		return _('EARFCN must be an integer from 0 through 70545.');
	if (state.pci !== '' && !/^(?:0|[1-9][0-9]{0,2})$/.test(state.pci))
		return _('PCI must be empty or an integer from 0 through 503.');
	if (state.pci !== '' && Number(state.pci) > 503)
		return _('PCI must be empty or an integer from 0 through 503.');
	return null;
}

function validateExpertResult(result, context, requireAccepted) {
	const error = widgets.responseError(result);
	const verification = widgets.object(widgets.object(result).verification);

	if (error)
		return error;
	if (!requireAccepted &&
	    (!widgets.mutationAllowed(result, result, context.modemId, context.generation) ||
	     typeof result.state !== 'string'))
		return _('The expert bridge returned an incomplete schema 4 response.');
	if (requireAccepted && (!replacementIdentityIsValid(result) || result.accepted !== true ||
	    [ 'applied_verified', 'cleared_verified' ].indexOf(result.state) === -1 ||
	    verification.registration !== true || verification.nvm !== true ||
	    (result.state === 'applied_verified' &&
	     (verification.serving_cell !== true ||
	      !Number.isSafeInteger(verification.earfcn) || verification.earfcn < 0 ||
	      !Number.isSafeInteger(verification.pci) || verification.pci < 0 ||
	      verification.pci > 503))))
		return _('The expert bridge returned an incomplete schema 4 response.');
	return null;
}

function performExpertMutation(controller, entry, state, operation, invoke) {
	const displayed = expertMutationContext(entry);
	const latest = currentEntry(controller, entry.summary.modem_id);
	const context = latest && expertMutationContext(latest);

	if (state.busy || !displayed || !context || displayed.generation !== context.generation) {
		state.result = {
			kind: 'warning',
			message: _('The expert capability or modem generation is not current. Refresh before applying a cell lock.')
		};
		controller.redraw();
		return Promise.resolve();
	}
	state.busy = operation;
	controller.result = {
		kind: 'notice',
		message: _('Cell-lock reset, reprobe, registration, and verification are in progress…')
	};
	controller.redraw();
	return invoke(context).then(function(result) {
		const error = validateExpertResult(result, context, true);

		state.busy = null;
		controller.result = error ? mutationFailureMessage(result, operation) : {
			kind: 'success',
			message: result.state === 'cleared_verified' ?
				_('The cell lock was cleared and its post-reset NVM state was verified.') :
				_('The cell lock was applied and verified against post-reset NVM and serving-cell state.')
		};
		return controller.refresh(true);
	}).catch(function(error) {
		state.busy = null;
		controller.result = {
			kind: 'warning',
			message: _('The expert mutation outcome is unknown: %s. Refresh and do not retry until the live state is known.')
				.format(widgets.display(error && error.message, _('Unknown transport error')))
		};
		return controller.refresh(true);
	});
}

function cellResultIsValid(cell) {
	return widgets.isObject(cell) && (cell.type === 4 || cell.type === 5) &&
		typeof cell.serving === 'boolean' && cell.serving === (cell.type === 4) &&
		Number.isSafeInteger(cell.earfcn) && cell.earfcn >= 0 && cell.earfcn <= 70545 &&
		Number.isSafeInteger(cell.pci) && cell.pci >= 0 && cell.pci <= 503 &&
		Number.isSafeInteger(cell.band) && cell.band > 0 && cell.band <= 85 &&
		(cell.rsrp == null || (typeof cell.rsrp === 'number' && Number.isFinite(cell.rsrp))) &&
		(cell.rsrq == null || (typeof cell.rsrq === 'number' && Number.isFinite(cell.rsrq)));
}

function lteBandLabel(band) {
	return Number.isSafeInteger(band) && band > 0 ? 'B' + String(band) : band;
}

function cellMetric(value, unit) {
	return value == null ? widgets.display(value) : '%s %s'.format(value, unit);
}

function cellLockStatusValue(observed) {
	if (observed.state === 'clear' && observed.enabled === false) {
		return E('div', { 'class': 'l850gl-mm-lock-status is-unlocked' }, [
			E('span', { 'class': 'l850gl-mm-cell-status-title' }, [ _('Lock status') ]),
			E('span', {
				'class': 'l850gl-mm-cell-lock-state is-unlocked'
			}, [ _('UNLOCK') ])
		]);
	}
	if (observed.enabled === true) {
		return E('div', { 'class': 'l850gl-mm-lock-status is-locked' }, [
			E('span', {
				'class': 'l850gl-mm-cell-lock-state is-locked'
			}, [ _('LOCK') ]),
			E('div', {
				'class': 'cbi-section-node l850gl-mm-lock-status-detail'
			}, [
				E('span', {}, [
					_('EARFCN'), ' ',
					E('span', { 'class': 'l850gl-mm-lock-status-number' }, [
						String(observed.earfcn)
					])
				]),
				E('span', {}, [
					_('PCI'), ' ',
					E('span', { 'class': 'l850gl-mm-lock-status-number' }, [
						observed.pci == null ? 'any' : String(observed.pci)
					])
				])
			])
		]);
	}
	return E('div', { 'class': 'l850gl-mm-lock-status' }, [
		E('span', { 'class': 'l850gl-mm-cell-status-title' }, [ _('Lock status') ]),
		E('span', {
			'class': 'l850gl-mm-cell-lock-state is-unavailable'
		}, [ _('Unavailable') ])
	]);
}

function selectScanCell(controller, state, cell) {
	if (state.busy)
		return;
	if (!cellResultIsValid(cell)) {
		state.result = {
			kind: 'warning',
			message: _('The selected scan result is malformed and was not copied.')
		};
		controller.redraw();
		return;
	}
	state.earfcn = String(cell.earfcn);
	state.pci = String(cell.pci);
	state.result = {
		kind: 'notice',
		message: _('Selected cell (EARFCN %s, PCI %s). Review the values before applying the lock.')
			.format(cell.earfcn, cell.pci)
	};
	controller.redraw();
}

function scanCardField(label, value) {
	return E('span', { 'class': 'l850gl-mm-cell-card-field' }, [
		E('span', { 'class': 'l850gl-mm-cell-card-label' }, [ label ]),
		E('span', { 'class': 'l850gl-mm-cell-card-value' }, [ widgets.display(value) ])
	]);
}

function renderScanResults(controller, state, cells) {
	return E('div', { 'class': 'l850gl-mm-cell-cards' }, cells.map(function(cell) {
		const selected = state.earfcn === String(cell.earfcn) &&
			state.pci === String(cell.pci);

		return E('button', {
			'class': 'cbi-section-node l850gl-mm-cell-card',
			'type': 'button',
			'aria-pressed': selected ? 'true' : 'false',
			'title': _('Tap line to use'),
			'disabled': state.busy ? '' : null,
			'click': function() { selectScanCell(controller, state, cell); }
		}, [
			scanCardField(_('Band'), lteBandLabel(cell.band)),
			scanCardField(_('EARFCN'), cell.earfcn),
			scanCardField(_('PCI'), cell.pci),
			scanCardField(_('RSRP'), cellMetric(cell.rsrp, 'dBm')),
			scanCardField(_('RSRQ'), cellMetric(cell.rsrq, 'dB'))
		]);
	}));
}

function performScan(controller, entry, state) {
	const displayed = expertScanContext(entry);
	const latest = currentEntry(controller, entry.summary.modem_id);
	const context = latest && expertScanContext(latest);

	if (state.busy)
		return Promise.resolve();
	if (!displayed || !context || displayed.generation !== context.generation) {
		state.result = {
			kind: 'warning',
			message: _('The cell-scan capability or modem generation changed. Refresh before scanning.')
		};
		controller.redraw();
		return controller.refresh(true);
	}
	state.busy = 'scan';
	state.result = { kind: 'notice', message: _('Cell scan in progress…') };
	controller.redraw();
	return api.cellScan(context.modemId, context.generation).then(function(result) {
		const error = validateExpertResult(result, context, false);

		state.busy = null;
		if (error || result.state !== 'scan_ready' || result.source !== 'modemmanager' ||
		    !Array.isArray(result.cells) || result.cells.length > 64 ||
		    !result.cells.every(cellResultIsValid)) {
			state.scan = null;
			state.result = mutationFailureMessage(result, _('Cell scan'));
		}
		else {
			state.scan = result.cells;
			state.result = { kind: 'success', message: _('Cell scan completed.') };
		}
		controller.redraw();
		return result;
	}).catch(function(error) {
		state.busy = null;
		state.scan = null;
		state.result = {
			kind: 'danger',
			message: _('Cell scan failed: %s').format(
				widgets.display(error && error.message, _('Unknown transport error')))
		};
		controller.redraw();
	});
}

function renderPciLock(controller, entry, state, index) {
	const status = expertStatus(entry);
	const scan = widgets.object(status.scan);
	const canScan = expertScanContext(entry) !== null;
	const canMutate = expertMutationContext(entry) !== null;
	const inputError = validateCellInput(state);
	const cells = Array.isArray(state.scan) ? state.scan : [];
	const observed = lockObservation(status) || {};
	const earfcnId = 'l850gl-mm-earfcn-' + index;
	const pciId = 'l850gl-mm-pci-' + index;
	let applyButton = null;
	function updateApplyButton() {
		if (applyButton)
			applyButton.disabled = !canMutate || Boolean(state.busy) ||
				validateCellInput(state) !== null;
	}
	const children = [
		E('h4', { 'class': 'l850gl-mm-panel-title' }, [ _('PCI/EARFCN Lock') ])
	];

	if ([ 'unsupported_build', 'unsupported_firmware' ].indexOf(status.state) !== -1) {
		children.push(E('div', { 'class': 'alert-message warning' }, [
			status.state === 'unsupported_build' ?
				_('PCI lock is absent from this base build. Install an explicitly reviewed expert build to expose the typed expert object.') :
				_('This firmware is not in the live-validated mutation allowlist. Standard ModemManager cell scan may still be attempted when advertised, but no vendor fallback, lock tuple, or reset sequence will be guessed.')
		]));
	}
	children.push(E('div', {
		'class': 'cbi-page-actions l850gl-mm-actions l850gl-mm-scan-actions'
	}, [
		E('span', { 'class': 'l850gl-mm-scan-control' }, [
			scanStatusIndicator(scan, state, canScan),
			E('button', {
				'class': 'btn cbi-button cbi-button-neutral', 'type': 'button',
				'disabled': !canScan || state.busy ? '' : null,
				'click': function() { return performScan(controller, entry, state); }
			}, [ state.busy === 'scan' ? _('Scanning…') : _('Scan cells') ])
		]),
		E('span', { 'class': 'l850gl-mm-scan-hint' }, [ _('Tap line to use') ])
	]));
	if (cells.length) {
		children.push(renderScanResults(controller, state, cells));
	}
	children.push(E('div', {
		'class': 'l850gl-mm-cell-lock-layout'
	}, [
		E('div', { 'class': 'l850gl-mm-cell-input-grid' }, [
			E('div', { 'class': 'l850gl-mm-cell-input' }, [
				E('label', { 'for': earfcnId }, [ _('EARFCN') ]),
				E('div', {}, [ E('input', {
					'id': earfcnId, 'class': 'cbi-input-text', 'type': 'number', 'min': 0,
					'value': state.earfcn,
					'disabled': !canMutate || state.busy ? '' : null,
					'input': function(event) {
						state.earfcn = event.target.value;
						updateApplyButton();
					}
				}) ])
			]),
			E('div', { 'class': 'l850gl-mm-cell-input' }, [
				E('label', { 'for': pciId }, [ _('PCI (optional)') ]),
				E('div', {}, [ E('input', {
					'id': pciId, 'class': 'cbi-input-text', 'type': 'number',
					'min': 0, 'max': 503, 'value': state.pci,
					'disabled': !canMutate || state.busy ? '' : null,
					'input': function(event) {
						state.pci = event.target.value;
						updateApplyButton();
					}
				}) ])
			])
		]),
		E('div', { 'class': 'cbi-section-node l850gl-mm-cell-status-box' }, [
			E('div', {
				'class': 'l850gl-mm-cell-status-value',
				'role': 'status',
				'aria-live': 'polite'
			}, [ cellLockStatusValue(observed) ])
		])
	]));
	if (inputError && (state.earfcn !== '' || state.pci !== ''))
		children.push(E('div', { 'class': 'alert-message warning' }, [ inputError ]));
	applyButton = E('button', {
		'class': 'btn cbi-button cbi-button-action', 'type': 'button',
		'disabled': !canMutate || state.busy || inputError ? '' : null,
		'click': function() {
			if (!canMutate || state.busy || validateCellInput(state) !== null)
				return;
			const earfcn = Number(state.earfcn);
			const pci = state.pci === '' ? null : Number(state.pci);

			confirmMutation(_('Apply PCI/EARFCN lock'),
				_('Applying a cell lock is disruptive and can interrupt WAN service. Verification requires reset, reprobe, registration, and a matching serving cell.'),
				_('Apply cell lock'), function() {
					return performExpertMutation(controller, entry, state,
						_('Cell lock'), function(context) {
							return api.setCellLock(context.modemId,
								context.generation, earfcn, pci, true);
						});
				});
		}
	}, [ _('Apply cell lock') ]);
	children.push(E('div', {
		'class': 'cbi-page-actions l850gl-mm-actions l850gl-mm-cell-actions'
	}, [
		applyButton,
		E('button', {
			'class': 'btn cbi-button cbi-button-negative', 'type': 'button',
			'disabled': !canMutate || state.busy ? '' : null,
			'click': function() {
				confirmMutation(_('Clear PCI/EARFCN lock'),
					_('Clearing a cell lock is disruptive and requires the same postcondition verification.'),
					_('Clear cell lock'), function() {
						return performExpertMutation(controller, entry, state,
							_('Clear cell lock'), function(context) {
								return api.clearCellLock(context.modemId,
									context.generation, true);
							});
					});
			}
		}, [ _('Clear cell lock') ])
	]));
	return E('div', {
		'class': 'cbi-section l850gl-mm-panel l850gl-mm-pci-lock'
	}, children);
}

function renderDevice(controller, entry, index) {
	const error = widgets.lockError(entry.lock, entry.summary);
	const state = stateFor(entry.summary.modem_id);
	const children = [ E('h3', { 'class': 'l850gl-mm-device-title' }, [
		widgets.activeLabel(entry.summary.model, true)
	]) ];

	if (error) {
		children.push(widgets.errorPanel(error));
		return E('div', { 'class': 'cbi-section l850gl-mm-device' }, children);
	}
	synchronizeState(entry, state);
	const panel = statusPanel(state.result);

	if (panel)
		children.push(panel);
	children.push(renderModePolicy(controller, entry, state, index));
	children.push(renderBandLock(controller, entry, state, index));
	children.push(renderPciLock(controller, entry, state, index));
	return E('div', { 'class': 'cbi-section l850gl-mm-device' }, children);
}

function renderSnapshots(snapshot, controller) {
	const error = widgets.listError(snapshot.list);
	let content;

	if (error)
		content = widgets.errorPanel(error);
	else if (!snapshot.entries.length)
		content = E('div', { 'class': 'alert-message notice' }, [
			_('No L850-GL modem is currently exported by ModemManager.')
		]);
	else
		content = E('div', {}, snapshot.entries.map(function(entry, index) {
			return renderDevice(controller, entry, index);
		}));
	return controller.result ? E('div', {}, [ statusPanel(controller.result), content ]) : content;
}

function editorHasFocus(content) {
	if (typeof document === 'undefined' || !content || typeof content.contains !== 'function')
		return false;

	const active = document.activeElement;
	const tag = active && String(active.tagName || '').toLowerCase();

	return content.contains(active) && [ 'input', 'select', 'textarea' ].indexOf(tag) !== -1;
}

function redrawAfterEditorBlur(controller) {
	if (typeof window === 'undefined' || typeof window.setTimeout !== 'function')
		return;

	window.setTimeout(function() {
		if (controller.redrawPending && !editorHasFocus(controller.content))
			controller.redraw();
	}, 0);
}

return view.extend({
	load: loadSnapshots,

	render: function(snapshot) {
		const controller = {
			snapshot: snapshot,
			content: null,
			result: null,
			refreshEpoch: 0,
			redrawPending: false,
			redraw: function() {
				this.redrawPending = false;
				if (this.content)
					dom.content(this.content, renderSnapshots(this.snapshot, this));
			},
			refresh: function(force) {
				const epoch = ++this.refreshEpoch;

				return loadSnapshots().then(function(next) {
					if (epoch !== controller.refreshEpoch)
						return null;
					controller.snapshot = next;
					if (force || !editorHasFocus(controller.content)) {
						controller.redraw();
					}
					else {
						controller.redrawPending = true;
					}
					return next;
				});
			}
		};

		controller.content = E('div', {
			'id': 'l850gl-mm-lock',
			'focusout': function() {
				redrawAfterEditorBlur(controller);
			}
		}, [
			renderSnapshots(snapshot, controller)
		]);
		poll.add(function() {
			return controller.refresh(false);
		}, 10);
		return E('div', { 'class': 'cbi-map l850gl-mm-page l850gl-mm-lock-page' }, [
			widgets.stylesheet(),
			E('h2', {}, [ _('Lock') ]),
			E('div', { 'class': 'cbi-map-descr' }, [
				_('Band Lock uses ModemManager')
			]),
			controller.content
		]);
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
