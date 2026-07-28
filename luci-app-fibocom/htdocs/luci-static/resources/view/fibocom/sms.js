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
const SMS_CACHE_MAX = 1024;
const MAX_SMS_PAGES = Math.ceil(SMS_CACHE_MAX / SMS_LIMIT);
const MAX_RECIPIENT_DIGITS = 20;
const MAX_TEXT_SCALARS = 1600;
const MAX_TEXT_BYTES = 6400;
const LONG_PRESS_MS = 550;
const LONG_PRESS_MOVE_PX = 10;
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
			composeOpen: false,
			conversationNumber: null,
			conversationReturnFolder: null,
			conversationReturnPageCount: 1,
			conversationReturnDraft: null,
			conversationDrafts: Object.create(null),
			selectionMode: false,
			selectedSmsIds: Object.create(null),
			selectionFolder: null,
			selectionGeneration: null,
			selectionMessagingGeneration: null,
			suppressCardClickSmsId: null,
			suppressCardClickUntil: 0,
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
			pageCount: 1,
			loadedFolder: 'all',
			loadingMore: false,
			preparingDeleteAll: false,
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

function activeFolder(state) {
	return state.conversationNumber ? 'all' : state.folder;
}

function loadSmsPages(summary, state, folderOverride) {
	let page = 0;
	let cursor = '';
	let first = null;
	let duplicate = false;
	const messages = [];
	const seen = Object.create(null);
	const folder = typeof folderOverride === 'string' ? folderOverride : state.folder;
	const strictPaging = state.strictPaging === true;

	function next() {
		return api.listSms(summary.modem_id, folder, SMS_LIMIT, cursor)
			.catch(transportResult)
			.then(function(result) {
				const error = widgets.smsError(result, summary);

				if (error && page > 0 && widgets.object(result.error).code === 'stale_cursor') {
					if (strictPaging) {
						return {
							messages: result,
							error: _('The SMS inventory changed while preparing the complete folder. Refresh and try again.')
						};
					}
					state.pageCount = 1;
					state.result = {
						kind: 'warning',
						message: _('The SMS inventory changed while paging. Older pages were reset; use Load more again if needed.')
					};
					return {
						messages: Object.assign({}, first, {
							messages: messages,
							next_cursor: first.next_cursor,
							has_more: first.has_more
						}),
						error: null
					};
				}
				if (error)
					return { messages: result, error: error };
				if (first && (result.generation !== first.generation ||
					result.messaging_generation !== first.messaging_generation ||
					result.revision !== first.revision)) {
					return {
						messages: result,
						error: _('The SMS inventory changed while loading pages. Refresh and try again.')
					};
				}
				if (!first)
					first = result;
				if (messages.length + result.messages.length > SMS_CACHE_MAX) {
					return {
						messages: result,
						error: _('The paginated SMS response exceeded the 1,024-message safety bound.')
					};
				}
				result.messages.forEach(function(message) {
					if (widgets.isObject(message) && typeof message.sms_id === 'string' &&
					    !seen[message.sms_id]) {
						seen[message.sms_id] = true;
						messages.push(message);
					}
					else if (widgets.isObject(message) && typeof message.sms_id === 'string') {
						duplicate = true;
					}
				});
				if (strictPaging && duplicate) {
					return {
						messages: result,
						error: _('The complete SMS inventory contained a duplicate message identity.')
					};
				}
				page++;
				cursor = result.next_cursor;
				if (page < state.pageCount && result.has_more)
					return next();
				return {
					messages: Object.assign({}, first, {
						messages: messages,
						next_cursor: cursor,
						has_more: result.has_more
					}),
					error: null
				};
			});
	}

	return next();
}

function loadSnapshots() {
	return api.listModems().then(function(listResult) {
		if (widgets.listError(listResult))
			return { list: listResult, entries: [] };

		const summaries = widgets.modems(listResult);

		pruneDeviceStates(summaries);

		return Promise.all(summaries.map(function(summary) {
			const state = stateFor(summary.modem_id);

			const folder = activeFolder(state);

			if (state.loadedFolder !== folder) {
				state.loadedFolder = folder;
				state.pageCount = 1;
			}
			return loadSmsPages(summary, state, folder).then(function(loaded) {
					return {
						summary: summary,
						messages: loaded.messages,
						messagesError: loaded.error
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

function messageStateLabel(state) {
	switch (String(state || '').toLowerCase()) {
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

function legacyCopyText(value) {
	return new Promise(function(resolve, reject) {
		if (typeof document === 'undefined' || !document.body ||
		    typeof document.createElement !== 'function' ||
		    typeof document.execCommand !== 'function') {
			reject(new Error(_('Clipboard access is unavailable.')));
			return;
		}

		const buffer = document.createElement('textarea');
		let copied = false;

		buffer.className = 'fibocom-copy-buffer';
		buffer.value = value;
		buffer.setAttribute('readonly', '');
		document.body.appendChild(buffer);
		buffer.select();
		buffer.setSelectionRange(0, value.length);
		try {
			copied = document.execCommand('copy') === true;
		}
		finally {
			buffer.remove();
		}

		if (copied)
			resolve();
		else
			reject(new Error(_('Clipboard access was denied.')));
	});
}

function copyText(value) {
	if (typeof navigator !== 'undefined' && navigator.clipboard &&
	    typeof navigator.clipboard.writeText === 'function') {
		try {
			return Promise.resolve(navigator.clipboard.writeText(value)).catch(function() {
				return legacyCopyText(value);
			});
		}
		catch (error) {
			return legacyCopyText(value);
		}
	}
	return legacyCopyText(value);
}

function copySmsNumber(value) {
	return copyText(value).then(function() {
		ui.addNotification(null, E('p', {}, [ _('Number copied.') ]), 'info');
	}).catch(function() {
		ui.addNotification(null, E('p', {}, [
			_('Unable to copy the number. Select it manually instead.')
		]), 'warning');
	});
}

function copyableMessage(value) {
	const output = [];
	const pattern = /\+?[0-9]+(?:[.,:/-][0-9]+)*/g;
	let offset = 0;
	let match;

	while ((match = pattern.exec(value)) !== null) {
		if (match.index > offset)
			output.push(value.slice(offset, match.index));
		const number = match[0];

		output.push(E('button', {
			'class': 'fibocom-sms-number fibocom-sms-interactive',
			'type': 'button',
			'title': _('Copy number'),
			'aria-label': _('Copy number'),
			'pointerdown': function(event) {
				if (event && typeof event.stopPropagation === 'function')
					event.stopPropagation();
			},
			'click': function(event) {
				if (event && typeof event.preventDefault === 'function')
					event.preventDefault();
				if (event && typeof event.stopPropagation === 'function')
					event.stopPropagation();
				return copySmsNumber(number);
			}
		}, [ number ]));
		offset = pattern.lastIndex;
	}

	if (offset < value.length)
		output.push(value.slice(offset));
	return output.length ? output : [ value ];
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

function retrySafetyNotice(messages) {
	const capacity = Number.isSafeInteger(messages && messages.dedupe_capacity) ?
		messages.dedupe_capacity : 64;
	const seconds = Number.isSafeInteger(messages && messages.dedupe_window_seconds) ?
		messages.dedupe_window_seconds : 300;

	return _('The bridge retains at most %d recent request tokens for up to %d seconds. Heavy request volume can evict an older token sooner; after eviction or a bridge restart, do not assume a retry is duplicate-safe.')
		.format(capacity, seconds);
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

	if (entry.messagesError || widgets.smsError(messages, summary, 1024) ||
	    !widgets.mutationAllowed(messages, messages,
		summary.modem_id, summary.generation) ||
	    !Number.isSafeInteger(messages.messaging_generation))
		return null;

	return {
		modemId: summary.modem_id,
		generation: messages.generation,
		messagingGeneration: messages.messaging_generation
	};
}

function isMutationBusy(state) {
	return state.sending || state.preparingDeleteAll ||
		Object.keys(state.deleting).length > 0;
}

function statusPanel(result) {
	if (!result)
		return null;

	return E('div', { 'class': 'alert-message ' + result.kind }, [ result.message ]);
}

function clearSelection(state) {
	state.selectionMode = false;
	state.selectedSmsIds = Object.create(null);
	state.selectionFolder = null;
	state.selectionGeneration = null;
	state.selectionMessagingGeneration = null;
}

function selectedIds(state) {
	return Object.keys(state.selectedSmsIds);
}

function selectionEpochMatches(state, entry) {
	const messages = widgets.object(entry.messages);

	return state.selectionFolder === activeFolder(state) &&
		state.selectionGeneration === messages.generation &&
		state.selectionMessagingGeneration === messages.messaging_generation;
}

function bindSelectionEpoch(state, entry) {
	const messages = widgets.object(entry.messages);

	state.selectionFolder = activeFolder(state);
	state.selectionGeneration = messages.generation;
	state.selectionMessagingGeneration = messages.messaging_generation;
}

function pruneSelection(state, entry, messages) {
	const available = Object.create(null);

	if (state.selectionMode && !selectionEpochMatches(state, entry)) {
		clearSelection(state);
		return;
	}

	messages.forEach(function(message) {
		if (typeof message.sms_id === 'string')
			available[message.sms_id] = true;
	});

	selectedIds(state).forEach(function(smsId) {
		if (!available[smsId])
			delete state.selectedSmsIds[smsId];
	});

	if (!selectedIds(state).length)
		state.selectionMode = false;
}

function selectMessage(controller, entry, state, smsId) {
	if (!smsId || isMutationBusy(state))
		return;

	if (!state.selectionMode)
		bindSelectionEpoch(state, entry);
	state.selectionMode = true;
	state.selectedSmsIds[smsId] = true;
	controller.redraw();
}

function toggleMessageSelection(controller, entry, state, smsId) {
	if (!smsId || isMutationBusy(state))
		return;

	if (!state.selectionMode)
		bindSelectionEpoch(state, entry);
	if (state.selectedSmsIds[smsId])
		delete state.selectedSmsIds[smsId];
	else
		state.selectedSmsIds[smsId] = true;

	state.selectionMode = selectedIds(state).length > 0;
	controller.redraw();
}

function messageNumber(message) {
	return typeof message.number === 'string' && message.number.length ?
		message.number : null;
}

function messagesForView(messages, state) {
	if (!state.conversationNumber)
		return messages;

	return messages.filter(function(message) {
		return messageNumber(message) === state.conversationNumber;
	});
}

const DRAFT_FIELDS = [
	'composeOpen', 'recipient', 'text', 'clientToken', 'tokenRecipient',
	'tokenText', 'tokenModemId', 'tokenGeneration', 'tokenMessagingGeneration'
];

function captureDraft(state) {
	const draft = {};

	DRAFT_FIELDS.forEach(function(field) {
		draft[field] = state[field];
	});
	return draft;
}

function restoreDraft(state, draft) {
	DRAFT_FIELDS.forEach(function(field) {
		state[field] = draft && Object.prototype.hasOwnProperty.call(draft, field) ?
			draft[field] : null;
	});
	state.composeOpen = draft ? draft.composeOpen === true : false;
	state.recipient = draft && typeof draft.recipient === 'string' ? draft.recipient : '';
	state.text = draft && typeof draft.text === 'string' ? draft.text : '';
}

function newConversationDraft(number) {
	const canReply = /^\+?[0-9]{1,20}$/.test(number);

	return {
		composeOpen: canReply,
		recipient: canReply ? number : '',
		text: '',
		clientToken: null,
		tokenRecipient: null,
		tokenText: null,
		tokenModemId: null,
		tokenGeneration: null,
		tokenMessagingGeneration: null
	};
}

function openConversation(controller, state, message) {
	const number = messageNumber(message);

	if (!number || state.selectionMode || isMutationBusy(state))
		return Promise.resolve();

	clearSelection(state);
	state.conversationReturnDraft = captureDraft(state);
	state.conversationReturnPageCount = state.pageCount;
	state.conversationNumber = number;
	state.conversationReturnFolder = state.folder;
	state.loadedFolder = 'all';
	state.pageCount = 1;
	restoreDraft(state, state.conversationDrafts[number] || newConversationDraft(number));
	state.result = null;
	controller.redraw();
	return controller.refresh(true);
}

function closeConversation(controller, state) {
	const folder = state.conversationReturnFolder || 'all';
	const number = state.conversationNumber;

	clearSelection(state);
	if (number)
		state.conversationDrafts[number] = captureDraft(state);
	state.conversationNumber = null;
	state.conversationReturnFolder = null;
	state.folder = folder;
	state.loadedFolder = folder;
	state.pageCount = state.conversationReturnPageCount || 1;
	state.conversationReturnPageCount = 1;
	restoreDraft(state, state.conversationReturnDraft);
	state.conversationReturnDraft = null;
	state.result = null;
	return controller.refresh(true);
}

function isCardChildControl(target) {
	return target && typeof target.closest === 'function' &&
		target.closest('button, input, textarea, select, option, a, summary, details');
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

		if (!error && (!widgets.mutationAllowed(result, result,
			context.modemId, context.generation) ||
			result.messaging_generation !== context.messagingGeneration ||
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
					_('SMS send was not confirmed: %s. Review the message list before retrying. Retry without editing to reuse the same request token.').format(error) + ' ' + retrySafetyNotice(entry.messages) :
					_('Unable to send SMS: %s. Edit the recipient or text to start a new request, or retry unchanged with the same request token.').format(error) + ' ' + retrySafetyNotice(entry.messages)
			};
			controller.redraw();
			return null;
		}

		clearRetryToken(state);
		state.recipient = state.conversationNumber &&
			/^\+?[0-9]{1,20}$/.test(state.conversationNumber) ?
			state.conversationNumber : '';
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
				' ' + retrySafetyNotice(entry.messages)
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

		if (!error && (!widgets.mutationAllowed(result, result,
			context.modemId, context.generation) ||
			result.messaging_generation !== context.messagingGeneration ||
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

		delete state.selectedSmsIds[smsId];
		if (!selectedIds(state).length)
			clearSelection(state);
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

function uniqueSmsIds(messages) {
	const seen = Object.create(null);
	const ids = [];

	messages.forEach(function(message) {
		const smsId = widgets.isObject(message) && typeof message.sms_id === 'string' ?
			message.sms_id : null;

		if (smsId && !seen[smsId]) {
			seen[smsId] = true;
			ids.push(smsId);
		}
	});

	return ids;
}

function bulkDeleteError(result, context, smsId) {
	let error = widgets.responseError(result);

	if (!error && (!widgets.mutationAllowed(result, result,
		context.modemId, context.generation) ||
		result.messaging_generation !== context.messagingGeneration ||
		result.sms_id !== smsId || result.deleted !== true))
		error = _('The bridge returned an incomplete SMS delete response.');

	return error;
}

function performBulkDelete(controller, entry, state, smsIds) {
	const context = mutationContext(entry);
	const queue = smsIds.slice();
	const startedFromSelection = state.selectionMode;
	let index = 0;
	let deleted = 0;
	let alreadyAbsent = 0;

	if (!context) {
		state.result = {
			kind: 'danger',
			message: _('The modem changed or its messaging generation is unavailable. Refresh before deleting.')
		};
		controller.redraw();
		return Promise.resolve();
	}

	queue.forEach(function(smsId) {
		state.deleting[smsId] = true;
	});
	state.result = {
		kind: 'notice',
		message: _('Deleting %d SMS messages one at a time...').format(queue.length)
	};
	controller.redraw();

	function finishWithError(message) {
		queue.forEach(function(smsId) {
			delete state.deleting[smsId];
		});
		if (!startedFromSelection) {
			clearSelection(state);
			bindSelectionEpoch(state, entry);
			queue.slice(index).forEach(function(smsId) {
				state.selectedSmsIds[smsId] = true;
			});
			state.pageCount = MAX_SMS_PAGES;
		}
		state.selectionMode = selectedIds(state).length > 0;
		state.result = {
			kind: deleted || alreadyAbsent ? 'warning' : 'danger',
			message: _('Bulk deletion stopped: %d deleted, %d already absent, %d not attempted. The current result was not confirmed: %s')
				.format(deleted, alreadyAbsent, queue.length - index - 1, message)
		};
		return controller.refresh(true);
	}

	function next() {
		if (index >= queue.length) {
			queue.forEach(function(smsId) {
				delete state.deleting[smsId];
			});
			clearSelection(state);
			state.result = {
				kind: 'success',
				message: alreadyAbsent ?
					_('%d SMS messages deleted; %d were already absent.')
						.format(deleted, alreadyAbsent) :
					_('%d SMS messages deleted.').format(deleted)
			};
			return controller.refresh(true);
		}

		const smsId = queue[index];

		return api.deleteSms(
			context.modemId,
			context.generation,
			context.messagingGeneration,
			smsId,
			true
		).then(function(result) {
			const error = bulkDeleteError(result, context, smsId);
			const code = widgets.object(widgets.object(result).error).code;

			if (error && code !== 'not_found')
				return finishWithError(error);

			delete state.deleting[smsId];
			delete state.selectedSmsIds[smsId];
			if (code === 'not_found')
				alreadyAbsent++;
			else
				deleted++;
			index++;
			state.result = {
				kind: 'notice',
				message: _('Deleting SMS messages: %d of %d complete...')
					.format(index, queue.length)
			};
			controller.redraw();
			return next();
		}).catch(function(error) {
			return finishWithError(
				_('Deletion could not be confirmed because the RPC connection failed: %s')
					.format(widgets.display(error && error.message, _('Unknown transport error')))
			);
		});
	}

	return next();
}

function confirmBulkDelete(controller, entry, state, smsIds, deleteAll) {
	if (isMutationBusy(state) || !smsIds.length)
		return;

	const title = deleteAll ? _('Delete all SMS') : _('Delete selected SMS');
	const question = deleteAll ?
		_('Delete all %d SMS messages in this view permanently? This action cannot be undone.')
			.format(smsIds.length) :
		_('Delete the %d selected SMS messages permanently? This action cannot be undone.')
			.format(smsIds.length);

	ui.showModal(title, [
		E('p', {}, [ question ]),
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
					return performBulkDelete(controller, entry, state, smsIds);
				}
			}, [ _('Delete') ])
		])
	]);
}

function prepareDeleteAll(controller, entry, state) {
	if (isMutationBusy(state))
		return Promise.resolve();
	if (widgets.object(entry.messages).cache_truncated === true ||
	    widgets.object(entry.messages).cache_state === 'ready-truncated') {
		state.result = {
			kind: 'warning',
			message: _('Delete all is unavailable because the SMS inventory exceeds the 1,024-message safety bound.')
		};
		controller.redraw();
		return Promise.resolve();
	}

	const shadowState = {
		folder: activeFolder(state),
		pageCount: MAX_SMS_PAGES,
		strictPaging: true,
		result: null
	};

	state.preparingDeleteAll = true;
	state.result = { kind: 'notice', message: _('Loading the complete SMS list...') };
	controller.redraw();

	return loadSmsPages(entry.summary, shadowState, activeFolder(state)).then(function(loaded) {
		state.preparingDeleteAll = false;
		if (loaded.error) {
			state.result = {
				kind: 'warning',
				message: _('Unable to load all SMS messages before deletion: %s')
					.format(loaded.error)
			};
			controller.redraw();
			return null;
		}

		const result = widgets.object(loaded.messages);
		const allMessages = Array.isArray(result.messages) ?
			result.messages.filter(function(message) { return widgets.isObject(message); }) : [];

		if (result.has_more === true || result.cache_truncated === true ||
		    result.cache_state === 'ready-truncated') {
			state.result = {
				kind: 'warning',
				message: _('Delete all is unavailable because the SMS inventory exceeds the 1,024-message safety bound.')
			};
			controller.redraw();
			return null;
		}

		const ids = uniqueSmsIds(messagesForView(allMessages, state));

		state.result = null;
		controller.redraw();
		if (!ids.length) {
			state.result = { kind: 'notice', message: _('There are no deletable SMS messages in this view.') };
			controller.redraw();
			return null;
		}

		confirmBulkDelete(controller, entry, state, ids, true);
		return null;
	}).catch(function(error) {
		state.preparingDeleteAll = false;
		state.result = {
			kind: 'warning',
			message: _('Unable to load all SMS messages before deletion: %s')
				.format(widgets.display(error && error.message, _('Unknown transport error')))
		};
		controller.redraw();
		return null;
	});
}

function loadMore(controller, entry, state) {
	if (state.loadingMore || isMutationBusy(state) || state.pageCount >= MAX_SMS_PAGES)
		return Promise.resolve();
	state.loadingMore = true;
	state.pageCount++;
	state.result = { kind: 'notice', message: _('Loading older SMS messages…') };
	controller.redraw();
	return controller.refresh(true).then(function(snapshot) {
		const updated = snapshot && snapshot.entries && snapshot.entries.find(function(candidate) {
			return candidate.summary.modem_id === entry.summary.modem_id;
		});

		state.loadingMore = false;
		if (!updated || updated.messagesError) {
			state.pageCount = Math.max(1, state.pageCount - 1);
			state.result = {
				kind: 'warning',
				message: updated && updated.messagesError ? updated.messagesError :
					_('Unable to load the next SMS page.')
			};
		}
		else {
			state.result = {
				kind: 'success',
				message: _('Older SMS messages loaded.')
			};
		}
		controller.redraw();
		return snapshot;
	});
}

function trashIcon() {
	return E('span', {
		'class': 'fibocom-sms-trash-glyph',
		'aria-hidden': 'true'
	}, []);
}

function renderMessage(controller, entry, state, message) {
	const direction = scalar(message.direction, 'unknown');
	const numberLabel = String(direction).toLowerCase() === 'report' ? _('Number') :
		(isIncoming(direction) ? _('From') : _('To'));
	const number = messageNumber(message);
	const smsId = typeof message.sms_id === 'string' ? message.sms_id : null;
	const text = typeof message.text === 'string' ? message.text : '';
	const hasBinaryData = message.has_binary_data === true;
	const body = text || (hasBinaryData ? _('Binary SMS payload (not displayed)') :
		_('Empty text message'));
	const busy = isMutationBusy(state);
	const selected = smsId ? state.selectedSmsIds[smsId] === true : false;
	const cardClasses = [ 'cbi-section-node', 'fibocom-sms-card' ];
	let pressTimer = null;
	let pressX = null;
	let pressY = null;
	const rows = [
		[ _('State'), widgets.badge(messageStateLabel(message.state), message.state) ],
		[ numberLabel, scalar(message.number) ],
		[ _('Timestamp'), scalar(message.timestamp) ]
	];

	if (number)
		cardClasses.push('fibocom-sms-card-clickable');
	if (selected)
		cardClasses.push('fibocom-sms-card-selected', 'is-selected');
	if (state.selectionMode)
		cardClasses.push('fibocom-sms-select-mode');
	if (state.conversationNumber)
		cardClasses.push(isIncoming(direction) ?
			'fibocom-sms-chat-inbound' : 'fibocom-sms-chat-outbound');

	function cancelLongPress() {
		if (pressTimer !== null && typeof window !== 'undefined') {
			window.clearTimeout(pressTimer);
			if (controller && typeof controller.untrackLongPress === 'function')
				controller.untrackLongPress(pressTimer);
		}
		pressTimer = null;
		pressX = null;
		pressY = null;
	}

	function beginLongPress(event) {
		if (!smsId || busy || isCardChildControl(event && event.target) ||
			(event && typeof event.button === 'number' && event.button !== 0) ||
			typeof window === 'undefined')
			return;

		cancelLongPress();
		pressX = event && typeof event.clientX === 'number' ? event.clientX : null;
		pressY = event && typeof event.clientY === 'number' ? event.clientY : null;
		pressTimer = window.setTimeout(function() {
			if (controller && typeof controller.untrackLongPress === 'function')
				controller.untrackLongPress(pressTimer);
			pressTimer = null;
			state.suppressCardClickSmsId = smsId;
			state.suppressCardClickUntil = Date.now() + 1000;
			selectMessage(controller, entry, state, smsId);
		}, LONG_PRESS_MS);
		if (controller && typeof controller.trackLongPress === 'function')
			controller.trackLongPress(pressTimer);
	}

	function moveLongPress(event) {
		if (pressTimer === null || pressX === null || pressY === null || !event)
			return;
		if (typeof event.clientX === 'number' && typeof event.clientY === 'number' &&
		    (Math.abs(event.clientX - pressX) > LONG_PRESS_MOVE_PX ||
		     Math.abs(event.clientY - pressY) > LONG_PRESS_MOVE_PX))
			cancelLongPress();
	}

	function activateCard(event) {
		cancelLongPress();
		if (isCardChildControl(event && event.target))
			return;

		if (smsId && state.suppressCardClickSmsId === smsId &&
			Date.now() <= state.suppressCardClickUntil) {
			state.suppressCardClickSmsId = null;
			state.suppressCardClickUntil = 0;
			if (event && typeof event.preventDefault === 'function')
				event.preventDefault();
			return;
		}

		state.suppressCardClickSmsId = null;
		state.suppressCardClickUntil = 0;
		if (state.selectionMode) {
			toggleMessageSelection(controller, entry, state, smsId);
			return;
		}
		if (state.conversationNumber)
			return;

		return openConversation(controller, state, message);
	}

	function activateCardFromKeyboard(event) {
		if (!event || [ 'Enter', ' ', 'Escape' ].indexOf(event.key) === -1 ||
			isCardChildControl(event.target))
			return;

		event.preventDefault();
		if (event.key === 'Escape') {
			if (state.selectionMode) {
				clearSelection(state);
				controller.redraw();
			}
			else if (state.conversationNumber) {
				return closeConversation(controller, state);
			}
		}
		else if (event.key === ' ' || state.selectionMode) {
			toggleMessageSelection(controller, entry, state, smsId);
		}
		else if (!state.conversationNumber) {
			return openConversation(controller, state, message);
		}
	}

	return E('div', {
		'class': cardClasses.join(' '),
		'role': number || smsId ? 'button' : null,
		'tabindex': number || smsId ? '0' : null,
		'aria-pressed': state.selectionMode && smsId ? (selected ? 'true' : 'false') : null,
		'aria-selected': state.selectionMode && smsId ? (selected ? 'true' : 'false') : null,
		'aria-label': state.selectionMode ?
			(selected ? _('Selected SMS; tap to deselect') : _('SMS; tap to select')) :
			(state.conversationNumber ? _('SMS; hold to select') :
				(number ? _('Open conversation with %s').format(number) : null)),
		'pointerdown': beginLongPress,
		'pointermove': moveLongPress,
		'pointerup': cancelLongPress,
		'pointercancel': cancelLongPress,
		'pointerleave': cancelLongPress,
		'click': activateCard,
		'keydown': activateCardFromKeyboard,
		'contextmenu': function(event) {
			if (!isCardChildControl(event && event.target) && event &&
			    typeof event.preventDefault === 'function')
				event.preventDefault();
		}
	}, [
		E('div', { 'class': 'fibocom-sms-card-tools' }, [
			selected ? E('span', {
				'class': 'fibocom-sms-selection-check',
				'title': _('Selected'),
				'aria-label': _('Selected')
			}, [ '\u2713' ]) : E([]),
			smsId ? E('button', {
				'class': 'btn cbi-button cbi-button-negative fibocom-sms-delete-icon fibocom-sms-interactive',
				'type': 'button',
				'title': _('Delete SMS'),
				'aria-label': state.deleting[smsId] ? _('Deleting SMS') : _('Delete SMS'),
				'disabled': busy ? '' : null,
				'pointerdown': function(event) {
					if (event && typeof event.stopPropagation === 'function')
						event.stopPropagation();
				},
				'click': function(event) {
					if (event && typeof event.preventDefault === 'function')
						event.preventDefault();
					if (event && typeof event.stopPropagation === 'function')
						event.stopPropagation();
					confirmDelete(controller, entry, state, smsId);
				}
			}, [ state.deleting[smsId] ? E('span', {
				'class': 'fibocom-sms-delete-progress',
				'aria-hidden': 'true'
			}, [ '\u2026' ]) : trashIcon() ]) : E([])
		]),
		widgets.keyValueList(rows),
		E('div', { 'class': 'fibocom-sms-body' }, [
			E('div', { 'class': 'fibocom-sms-body-title' }, [ _('Message'), ':' ]),
			E('div', { 'class': 'fibocom-sms-message' }, copyableMessage(body))
		]),
		message.text_truncated === true && isIncoming(direction) ?
			E('div', { 'class': 'alert-message warning' }, [
				_('This received message was truncated by the bridge safety limit.')
			]) : E([]),
		hasBinaryData ? E('div', { 'class': 'alert-message notice' }, [
			_('This SMS contains binary data. Its raw payload is intentionally not exposed or displayed.')
		]) : E([])
	]);
}

function renderCompose(controller, entry, state, index) {
	const busy = isMutationBusy(state);
	const recipientId = 'fibocom-sms-recipient-' + index;
	const textId = 'fibocom-sms-text-' + index;
	const children = [
		E('div', { 'class': 'cbi-value fibocom-form-row' }, [
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
		E('div', { 'class': 'cbi-value fibocom-form-row' }, [
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
		E('div', { 'class': 'cbi-page-actions fibocom-actions' }, [
			E('button', {
				'class': 'btn cbi-button cbi-button-positive important',
				'type': 'submit',
				'disabled': busy ? '' : null
			}, [ state.sending ? _('Sending…') :
				(state.clientToken ? _('Retry SMS send') : _('Send SMS')) ])
		])
	];

	return E('details', {
		'class': 'cbi-section fibocom-panel fibocom-compose',
		'open': state.composeOpen ? '' : null,
		'toggle': function(event) {
			state.composeOpen = event.target.open === true;
		}
	}, [
		E('summary', { 'class': 'fibocom-compose-summary' }, [ _('Write SMS') ]),
		E('form', {
			'class': 'fibocom-compose-form',
			'submit': function(event) {
				return handleSend(controller, entry, state, event);
			}
		}, children)
	]);
}

function renderConversationHeader(controller, state, messages) {
	return E('div', { 'class': 'cbi-section fibocom-panel fibocom-conversation-header' }, [
		E('button', {
			'class': 'btn cbi-button cbi-button-neutral fibocom-conversation-back',
			'type': 'button',
			'disabled': isMutationBusy(state) ? '' : null,
			'click': function() { return closeConversation(controller, state); }
		}, [ _('Back') ]),
		E('div', { 'class': 'fibocom-conversation-heading' }, [
			E('div', { 'class': 'fibocom-conversation-title' }, [ _('Conversation') ]),
			E('div', { 'class': 'fibocom-conversation-number' }, [ state.conversationNumber ])
		]),
		E('span', { 'class': 'label fibocom-conversation-count' }, [
			_('%d messages').format(messages.length)
		])
	]);
}

function renderMessageActions(controller, entry, state, messages, hasMore) {
	const selected = selectedIds(state);
	const busy = isMutationBusy(state);
	const children = [
		E('button', {
			'class': 'btn cbi-button cbi-button-negative fibocom-sms-bulk-delete',
			'type': 'button',
			'disabled': busy || !messages.length ? '' : null,
			'click': function() {
				if (state.selectionMode)
					confirmBulkDelete(controller, entry, state, selected.slice(), false);
				else
					return prepareDeleteAll(controller, entry, state);
			}
		}, [ state.preparingDeleteAll ? _('Loading...') :
			(state.selectionMode ? _('Delete') : _('Delete all')) ])
	];

	if (state.selectionMode) {
		children.push(E('span', {
			'class': 'fibocom-sms-selected-count',
			'aria-live': 'polite'
		}, [ _('%d selected').format(selected.length) ]));
		children.push(E('button', {
			'class': 'btn cbi-button cbi-button-neutral fibocom-sms-selection-cancel',
			'type': 'button',
			'disabled': busy ? '' : null,
			'click': function() {
				clearSelection(state);
				controller.redraw();
			}
		}, [ _('Cancel') ]));
	}

	if (hasMore && state.pageCount < MAX_SMS_PAGES) {
		children.push(E('button', {
			'class': 'btn cbi-button cbi-button-neutral fibocom-sms-load-more',
			'type': 'button',
			'disabled': state.loadingMore || busy ? '' : null,
			'click': function() { return loadMore(controller, entry, state); }
		}, [ state.loadingMore ? _('Loading...') : _('Load more') ]));
	}

	return E('div', { 'class': 'cbi-page-actions fibocom-sms-list-actions' }, children);
}

function renderDevice(controller, entry, index) {
	const summary = widgets.object(entry.summary);
	const messages = widgets.object(entry.messages);
	const state = stateFor(summary.modem_id);
	const error = entry.messagesError || widgets.smsError(entry.messages, summary, 1024);
	const title = widgets.display(summary.model, _('Fibocom modem'));
	const children = [ E('h3', { 'class': 'fibocom-device-title' }, [ title ]) ];

	if (error) {
		children.push(widgets.errorPanel(error));
		return E('div', { 'class': 'cbi-section fibocom-device' }, children);
	}

	const list = Array.isArray(messages.messages) ? messages.messages.filter(function(message) {
		return widgets.isObject(message);
	}) : [];
	const filtered = messagesForView(list, state);
	const visible = state.conversationNumber ? filtered.slice().reverse() : filtered;
	const filterId = 'fibocom-sms-folder-' + index;
	const status = statusPanel(state.result);

	pruneSelection(state, entry, filtered);
	if (status)
		children.push(status);
	if (state.conversationNumber)
		children.push(renderConversationHeader(controller, state, filtered));
	else
		children.push(E('div', { 'class': 'cbi-section fibocom-panel' }, [
		E('div', { 'class': 'cbi-value fibocom-form-row' }, [
			E('label', {
				'class': 'cbi-value-title',
				'for': filterId
			}, [ _('Folder') ]),
			E('div', { 'class': 'cbi-value-field' }, [
				E('select', {
					'id': filterId,
					'class': 'cbi-input-select',
					'change': function(event) {
						clearSelection(state);
						state.folder = event.target.value;
						state.loadedFolder = state.folder;
						state.pageCount = 1;
						state.result = null;
						return controller.refresh(true);
					}
				}, folderOptions(state.folder))
			])
		]),
		widgets.keyValueList([
			[ _('Messaging cache'), widgets.badge(
				widgets.display(messages.cache_state, _('Unknown')), messages.cache_state) ],
			[ _('Loaded messages'), list.length ]
		])
	]));

	if (!state.conversationNumber || /^\+?[0-9]{1,20}$/.test(state.conversationNumber))
		children.push(renderCompose(controller, entry, state, index));
	else
		children.push(E('div', { 'class': 'alert-message notice' }, [
			_('This sender cannot be used as an SMS recipient.')
		]));
	children.push(E('div', { 'class': 'cbi-section fibocom-panel' }, [
		E('div', { 'class': 'fibocom-sms-panel-heading' }, [
			E('h4', { 'class': 'fibocom-panel-title' }, [
				state.conversationNumber ? _('Conversation') : _('Messages')
			]),
			!state.selectionMode ? E('span', {
				'class': 'cbi-value-description fibocom-sms-select-hint'
			}, [ _('Hold a message to select it.') ]) : E([])
		]),
		visible.length ? E('div', {
			'class': 'fibocom-sms-list' +
				(state.conversationNumber ? ' fibocom-sms-chat-list' : '')
		}, visible.map(function(message) {
			return renderMessage(controller, entry, state, message);
		})) : E('div', { 'class': 'alert-message notice' }, [
			state.conversationNumber ? _('No messages with this number are loaded.') :
				_('No SMS messages are available in this folder.')
		]),
		renderMessageActions(controller, entry, state, visible, messages.has_more === true)
	]));

	return E('div', { 'class': 'cbi-section fibocom-device' }, children);
}

function renderSnapshots(snapshot, controller) {
	const error = widgets.listError(snapshot.list);

	if (error)
		return widgets.errorPanel(error);

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
	const compose = active && typeof active.closest === 'function' ?
		active.closest('.fibocom-compose') : null;

	return content.contains(active) && content.contains(compose) &&
		[ 'input', 'textarea' ].indexOf(tag) !== -1;
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
			longPressTimers: [],
			trackLongPress: function(timer) {
				this.longPressTimers.push(timer);
			},
			untrackLongPress: function(timer) {
				this.longPressTimers = this.longPressTimers.filter(function(candidate) {
					return candidate !== timer;
				});
			},
			cancelLongPresses: function() {
				if (typeof window !== 'undefined' && typeof window.clearTimeout === 'function') {
					this.longPressTimers.forEach(function(timer) {
						window.clearTimeout(timer);
					});
				}
				this.longPressTimers = [];
			},
			redraw: function() {
				this.redrawPending = false;
				this.cancelLongPresses();
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
			},
			'keydown': function(event) {
				if (!event || event.key !== 'Escape')
					return;
				const states = Object.keys(deviceStates).map(function(modemId) {
					return deviceStates[modemId];
				});
				const active = states.find(function(state) {
					return state.selectionMode || state.conversationNumber;
				});

				if (!active)
					return;
				event.preventDefault();
				if (active.selectionMode) {
					clearSelection(active);
					controller.redraw();
				}
				else {
					return closeConversation(controller, active);
				}
			}
		}, [
			renderSnapshots(snapshot, controller)
		]);

		poll.add(function() {
			return controller.refresh(false);
		}, 10);

		return E('div', { 'class': 'cbi-map fibocom-page fibocom-sms-page' }, [
			widgets.stylesheet(),
			E('h2', {}, [ _('SMS') ]),
			E('div', { 'class': 'cbi-map-descr' }, [
				_('Messages are read, sent, and deleted through ModemManager.')
			]),
			controller.content
		]);
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
