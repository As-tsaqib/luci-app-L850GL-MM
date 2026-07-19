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
			selectedSlot: null,
			slotDirty: false,
			busy: null,
			result: null
		};
	}

	return deviceStates[modemId];
}

function pruneDeviceStates(summaries) {
	const present = Object.create(null);

	summaries.forEach(function(summary) {
		if (widgets.isObject(summary) && typeof summary.modem_id === 'string')
			present[summary.modem_id] = true;
	});

	Object.keys(deviceStates).forEach(function(modemId) {
		if (!present[modemId])
			delete deviceStates[modemId];
	});
}

function loadSnapshots() {
	return api.listModems().then(function(listResult) {
		if (widgets.responseError(listResult))
			return { list: listResult, entries: [] };

		const summaries = widgets.modems(listResult).filter(function(summary) {
			return widgets.isObject(summary) && typeof summary.modem_id === 'string';
		});

		pruneDeviceStates(summaries);

		return Promise.all(summaries.map(function(summary) {
			return Promise.all([
				api.getStatus(summary.modem_id).catch(transportResult),
				api.getCapabilities(summary.modem_id).catch(transportResult)
			]).then(function(results) {
				return {
					summary: summary,
					status: results[0],
					capabilities: results[1]
				};
			});
		})).then(function(entries) {
			return { list: listResult, entries: entries };
		});
	}).catch(function(error) {
		return { list: transportResult(error), entries: [] };
	});
}

function uniqueCanonicalStrings(values, includeAny) {
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

function supportedBandIds(status) {
	return uniqueCanonicalStrings(widgets.object(status.radio).supported_bands, false);
}

function currentBandIds(status) {
	return uniqueCanonicalStrings(widgets.object(status.radio).current_bands, true);
}

function validSlots(status) {
	const raw = widgets.object(status.sim).slots;
	const seen = Object.create(null);
	const slots = [];

	if (Array.isArray(raw)) {
		raw.forEach(function(entry) {
			const details = widgets.object(entry);
			const slot = Number(details.slot != null ? details.slot : details.number);

			if (!Number.isSafeInteger(slot) || slot < 1 || slot > 16 || seen[slot])
				return;

			seen[slot] = true;
			slots.push({
				slot: slot,
				present: details.present,
				active: details.active === true || details.primary === true
			});
		});
	}
	else if (Number.isSafeInteger(raw) && raw > 0 && raw <= 16) {
		for (let slot = 1; slot <= raw; slot++)
			slots.push({ slot: slot, present: null, active: null });
	}

	return slots;
}

function synchronizeState(entry, state) {
	const status = widgets.object(entry.status);
	const generation = status.generation;
	const radio = widgets.object(status.radio);
	const sim = widgets.object(status.sim);
	const generationChanged = state.generation !== generation;

	if (generationChanged) {
		state.generation = generation;
		state.bandDirty = false;
		state.slotDirty = false;
		state.busy = null;
		state.result = null;
	}

	if (!state.bandDirty && !state.busy) {
		const supported = supportedBandIds(status);
		const supportedSet = Object.create(null);
		const current = currentBandIds(status);

		supported.forEach(function(band) { supportedSet[band] = true; });
		state.bandAutomatic = radio.band_selection === 'all-supported' ||
			current.indexOf('any') !== -1;
		state.selectedBands = Object.create(null);
		current.forEach(function(band) {
			if (supportedSet[band])
				state.selectedBands[band] = true;
		});
	}

	if (!state.slotDirty && !state.busy) {
		const primary = Number(sim.primary_slot != null ? sim.primary_slot : sim.slot);

		state.selectedSlot = Number.isSafeInteger(primary) && primary > 0 ? primary : null;
	}
}

function capability(entry, name) {
	return widgets.object(widgets.object(entry.capabilities).capabilities)[name] || {};
}

function capabilityIsMutable(entry, name) {
	const feature = widgets.object(capability(entry, name));

	return feature.state === 'available' && feature.mutable === true &&
		feature.busy !== true;
}

function snapshotContext(entry) {
	const summary = widgets.object(entry.summary);
	const status = widgets.object(entry.status);
	const capabilities = widgets.object(entry.capabilities);

	if (typeof summary.modem_id !== 'string' || status.modem_id !== summary.modem_id ||
		capabilities.modem_id !== summary.modem_id ||
		!Number.isSafeInteger(status.generation) ||
		capabilities.generation !== status.generation ||
		(summary.generation != null && summary.generation !== status.generation))
		return null;

	return { modemId: summary.modem_id, generation: status.generation };
}

function mutationContext(entry, name) {
	const context = snapshotContext(entry);

	return context && capabilityIsMutable(entry, name) ? context : null;
}

function statusPanel(result) {
	if (!result)
		return null;

	return E('div', { 'class': 'alert-message ' + result.kind }, [ result.message ]);
}

function capabilitySummary(entry, name) {
	const feature = widgets.object(capability(entry, name));
	const state = feature.state || 'unknown';
	const rows = [
		[ _('Capability'), widgets.badge(widgets.display(state), state) ],
		[ _('Mutable'), feature.mutable ],
		[ _('Reason'), feature.reason ]
	];

	if (feature.busy === true || state === 'busy')
		rows.push([ _('Busy'), true ]);
	if (feature.retry_after_ms != null)
		rows.push([ _('Retry after'), _('%d ms').format(Number(feature.retry_after_ms)) ]);

	return widgets.keyValueTable(rows);
}

function networkInterfaceUrl(status) {
	const binding = widgets.object(status.network_binding);
	const section = typeof binding.section === 'string' ? binding.section : '';

	if (/^[A-Za-z0-9_]{1,32}$/.test(section))
		return L.url('admin/network/network', section);

	return L.url('admin/network/network');
}

function networkInterfaceLink(status, label) {
	return E('a', {
		'class': 'cbi-button',
		'href': networkInterfaceUrl(status)
	}, [ label || _('Open Network Interfaces') ]);
}

function mutationFailureMessage(result, operation) {
	const error = widgets.object(widgets.object(result).error);
	const details = widgets.responseError(result) || _('The request failed.');

	if (error.code === 'outcome_unknown') {
		return {
			kind: 'warning',
			message: _('%s may have completed, but its outcome is unknown. The view has been refreshed; do not retry until the live modem state confirms that a new request is safe.')
				.format(operation)
		};
	}

	if ([ 'stale_generation', 'stale_identity', 'device_gone', 'not_found' ]
		.indexOf(error.code) !== -1) {
		return {
			kind: 'warning',
			message: _('The modem identity or generation changed before %s could complete. The view has been refreshed; review the new modem state before trying again.')
				.format(operation)
		};
	}

	return {
		kind: error.code === 'busy' ? 'warning' : 'danger',
		message: _('%s failed: %s').format(operation, details)
	};
}

function currentEntry(controller, modemId) {
	const entries = widgets.object(controller.snapshot).entries;

	if (!Array.isArray(entries))
		return null;

	for (let index = 0; index < entries.length; index++) {
		if (widgets.object(entries[index].summary).modem_id === modemId)
			return entries[index];
	}

	return null;
}

function performMutation(controller, entry, featureName, operation, invoke, acceptedMessage) {
	const summary = widgets.object(entry.summary);
	const state = stateFor(summary.modem_id);
	const displayedContext = mutationContext(entry, featureName);
	const latestEntry = currentEntry(controller, summary.modem_id);
	const context = latestEntry ? mutationContext(latestEntry, featureName) : null;

	if (state.busy)
		return Promise.resolve();

	if (!displayedContext || !context ||
		displayedContext.modemId !== context.modemId ||
		displayedContext.generation !== context.generation) {
		state.result = {
			kind: 'warning',
			message: _('The capability, modem identity, or generation is no longer current. Refresh and review the modem before applying a change.')
		};
		controller.redraw();
		return controller.refresh(true);
	}

	state.busy = featureName;
	state.result = { kind: 'notice', message: _('%s in progress…').format(operation) };
	controller.redraw();

	return invoke(context).then(function(result) {
		let error = widgets.responseError(result);

		if (!error && (!widgets.isObject(result) || result.ok !== true ||
			result.accepted !== true ||
			(result.modem_id != null && result.modem_id !== context.modemId) ||
			(result.generation != null && result.generation !== context.generation)))
			error = _('The bridge returned an incomplete mutation response.');

		state.busy = null;
		if (error) {
			state.result = mutationFailureMessage(error === widgets.responseError(result) ?
				result : { ok: false, error: { message: error } }, operation);
		}
		else {
			state.result = { kind: 'success', message: acceptedMessage };
			if (featureName === 'bands')
				state.bandDirty = false;
			if (featureName === 'sim_slot')
				state.slotDirty = false;
		}

		return controller.refresh(true);
	}).catch(function(error) {
		state.busy = null;
		state.result = {
			kind: 'warning',
			message: _('The outcome of %s is unknown because the RPC connection failed: %s. The view has been refreshed; do not retry until the live state is known.')
				.format(operation, widgets.display(error && error.message, _('Unknown transport error')))
		};
		return controller.refresh(true);
	});
}

function confirmMutation(entry, state, title, warning, actionLabel, actionClass, action) {
	if (state.busy)
		return;

	ui.showModal(title, [
		E('div', { 'class': 'alert-message warning' }, [ warning ]),
		E('p', {}, [
			_('The request is bound to the currently displayed modem generation and will fail safely if the modem was replugged or replaced.')
		]),
		E('div', { 'class': 'right' }, [
			E('button', {
				'class': 'btn',
				'type': 'button',
				'click': ui.hideModal
			}, [ _('Cancel') ]),
			' ',
			E('button', {
				'class': actionClass,
				'type': 'button',
				'click': function() {
					ui.hideModal();
					return action(entry);
				}
			}, [ actionLabel ])
		])
	]);
}

function bandLabel(band) {
	let match = /^eutran-([0-9]+)$/.exec(band);

	if (match)
		return _('%s — LTE band %s').format(band, match[1]);

	match = /^utran-([0-9]+)$/.exec(band);
	if (match)
		return _('%s — UMTS band %s').format(band, match[1]);

	match = /^ngran-([0-9]+)$/.exec(band);
	if (match)
		return _('%s — 5G NR band %s').format(band, match[1]);

	return band;
}

function renderBands(controller, entry, state, index) {
	const status = widgets.object(entry.status);
	const radio = widgets.object(status.radio);
	const supported = supportedBandIds(status);
	const current = currentBandIds(status);
	const canMutate = mutationContext(entry, 'bands') !== null;
	const selected = Object.keys(state.selectedBands).filter(function(band) {
		return state.selectedBands[band] === true && supported.indexOf(band) !== -1;
	});
	const validSelection = state.bandAutomatic || selected.length > 0;
	const automaticId = 'fibocom-band-any-' + index;
	const explicitId = 'fibocom-band-explicit-' + index;
	const children = [
		E('h4', {}, [ _('Band selection') ]),
		capabilitySummary(entry, 'bands'),
		widgets.keyValueTable([
			[ _('Selection reported by ModemManager'), radio.band_selection ],
			[ _('Current bands'), current.length ? current : null ],
			[ _('Supported bands'), supported.length ? supported : null ]
		])
	];

	if (!canMutate) {
		children.push(E('div', { 'class': 'alert-message notice' }, [
			capabilityIsMutable(entry, 'bands') ?
				_('Band changes are temporarily disabled because the modem identity and capability snapshots do not have a matching generation.') :
				_('Band changes are unavailable because the bridge or modem did not advertise a mutable standard band capability. No vendor command fallback will be attempted.')
		]));
	}

	children.push(E('div', { 'class': 'cbi-value' }, [
		E('div', { 'class': 'cbi-value-title' }, [ _('Requested bands') ]),
		E('div', { 'class': 'cbi-value-field' }, [
			E('label', { 'for': automaticId }, [
				E('input', {
					'id': automaticId,
					'name': 'fibocom-band-policy-' + index,
					'type': 'radio',
					'checked': state.bandAutomatic ? '' : null,
					'disabled': !canMutate || state.busy ? '' : null,
					'change': function() {
						state.bandAutomatic = true;
						state.bandDirty = true;
						state.result = null;
						controller.redraw();
					}
				}),
				' ', _('Any supported band (automatic)')
			]),
			E('br'),
			E('label', { 'for': explicitId }, [
				E('input', {
					'id': explicitId,
					'name': 'fibocom-band-policy-' + index,
					'type': 'radio',
					'checked': !state.bandAutomatic ? '' : null,
					'disabled': !canMutate || state.busy || !supported.length ? '' : null,
					'change': function() {
						state.bandAutomatic = false;
						state.bandDirty = true;
						state.result = null;
						controller.redraw();
					}
				}),
				' ', _('Explicit supported bands')
			]),
			E('div', { 'style': 'margin-left: 1.5em; margin-top: .5em;' },
				supported.length ? supported.map(function(band, bandIndex) {
					const id = 'fibocom-band-' + index + '-' + bandIndex;

					return E('div', {}, [ E('label', { 'for': id }, [
						E('input', {
							'id': id,
							'type': 'checkbox',
							'checked': state.selectedBands[band] === true ? '' : null,
							'disabled': !canMutate || state.busy || state.bandAutomatic ? '' : null,
							'change': function(event) {
								state.selectedBands[band] = event.target.checked;
								state.bandDirty = true;
								state.result = null;
								controller.redraw();
							}
						}),
						' ', bandLabel(band)
					]) ]);
				}) : [ E('em', {}, [ _('No explicit supported band IDs were reported.') ]) ])
		])
	]));

	children.push(E('div', { 'class': 'cbi-page-actions' }, [
		E('button', {
			'class': 'btn cbi-button cbi-button-action',
			'type': 'button',
			'disabled': !canMutate || state.busy || !state.bandDirty || !validSelection ? '' : null,
			'click': function() {
				const bands = state.bandAutomatic ? [ 'any' ] : selected.slice();

				confirmMutation(entry, state, _('Apply band selection'),
					_('Changing radio bands can immediately detach the mobile network and interrupt this router’s WAN connection. Keep an alternate management path available.'),
					_('Apply bands'), 'btn cbi-button-action important', function(currentEntry) {
						return performMutation(controller, currentEntry, 'bands',
							_('Band change'), function(context) {
								return api.setBands(context.modemId, context.generation,
									bands, true);
							}, _('Band change accepted. Refreshing the live modem state…'));
					});
			}
		}, [ state.busy === 'bands' ? _('Applying…') : _('Apply band selection') ])
	]));

	return E('div', { 'class': 'cbi-section' }, children);
}

function modeText(value) {
	return uniqueCanonicalStrings(value, false).join(', ') || '—';
}

function renderModes(entry) {
	const status = widgets.object(entry.status);
	const radio = widgets.object(status.radio);
	const current = widgets.object(radio.current_modes);
	const supported = Array.isArray(radio.supported_modes) ? radio.supported_modes : [];
	const binding = widgets.object(status.network_binding);
	const rows = supported.filter(widgets.isObject).map(function(combination) {
		return [ modeText(combination.allowed), widgets.display(combination.preferred, _('None')) ];
	});

	return E('div', { 'class': 'cbi-section' }, [
		E('h4', {}, [ _('Network modes') ]),
		capabilitySummary(entry, 'modes'),
		widgets.keyValueTable([
			[ _('Current allowed modes'), modeText(current.allowed) ],
			[ _('Current preferred mode'), current.preferred ],
			[ _('Bound network interface'), binding.section ],
			[ _('Persistent allowed mode'), binding.allowedmode ],
			[ _('Persistent preferred mode'), binding.preferredmode ]
		]),
		widgets.table([ _('Allowed modes'), _('Preferred mode') ], rows,
			_('No supported mode combinations were reported.')),
		E('div', { 'class': 'alert-message notice' }, [
			_('Mode selection is read-only here. Persistent allowed and preferred modes belong to the netifd ModemManager interface so reconnects keep one authoritative configuration.')
		]),
		E('p', {}, [ networkInterfaceLink(status, _('Configure persistent network modes')) ])
	]);
}

function renderRadio(controller, entry, state) {
	const status = widgets.object(entry.status);
	const radio = widgets.object(status.radio);
	const canMutate = mutationContext(entry, 'radio') !== null;
	const radioState = radio.state || radio.power_state || 'unknown';
	const currentlyEnabled = radio.state === 'enabled' || radio.power_state === 'on';
	const currentlyDisabled = radio.state === 'disabled' || radio.power_state === 'off';
	const children = [
		E('h4', {}, [ _('Radio power') ]),
		capabilitySummary(entry, 'radio'),
		widgets.keyValueTable([
			[ _('Radio state'), widgets.badge(widgets.display(radioState), radioState) ],
			[ _('Power state'), radio.power_state ]
		])
	];

	if (!canMutate) {
		children.push(E('div', { 'class': 'alert-message notice' }, [
			_('Direct radio control is unavailable. Persistent connection intent remains owned by netifd; review the bound ModemManager network interface instead.')
		]));
		children.push(E('p', {}, [ networkInterfaceLink(status, _('Open the bound network interface')) ]));
	}
	else {
		children.push(E('p', {}, [
			_('This is an immediate ModemManager radio action. It does not change netifd configuration, and netifd may reconnect according to the persistent network interface intent.')
		]));
		children.push(E('div', { 'class': 'cbi-page-actions' }, [
			E('button', {
				'class': 'btn cbi-button-positive',
				'type': 'button',
				'disabled': state.busy || currentlyEnabled ? '' : null,
				'click': function() {
					confirmMutation(entry, state, _('Enable radio'),
						_('Enabling the radio may cause netifd to restore the cellular WAN session according to its saved interface configuration.'),
						_('Enable radio'), 'btn cbi-button-positive important', function(currentEntry) {
							return performMutation(controller, currentEntry, 'radio',
								_('Radio enable'), function(context) {
									return api.setRadio(context.modemId, context.generation,
										true, true);
								}, _('Radio enable accepted. Refreshing the live modem state…'));
						});
				}
			}, [ state.busy === 'radio' ? _('Applying…') : _('Enable radio') ]),
			' ',
			E('button', {
				'class': 'btn cbi-button-negative',
				'type': 'button',
				'disabled': state.busy || currentlyDisabled ? '' : null,
				'click': function() {
					confirmMutation(entry, state, _('Disable radio'),
						_('Disabling the radio will detach the mobile network and can immediately remove this router’s WAN access. Keep an alternate management path available.'),
						_('Disable radio'), 'btn cbi-button-negative important', function(currentEntry) {
							return performMutation(controller, currentEntry, 'radio',
								_('Radio disable'), function(context) {
									return api.setRadio(context.modemId, context.generation,
										false, true);
								}, _('Radio disable accepted. Refreshing the live modem state…'));
						});
				}
			}, [ state.busy === 'radio' ? _('Applying…') : _('Disable radio') ])
		]));
	}

	return E('div', { 'class': 'cbi-section' }, children);
}

function renderSimSlot(controller, entry, state, index) {
	const status = widgets.object(entry.status);
	const sim = widgets.object(status.sim);
	const slots = validSlots(status);
	const canMutate = mutationContext(entry, 'sim_slot') !== null;
	const selectedIsValid = slots.some(function(slot) { return slot.slot === state.selectedSlot; });
	const selectId = 'fibocom-primary-slot-' + index;
	const children = [
		E('h4', {}, [ _('Primary SIM slot') ]),
		capabilitySummary(entry, 'sim_slot'),
		widgets.keyValueTable([
			[ _('Current primary SIM slot'), sim.primary_slot || sim.slot ],
			[ _('Advertised physical slots'), slots.length ]
		])
	];

	if (!canMutate) {
		children.push(E('div', { 'class': 'alert-message notice' }, [
			_('SIM-slot switching is unavailable unless ModemManager advertises multiple physical slots and a mutable standard slot capability. eSIM profile selection is handled separately by the optional eSIM application.')
		]));
	}

	children.push(E('div', { 'class': 'cbi-value' }, [
		E('label', { 'class': 'cbi-value-title', 'for': selectId }, [ _('Requested slot') ]),
		E('div', { 'class': 'cbi-value-field' }, [
			E('select', {
				'id': selectId,
				'class': 'cbi-input-select',
				'disabled': !canMutate || state.busy || !slots.length ? '' : null,
				'change': function(event) {
					state.selectedSlot = Number(event.target.value);
					state.slotDirty = true;
					state.result = null;
					controller.redraw();
				}
			}, slots.map(function(slot) {
				let suffix = '';

				if (slot.active === true)
					suffix = _(' (active)');
				else if (slot.present === false)
					suffix = _(' (not present)');

				return E('option', {
					'value': slot.slot,
					'selected': slot.slot === state.selectedSlot ? '' : null
				}, [ _('SIM slot %d%s').format(slot.slot, suffix) ]);
			}) )
		])
	]));

	children.push(E('div', { 'class': 'cbi-page-actions' }, [
		E('button', {
			'class': 'btn cbi-button-negative',
			'type': 'button',
			'disabled': !canMutate || state.busy || !state.slotDirty || !selectedIsValid ? '' : null,
			'click': function() {
				const slot = state.selectedSlot;

				confirmMutation(entry, state, _('Switch primary SIM slot'),
					_('Switching the primary SIM can detach the mobile network, interrupt WAN access, and replace the active SMS or eSIM context. Keep an alternate management path available.'),
					_('Switch SIM slot'), 'btn cbi-button-negative important', function(currentEntry) {
						return performMutation(controller, currentEntry, 'sim_slot',
							_('SIM-slot change'), function(context) {
								return api.setPrimarySimSlot(context.modemId,
									context.generation, slot, true);
							}, _('SIM-slot change accepted. Refreshing the live modem state…'));
					});
			}
		}, [ state.busy === 'sim_slot' ? _('Switching…') : _('Switch primary SIM slot') ])
	]));

	return E('div', { 'class': 'cbi-section' }, children);
}

function renderReset(controller, entry, state) {
	const canMutate = mutationContext(entry, 'reset') !== null;
	const children = [
		E('h4', {}, [ _('Reset modem') ]),
		capabilitySummary(entry, 'reset'),
		E('p', {}, [
			_('A standard ModemManager reset is issued once. There is no vendor-command fallback or automatic reset loop.')
		])
	];

	children.push(E('div', { 'class': 'cbi-page-actions' }, [
		E('button', {
			'class': 'btn cbi-button-negative important',
			'type': 'button',
			'disabled': !canMutate || state.busy ? '' : null,
			'click': function() {
				confirmMutation(entry, state, _('Reset modem'),
					_('Resetting the modem will interrupt WAN access and make the modem disappear temporarily. A recovered modem receives a new identity, so wait for it to reappear before taking another action.'),
					_('Reset modem'), 'btn cbi-button-negative important', function(currentEntry) {
						return performMutation(controller, currentEntry, 'reset',
							_('Modem reset'), function(context) {
								return api.reset(context.modemId, context.generation, true);
							}, _('Modem reset accepted. Waiting for ModemManager to rediscover it…'));
					});
			}
		}, [ state.busy === 'reset' ? _('Resetting…') : _('Reset modem') ])
	]));

	return E('div', { 'class': 'cbi-section' }, children);
}

function renderDevice(controller, entry, index) {
	const summary = widgets.object(entry.summary);
	const statusError = widgets.responseError(entry.status);
	const capabilityError = widgets.responseError(entry.capabilities);
	const state = stateFor(summary.modem_id);
	const children = [ E('h3', {}, [ widgets.display(summary.model, _('Fibocom modem')) ]) ];

	if (statusError || capabilityError) {
		if (statusError)
			children.push(widgets.errorPanel({ transport_error: statusError }));
		if (capabilityError)
			children.push(widgets.errorPanel({ transport_error: capabilityError }));
		children.push(E('div', { 'class': 'alert-message warning' }, [
			_('Advanced controls are disabled until matching status and capability snapshots can be loaded.')
		]));
		return E('div', { 'class': 'cbi-section' }, children);
	}

	synchronizeState(entry, state);
	const panel = statusPanel(state.result);

	if (panel)
		children.push(panel);
	if (!snapshotContext(entry)) {
		children.push(E('div', { 'class': 'alert-message warning' }, [
			_('The status and capability snapshots do not identify the same live modem generation. All changes are disabled until a refresh returns matching snapshots.')
		]));
	}
	children.push(renderBands(controller, entry, state, index));
	children.push(renderModes(entry));
	children.push(renderRadio(controller, entry, state));
	children.push(renderSimSlot(controller, entry, state, index));
	children.push(renderReset(controller, entry, state));

	return E('div', { 'class': 'cbi-section' }, children);
}

function renderSnapshots(snapshot, controller) {
	const error = widgets.responseError(snapshot.list);

	if (error)
		return widgets.errorPanel({ transport_error: error });

	if (!snapshot.entries.length) {
		return E('div', { 'class': 'alert-message notice' }, [
			_('No Fibocom modem is currently exported by ModemManager.')
		]);
	}

	return E('div', {}, snapshot.entries.map(function(entry, index) {
		return renderDevice(controller, entry, index);
	}));
}

function editorHasFocus(content) {
	if (typeof document === 'undefined' || !content || typeof content.contains !== 'function')
		return false;

	const active = document.activeElement;
	const tag = active && String(active.tagName || '').toLowerCase();

	return content.contains(active) && [ 'input', 'select' ].indexOf(tag) !== -1;
}

return view.extend({
	load: loadSnapshots,

	render: function(snapshot) {
		const controller = {
			content: null,
			snapshot: snapshot,
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
					if (force || !editorHasFocus(controller.content))
						controller.redraw();

					return next;
				});
			}
		};

		controller.content = E('div', { 'id': 'fibocom-advanced' }, [
			renderSnapshots(snapshot, controller)
		]);

		poll.add(function() {
			return controller.refresh(false);
		}, 10);

		return E('div', { 'class': 'cbi-map' }, [
			E('h2', {}, [ _('Fibocom Modem Advanced Controls') ]),
			E('div', { 'class': 'cbi-map-descr' }, [
				_('Only capability-gated standard ModemManager operations are available here. This page exposes no vendor command console, device paths, data-session controls, or host command access.')
			]),
			controller.content
		]);
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
