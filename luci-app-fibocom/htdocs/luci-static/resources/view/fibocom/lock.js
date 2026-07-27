// SPDX-FileCopyrightText: 2026 As Tsaqib
// SPDX-License-Identifier: Apache-2.0
/* global api, ui */

'use strict';
'require dom';
'require poll';
'require ui';
'require view';
'require fibocom.api as api';
'require fibocom.widgets as widgets';

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
		state.bandDirty = false;
		state.busy = null;
		state.scan = null;
		state.result = null;
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

function bandLabel(band) {
	const match = /^eutran-([0-9]+)$/.exec(band);

	return match ? _('%s — LTE band %s').format(band, match[1]) : band;
}

function renderBandLock(controller, entry, state, index) {
	const lock = entry.lock;
	const capability = lock.band_lock;
	const supported = canonicalBands(lock.supported_bands, false);
	const current = canonicalBands(lock.current_bands, true);
	const selected = Object.keys(state.selectedBands).filter(function(band) {
		return state.selectedBands[band] && supported.indexOf(band) !== -1;
	});
	const canMutate = bandMutationContext(entry) !== null;
	const automaticId = 'fibocom-band-any-' + index;
	const explicitId = 'fibocom-band-explicit-' + index;
	const children = [
		E('h4', {}, [ _('Band Lock') ]),
		widgets.keyValueTable([
			[ _('Capability'), widgets.badge(capability.state, capability.state) ],
			[ _('Reason'), capability.reason ],
			[ _('Selection reported by ModemManager'), lock.band_selection ],
			[ _('Current bands'), current ],
			[ _('Supported bands'), supported ],
			[ _('Current allowed mode families'), lock.current_modes.allowed ],
			[ _('Current preferred mode'), lock.current_modes.preferred ]
		])
	];

	if (!canMutate) {
		children.push(E('div', { 'class': 'alert-message notice' }, [
			_('Band mutation is unavailable for this snapshot. The bridge will not use a vendor-command fallback.')
		]));
	}
	children.push(E('div', { 'class': 'cbi-value' }, [
		E('div', { 'class': 'cbi-value-title' }, [ _('Requested bands') ]),
		E('div', { 'class': 'cbi-value-field' }, [
			E('label', { 'for': automaticId }, [
				E('input', {
					'id': automaticId, 'name': 'fibocom-band-policy-' + index,
					'type': 'radio', 'checked': state.bandAutomatic ? '' : null,
					'disabled': !canMutate || state.busy ? '' : null,
					'change': function() {
						state.bandAutomatic = true;
						state.bandDirty = true;
						controller.redraw();
					}
				}), ' ', _('Any supported band (automatic)')
			]),
			E('br'),
			E('label', { 'for': explicitId }, [
				E('input', {
					'id': explicitId, 'name': 'fibocom-band-policy-' + index,
					'type': 'radio', 'checked': !state.bandAutomatic ? '' : null,
					'disabled': !canMutate || state.busy ? '' : null,
					'change': function() {
						state.bandAutomatic = false;
						state.bandDirty = true;
						controller.redraw();
					}
				}), ' ', _('Explicit supported bands')
			]),
			E('div', { 'style': 'margin-left:1.5em;margin-top:.5em' },
				supported.map(function(band, bandIndex) {
					const id = 'fibocom-band-' + index + '-' + bandIndex;
					return E('div', {}, [ E('label', { 'for': id }, [
						E('input', {
							'id': id, 'type': 'checkbox',
							'checked': state.selectedBands[band] ? '' : null,
							'disabled': !canMutate || state.busy || state.bandAutomatic ? '' : null,
							'change': function(event) {
								state.selectedBands[band] = event.target.checked;
								state.bandDirty = true;
								controller.redraw();
							}
						}), ' ', bandLabel(band)
					]) ]);
				}))
		])
	]));
	children.push(E('div', { 'class': 'cbi-page-actions' }, [
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
	return E('div', { 'class': 'cbi-section' }, children);
}

function validateCellInput(state) {
	if (!/^(?:0|[1-9][0-9]{0,5})$/.test(state.earfcn))
		return _('EARFCN must be an unsigned LTE channel number.');
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
		return _('The expert bridge returned an incomplete schema 2 response.');
	if (requireAccepted && (!replacementIdentityIsValid(result) || result.accepted !== true ||
	    [ 'applied_verified', 'cleared_verified' ].indexOf(result.state) === -1 ||
	    verification.registration !== true || verification.nvm !== true ||
	    (result.state === 'applied_verified' &&
	     (verification.serving_cell !== true ||
	      !Number.isSafeInteger(verification.earfcn) || verification.earfcn < 0 ||
	      !Number.isSafeInteger(verification.pci) || verification.pci < 0 ||
	      verification.pci > 503))))
		return _('The expert bridge returned an incomplete schema 2 response.');
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
	controller.redraw();
	return invoke(context).then(function(result) {
		const error = validateExpertResult(result, context, true);
		const targetState = replacementIdentityIsValid(result) ?
			stateFor(result.modem_id) : state;

		state.busy = null;
		targetState.busy = null;
		targetState.result = error ? mutationFailureMessage(result, operation) : {
			kind: 'success',
			message: result.state === 'cleared_verified' ?
				_('The cell lock was cleared and its post-reset NVM state was verified.') :
				_('The cell lock was applied and verified against post-reset NVM and serving-cell state.')
		};
		return controller.refresh(true);
	}).catch(function(error) {
		state.busy = null;
		state.result = {
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
	const earfcnId = 'fibocom-earfcn-' + index;
	const pciId = 'fibocom-pci-' + index;
	const children = [
		E('h4', {}, [ _('PCI/EARFCN Lock') ]),
		widgets.keyValueTable([
			[ _('Capability'), widgets.badge(status.state, status.state) ],
			[ _('Reason'), status.reason ],
			[ _('Scan capability'), widgets.badge(scan.state, scan.state) ],
			[ _('Scan reason'), scan.reason ],
			[ _('NVM lock state'), observed.state ],
			[ _('Configured EARFCN'), observed.earfcn ],
			[ _('Configured PCI'), observed.pci ],
			[ _('Configured LTE band'), observed.band ]
		])
	];

	if ([ 'unsupported_build', 'unsupported_firmware' ].indexOf(status.state) !== -1) {
		children.push(E('div', { 'class': 'alert-message warning' }, [
			status.state === 'unsupported_build' ?
				_('PCI lock is absent from this base build. Install an explicitly reviewed expert build to expose the typed expert object.') :
				_('This firmware is not in the live-validated mutation allowlist. Standard ModemManager cell scan may still be attempted when advertised, but no vendor fallback, lock tuple, or reset sequence will be guessed.')
		]));
	}
	children.push(E('div', { 'class': 'cbi-page-actions' }, [
		E('button', {
			'class': 'btn cbi-button cbi-button-neutral', 'type': 'button',
			'disabled': !canScan || state.busy ? '' : null,
			'click': function() { return performScan(controller, entry, state); }
		}, [ state.busy === 'scan' ? _('Scanning…') : _('Scan cells') ])
	]));
	if (cells.length) {
		children.push(widgets.table([
			_('Role'), _('LTE type'), _('EARFCN'), _('PCI'), _('Band'), _('RSRP'), _('RSRQ')
		], cells.map(function(cell) {
			return [ cell.serving ? _('Serving') : _('Neighbor'), cell.type,
				cell.earfcn, cell.pci, cell.band, cell.rsrp, cell.rsrq ];
		})));
	}
	children.push(E('div', { 'class': 'cbi-value' }, [
		E('label', { 'class': 'cbi-value-title', 'for': earfcnId }, [ _('EARFCN') ]),
		E('div', { 'class': 'cbi-value-field' }, [ E('input', {
			'id': earfcnId, 'class': 'cbi-input-text', 'type': 'number', 'min': 0,
			'value': state.earfcn, 'disabled': !canMutate || state.busy ? '' : null,
			'input': function(event) { state.earfcn = event.target.value; }
		}) ])
	]));
	children.push(E('div', { 'class': 'cbi-value' }, [
		E('label', { 'class': 'cbi-value-title', 'for': pciId }, [ _('PCI (optional)') ]),
		E('div', { 'class': 'cbi-value-field' }, [ E('input', {
			'id': pciId, 'class': 'cbi-input-text', 'type': 'number', 'min': 0, 'max': 503,
			'value': state.pci, 'disabled': !canMutate || state.busy ? '' : null,
			'input': function(event) { state.pci = event.target.value; }
		}) ])
	]));
	if (inputError && (state.earfcn !== '' || state.pci !== ''))
		children.push(E('div', { 'class': 'alert-message warning' }, [ inputError ]));
	children.push(E('div', { 'class': 'cbi-page-actions' }, [
		E('button', {
			'class': 'btn cbi-button cbi-button-action', 'type': 'button',
			'disabled': !canMutate || state.busy || inputError ? '' : null,
			'click': function() {
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
		}, [ _('Apply cell lock') ]),
		' ',
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
	return E('div', { 'class': 'cbi-section' }, children);
}

function renderDevice(controller, entry, index) {
	const error = widgets.lockError(entry.lock, entry.summary);
	const state = stateFor(entry.summary.modem_id);
	const children = [ E('h3', {}, [ widgets.activeLabel(entry.summary.model, true) ]) ];

	if (error) {
		children.push(widgets.errorPanel(error));
		return E('div', { 'class': 'cbi-section' }, children);
	}
	synchronizeState(entry, state);
	const panel = statusPanel(state.result);

	if (panel)
		children.push(panel);
	children.push(renderBandLock(controller, entry, state, index));
	children.push(renderPciLock(controller, entry, state, index));
	return E('div', { 'class': 'cbi-section' }, children);
}

function renderSnapshots(snapshot, controller) {
	const error = widgets.listError(snapshot.list);

	if (error)
		return widgets.errorPanel(error);
	if (!snapshot.entries.length)
		return E('div', { 'class': 'alert-message notice' }, [
			_('No Fibocom modem is currently exported by ModemManager.')
		]);
	return E('div', {}, snapshot.entries.map(function(entry, index) {
		return renderDevice(controller, entry, index);
	}));
}

return view.extend({
	load: loadSnapshots,

	render: function(snapshot) {
		const controller = {
			snapshot: snapshot,
			content: null,
			refreshEpoch: 0,
			redraw: function() {
				if (this.content)
					dom.content(this.content, renderSnapshots(this.snapshot, this));
			},
			refresh: function(force) {
				const epoch = ++this.refreshEpoch;

				return loadSnapshots().then(function(next) {
					if (epoch !== controller.refreshEpoch)
						return null;
					controller.snapshot = next;
					if (force !== false)
						controller.redraw();
					return next;
				});
			}
		};

		controller.content = E('div', { 'id': 'fibocom-lock' }, [
			renderSnapshots(snapshot, controller)
		]);
		poll.add(function() {
			return controller.refresh(true);
		}, 10);
		return E('div', { 'class': 'cbi-map' }, [
			E('h2', {}, [ _('Lock') ]),
			E('div', { 'class': 'cbi-map-descr' }, [
				_('Band Lock uses ModemManager SetCurrentBands. PCI/EARFCN Lock is an explicit expert build path; only an exact live-validated hardware and firmware tuple can use its fixed command state machine.')
			]),
			controller.content
		]);
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
