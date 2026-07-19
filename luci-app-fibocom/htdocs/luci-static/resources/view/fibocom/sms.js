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

const SMS_LIMIT = 100;
const MAX_RECIPIENT_DIGITS = 20;
const MAX_TEXT_SCALARS = 1600;
const MAX_TEXT_BYTES = 6400;
const deviceStates = Object.create(null);

function transportResult(error) {
	return {
		transport_error: widgets.display(error && error.message, _('RPC transport failure'))
	};
}

function stateFor(modemId) {
	if (!Object.prototype.hasOwnProperty.call(deviceStates, modemId)) {
		deviceStates[modemId] = {
			folder: 'all',
			recipient: '',
			text: '',
			clientToken: null,
			tokenRecipient: null,
			tokenText: null,
			tokenModemId: null,
			tokenGeneration: null,
			tokenMessagingGeneration: null,
			sending: false,
			deleting: Object.create(null),
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

function validateListResult(result, modemId) {
	if (widgets.responseError(result))
		return result;

	if (!widgets.isObject(result) || result.ok !== true || result.modem_id !== modemId ||
		!Number.isSafeInteger(result.generation) ||
		!Number.isSafeInteger(result.messaging_generation) ||
		!Number.isSafeInteger(result.revision) || typeof result.cache_state !== 'string' ||
		!Array.isArray(result.messages) || typeof result.next_cursor !== 'string' ||
		typeof result.has_more !== 'boolean') {
		return {
			ok: false,
			error: {
				code: 'invalid_response',
				message: _('The bridge returned an incomplete SMS list response.')
			}
		};
	}

	return result;
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
			const state = stateFor(summary.modem_id);

			return api.listSms(summary.modem_id, state.folder, SMS_LIMIT, '')
				.catch(transportResult)
				.then(function(messages) {
					return {
						summary: summary,
						messages: validateListResult(messages, summary.modem_id)
					};
				});
		})).then(function(entries) {
			return { list: listResult, entries: entries };
		});
	}).catch(function(error) {
		return { list: transportResult(error), entries: [] };
	});
}

function folderOptions(selected) {
	return [
		[ 'all', _('All messages') ],
		[ 'inbox', _('Inbox') ],
		[ 'outbox', _('Outbox') ],
		[ 'draft', _('Drafts') ],
		[ 'unknown', _('Unknown') ]
	].map(function(folder) {
		return E('option', {
			'value': folder[0],
			'selected': folder[0] === selected ? '' : null
		}, [ folder[1] ]);
	});
}

function scalar(value, fallback) {
	return typeof value === 'string' || typeof value === 'number' ?
		String(value) : (fallback != null ? fallback : '—');
}

function directionLabel(direction) {
	switch (String(direction || '').toLowerCase()) {
	case 'incoming':
	case 'inbound':
	case 'inbox':
	case 'received':
		return _('Received');
	case 'outgoing':
	case 'outbound':
	case 'outbox':
	case 'sent':
		return _('Sent');
	case 'report':
		return _('Status report');
	case 'draft':
		return _('Draft');
	default:
		return _('Unknown');
	}
}

function isIncoming(direction) {
	return [ 'incoming', 'inbound', 'inbox', 'received' ].indexOf(
		String(direction || '').toLowerCase()) !== -1;
}

function deliveryState(value) {
	if (!widgets.isObject(value))
		return scalar(value);

	const name = scalar(value.name, '');
	const code = scalar(value.code, '');

	if (name && code && name !== code)
		return '%s (%s)'.format(name, code);

	return name || code || '—';
}

function textMetrics(value) {
	let scalars = 0;
	let bytes = 0;

	for (let index = 0; index < value.length; index++) {
		const unit = value.charCodeAt(index);

		if (unit >= 0xD800 && unit <= 0xDBFF) {
			if (index + 1 >= value.length)
				return null;

			const next = value.charCodeAt(index + 1);

			if (next < 0xDC00 || next > 0xDFFF)
				return null;

			index++;
			scalars++;
			bytes += 4;
		}
		else if (unit >= 0xDC00 && unit <= 0xDFFF) {
			return null;
		}
		else {
			scalars++;
			bytes += unit <= 0x7F ? 1 : (unit <= 0x7FF ? 2 : 3);
		}
	}

	return { scalars: scalars, bytes: bytes };
}

function validateDraft(recipient, text) {
	if (!/^\+?[0-9]{1,20}$/.test(recipient)) {
		return _('Enter a recipient containing 1–%d digits, with an optional leading +.')
			.format(MAX_RECIPIENT_DIGITS);
	}

	if (!text.length)
		return _('Enter the SMS text.');

	const metrics = textMetrics(text);

	if (!metrics)
		return _('The SMS text contains invalid Unicode.');

	if (metrics.scalars > MAX_TEXT_SCALARS || metrics.bytes > MAX_TEXT_BYTES) {
		return _('SMS text is limited to %d Unicode characters and %d UTF-8 bytes.')
			.format(MAX_TEXT_SCALARS, MAX_TEXT_BYTES);
	}

	return null;
}

function generateClientToken() {
	const provider = typeof window !== 'undefined' ? window.crypto : null;

	if (!provider || typeof provider.getRandomValues !== 'function')
		throw new Error(_('Secure random generation is unavailable in this browser.'));

	const bytes = new Uint8Array(16);
	let token = 'smsop-';

	provider.getRandomValues(bytes);
	for (let index = 0; index < bytes.length; index++)
		token += ('0' + bytes[index].toString(16)).slice(-2);

	return token;
}

function clearRetryToken(state) {
	state.clientToken = null;
	state.tokenRecipient = null;
	state.tokenText = null;
	state.tokenModemId = null;
	state.tokenGeneration = null;
	state.tokenMessagingGeneration = null;
}

function retrySafetyNotice() {
	return _('Request-token deduplication is kept only in bridge memory for five minutes. After waiting longer or reloading, do not assume a retry is duplicate-safe.');
}

function updateDraft(state, field, value) {
	state[field] = value;
	state.result = null;

	if (state.clientToken &&
		(state.recipient !== state.tokenRecipient || state.text !== state.tokenText))
		clearRetryToken(state);
}

function mutationContext(entry) {
	const summary = widgets.object(entry.summary);
	const messages = widgets.object(entry.messages);

	if (typeof summary.modem_id !== 'string' || messages.modem_id !== summary.modem_id ||
		!Number.isSafeInteger(messages.generation) ||
		(summary.generation != null && summary.generation !== messages.generation) ||
		!Number.isSafeInteger(messages.messaging_generation))
		return null;

	return {
		modemId: summary.modem_id,
		generation: messages.generation,
		messagingGeneration: messages.messaging_generation
	};
}

function isMutationBusy(state) {
	return state.sending || Object.keys(state.deleting).length > 0;
}

function statusPanel(result) {
	if (!result)
		return null;

	return E('div', { 'class': 'alert-message ' + result.kind }, [ result.message ]);
}

function handleSend(controller, entry, state, event) {
	if (event && typeof event.preventDefault === 'function')
		event.preventDefault();

	if (isMutationBusy(state))
		return Promise.resolve();

	const recipient = state.recipient.trim();
	const text = state.text;
	const validationError = validateDraft(recipient, text);
	const context = mutationContext(entry);

	if (validationError) {
		state.result = { kind: 'warning', message: validationError };
		controller.redraw();
		return Promise.resolve();
	}

	if (!context) {
		state.result = {
			kind: 'danger',
			message: _('The modem changed or its messaging generation is unavailable. Refresh before sending.')
		};
		controller.redraw();
		return Promise.resolve();
	}

	if (state.clientToken &&
		(state.tokenModemId !== context.modemId ||
		state.tokenGeneration !== context.generation ||
		state.tokenMessagingGeneration !== context.messagingGeneration)) {
		clearRetryToken(state);
		state.result = {
			kind: 'warning',
			message: _('The modem messaging generation changed after an uncertain send. Review the message list, then send again only if a new request is intended.')
		};
		controller.redraw();
		return Promise.resolve();
	}

	if (!state.clientToken || recipient !== state.tokenRecipient || text !== state.tokenText) {
		try {
			state.clientToken = generateClientToken();
		}
		catch (error) {
			state.result = { kind: 'danger', message: error.message };
			controller.redraw();
			return Promise.resolve();
		}

		state.tokenRecipient = recipient;
		state.tokenText = text;
		state.tokenModemId = context.modemId;
		state.tokenGeneration = context.generation;
		state.tokenMessagingGeneration = context.messagingGeneration;
	}

	state.recipient = recipient;
	state.sending = true;
	state.result = { kind: 'notice', message: _('Sending SMS…') };
	controller.redraw();

	return api.sendSms(
		context.modemId,
		context.generation,
		context.messagingGeneration,
		recipient,
		text,
		state.clientToken
	).then(function(result) {
		let error = widgets.responseError(result);
		let incomplete = false;

		if (!error && (!widgets.isObject(result) || result.ok !== true ||
			typeof result.sms_id !== 'string' || typeof result.state !== 'string' ||
			typeof result.deduplicated !== 'boolean')) {
			error = _('The bridge returned an incomplete SMS send response.');
			incomplete = true;
		}

		state.sending = false;
		if (error) {
			const details = widgets.object(widgets.object(result).error);
			const code = typeof details.code === 'string' ? details.code : '';
			const outcomeUncertain = incomplete ||
				[ 'outcome_unknown', 'timeout', 'busy' ].indexOf(code) !== -1;

			state.result = {
				kind: outcomeUncertain ? 'warning' : 'danger',
				message: outcomeUncertain ?
					_('SMS send was not confirmed: %s. Review the message list before retrying. Retry without editing to reuse the same request token.').format(error) + ' ' + retrySafetyNotice() :
					_('Unable to send SMS: %s. Edit the recipient or text to start a new request, or retry unchanged with the same request token.').format(error) + ' ' + retrySafetyNotice()
			};
			controller.redraw();
			return null;
		}

		clearRetryToken(state);
		state.recipient = '';
		state.text = '';
		state.result = {
			kind: 'success',
			message: result.deduplicated === true ?
				_('SMS send confirmed; the existing request was safely reused.') :
				_('SMS sent successfully.')
		};

		return controller.refresh(true);
	}).catch(function(error) {
		state.sending = false;
		state.result = {
			kind: 'warning',
			message: _('The send result is unknown because the RPC connection failed: %s. Retry without editing the recipient or text to reuse the same request token.')
				.format(widgets.display(error && error.message, _('Unknown transport error'))) +
				' ' + retrySafetyNotice()
		};
		controller.redraw();
		return null;
	});
}

function performDelete(controller, entry, state, smsId) {
	const context = mutationContext(entry);

	if (!context) {
		state.result = {
			kind: 'danger',
			message: _('The modem changed or its messaging generation is unavailable. Refresh before deleting.')
		};
		controller.redraw();
		return Promise.resolve();
	}

	state.deleting[smsId] = true;
	state.result = { kind: 'notice', message: _('Deleting SMS…') };
	controller.redraw();

	return api.deleteSms(
		context.modemId,
		context.generation,
		context.messagingGeneration,
		smsId,
		true
	).then(function(result) {
		let error = widgets.responseError(result);

		if (!error && (!widgets.isObject(result) || result.ok !== true ||
			result.sms_id !== smsId || result.deleted !== true))
			error = _('The bridge returned an incomplete SMS delete response.');

		delete state.deleting[smsId];
		if (error) {
			state.result = {
				kind: 'danger',
				message: _('Unable to delete SMS: %s').format(error)
			};
			controller.redraw();
			return null;
		}

		state.result = { kind: 'success', message: _('SMS deleted.') };
		return controller.refresh(true);
	}).catch(function(error) {
		delete state.deleting[smsId];
		state.result = {
			kind: 'danger',
			message: _('Unable to confirm whether the SMS was deleted: %s')
				.format(widgets.display(error && error.message, _('Unknown transport error')))
		};
		controller.redraw();
		return null;
	});
}

function confirmDelete(controller, entry, state, smsId) {
	if (isMutationBusy(state))
		return;

	ui.showModal(_('Delete SMS'), [
		E('p', {}, [ _('Delete this SMS permanently? This action cannot be undone.') ]),
		E('div', { 'class': 'right' }, [
			E('button', {
				'class': 'btn',
				'click': ui.hideModal
			}, [ _('Cancel') ]),
			' ',
			E('button', {
				'class': 'btn cbi-button-negative important',
				'click': function() {
					ui.hideModal();
					return performDelete(controller, entry, state, smsId);
				}
			}, [ _('Delete') ])
		])
	]);
}

function renderMessage(controller, entry, state, message) {
	const direction = scalar(message.direction, 'unknown');
	const numberLabel = String(direction).toLowerCase() === 'report' ? _('Number') :
		(isIncoming(direction) ? _('From') : _('To'));
	const delivery = deliveryState(message.delivery_state);
	const smsId = typeof message.sms_id === 'string' ? message.sms_id : null;
	const text = typeof message.text === 'string' ? message.text : '';
	const busy = isMutationBusy(state);
	const rows = [
		[ _('Direction'), directionLabel(direction) ],
		[ numberLabel, scalar(message.number) ],
		[ _('Timestamp'), scalar(message.timestamp) ],
		[ _('State'), widgets.badge(scalar(message.state, _('Unknown')), message.state) ],
		[ _('Folder'), scalar(message.folder) ],
		[ _('PDU type'), scalar(message.pdu_type) ],
		[ _('Delivery state'), delivery ],
		[ _('Message reference'), scalar(message.message_reference) ]
	];

	return E('div', { 'class': 'cbi-section-node' }, [
		E('h5', {}, [
			directionLabel(direction),
			message.timestamp != null ? ' — ' + scalar(message.timestamp) : ''
		]),
		widgets.keyValueTable(rows),
		message.text_truncated === true && isIncoming(direction) ?
			E('div', { 'class': 'alert-message warning' }, [
				_('This received message was truncated by the bridge safety limit.')
			]) : E([]),
		E('div', { 'class': 'cbi-value' }, [
			E('div', { 'class': 'cbi-value-title' }, [ _('Message') ]),
			E('div', {
				'class': 'cbi-value-field',
				'style': 'white-space: pre-wrap; overflow-wrap: anywhere;'
			}, [ text || _('Empty message') ])
		]),
		smsId ? E('div', { 'class': 'right' }, [
			E('button', {
				'class': 'btn cbi-button-negative',
				'type': 'button',
				'disabled': busy ? '' : null,
				'click': function() {
					confirmDelete(controller, entry, state, smsId);
				}
			}, [ state.deleting[smsId] ? _('Deleting…') : _('Delete') ])
		]) : E([])
	]);
}

function renderCompose(controller, entry, state, index) {
	const busy = isMutationBusy(state);
	const recipientId = 'fibocom-sms-recipient-' + index;
	const textId = 'fibocom-sms-text-' + index;
	const status = statusPanel(state.result);
	const children = [
		E('h4', {}, [ _('Compose SMS') ]),
		E('div', { 'class': 'cbi-value' }, [
			E('label', {
				'class': 'cbi-value-title',
				'for': recipientId
			}, [ _('Recipient') ]),
			E('div', { 'class': 'cbi-value-field' }, [
				E('input', {
					'id': recipientId,
					'class': 'cbi-input-text',
					'type': 'tel',
					'inputmode': 'tel',
					'autocomplete': 'off',
					'maxlength': MAX_RECIPIENT_DIGITS + 1,
					'placeholder': _('Optional +, followed by up to 20 digits'),
					'value': state.recipient,
					'disabled': busy ? '' : null,
					'input': function(event) {
						updateDraft(state, 'recipient', event.target.value);
					}
				})
			])
		]),
		E('div', { 'class': 'cbi-value' }, [
			E('label', {
				'class': 'cbi-value-title',
				'for': textId
			}, [ _('Message') ]),
			E('div', { 'class': 'cbi-value-field' }, [
				E('textarea', {
					'id': textId,
					'class': 'cbi-input-textarea',
					'rows': 5,
					'autocomplete': 'off',
					'placeholder': _('Enter message text'),
					'disabled': busy ? '' : null,
					'input': function(event) {
						updateDraft(state, 'text', event.target.value);
					}
				}, [ state.text ]),
				E('div', { 'class': 'cbi-value-description' }, [
					_('Maximum %d Unicode characters and %d UTF-8 bytes.')
						.format(MAX_TEXT_SCALARS, MAX_TEXT_BYTES)
				])
			])
		]),
		E('div', { 'class': 'cbi-page-actions' }, [
			E('button', {
				'class': 'btn cbi-button cbi-button-positive important',
				'type': 'submit',
				'disabled': busy ? '' : null
			}, [ state.sending ? _('Sending…') :
				(state.clientToken ? _('Retry SMS send') : _('Send SMS')) ])
		])
	];

	if (status)
		children.splice(1, 0, status);

	return E('form', {
		'class': 'cbi-section',
		'submit': function(event) {
			return handleSend(controller, entry, state, event);
		}
	}, children);
}

function renderDevice(controller, entry, index) {
	const summary = widgets.object(entry.summary);
	const messages = widgets.object(entry.messages);
	const state = stateFor(summary.modem_id);
	const error = widgets.responseError(entry.messages);
	const title = widgets.display(summary.model, _('Fibocom modem'));
	const children = [ E('h3', {}, [ title ]) ];

	if (error) {
		children.push(widgets.errorPanel({ transport_error: error }));
		return E('div', { 'class': 'cbi-section' }, children);
	}

	const notice = widgets.schemaNotice(messages);
	const list = Array.isArray(messages.messages) ? messages.messages.filter(function(message) {
		return widgets.isObject(message);
	}) : [];
	const filterId = 'fibocom-sms-folder-' + index;

	if (notice)
		children.push(notice);

	children.push(E('div', { 'class': 'cbi-section' }, [
		E('div', { 'class': 'cbi-value' }, [
			E('label', {
				'class': 'cbi-value-title',
				'for': filterId
			}, [ _('Folder') ]),
			E('div', { 'class': 'cbi-value-field' }, [
				E('select', {
					'id': filterId,
					'class': 'cbi-input-select',
					'change': function(event) {
						state.folder = event.target.value;
						state.result = null;
						return controller.refresh(true);
					}
				}, folderOptions(state.folder))
			])
		]),
		widgets.keyValueTable([
			[ _('Messaging cache'), widgets.badge(
				widgets.display(messages.cache_state, _('Unknown')), messages.cache_state) ],
			[ _('Revision'), messages.revision ]
		])
	]));

	children.push(renderCompose(controller, entry, state, index));
	children.push(E('div', { 'class': 'cbi-section' }, [
		E('h4', {}, [ _('Messages') ]),
		list.length ? E('div', {}, list.map(function(message) {
			return renderMessage(controller, entry, state, message);
		})) : E('div', { 'class': 'alert-message notice' }, [
			_('No SMS messages are available in this folder.')
		]),
		messages.has_more === true ? E('div', { 'class': 'alert-message notice' }, [
			_('Only the first %d messages are shown. Narrow the folder filter to find older messages.')
				.format(SMS_LIMIT)
		]) : E([])
	]));

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
			content: null,
			snapshot: snapshot,
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
			'id': 'fibocom-sms',
			'focusout': function() {
				redrawAfterEditorBlur(controller);
			}
		}, [
			renderSnapshots(snapshot, controller)
		]);

		poll.add(function() {
			return controller.refresh(false);
		}, 10);

		return E('div', { 'class': 'cbi-map' }, [
			E('h2', {}, [ _('Fibocom Modem SMS') ]),
			E('div', { 'class': 'cbi-map-descr' }, [
				_('Messages are read, sent, and deleted through ModemManager. Recipient numbers and message text remain only in this authorized view and are never written to application logs.')
			]),
			controller.content
		]);
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
