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
		if (widgets.responseError(listResult))
			return { list: listResult, entries: [] };

		const requests = widgets.modems(listResult).filter(function(summary) {
			return widgets.isObject(summary) && typeof summary.modem_id === 'string';
		}).map(function(summary) {
			return api.getOverview(summary.modem_id).catch(transportResult).then(function(overview) {
				return { summary: summary, overview: overview };
			});
		});

		return Promise.all(requests).then(function(entries) {
			return { list: listResult, entries: entries };
		});
	}).catch(function(error) {
		return { list: transportResult(error), entries: [] };
	});
}

function renderDevice(entry) {
	const error = widgets.responseError(entry.overview);
	const summary = widgets.object(entry.summary);

	if (error) {
		return E('div', { 'class': 'cbi-section' }, [
			E('h3', {}, [ widgets.display(summary.model, _('Fibocom modem')) ]),
			widgets.errorPanel({ transport_error: error })
		]);
	}

	const overview = widgets.object(entry.overview);
	const modem = widgets.object(overview.modem);
	const sim = widgets.object(overview.sim);
	const network = widgets.object(overview.network);
	const signal = widgets.object(overview.signal);
	const bearer = widgets.object(overview.bearer);
	const openwrt = widgets.object(overview.openwrt);
	const state = modem.state || summary.state || 'unknown';
	const registration = network.registration || 'unknown';
	const support = summary.supported === true ? 'supported' :
		(summary.supported === false ? 'unsupported' : 'unknown');
	const supportLabel = summary.supported === true ? _('Supported') :
		(summary.supported === false ? _('Unsupported') : _('Unknown'));
	const warning = widgets.warningList(overview.warnings);
	const children = [
		E('h3', {}, [ widgets.display(modem.model || summary.model, _('Fibocom modem')) ]),
		widgets.keyValueTable([
			[ _('Manufacturer'), summary.manufacturer ],
			[ _('Revision'), modem.revision ],
			[ _('ModemManager plugin'), summary.plugin ],
			[ _('USB composition'), summary.composition ],
			[ _('Support'), widgets.badge(supportLabel, support) ],
			[ _('Support reason'), summary.support_reason ],
			[ _('Modem state'), widgets.badge(widgets.display(state), state) ],
			[ _('Snapshot freshness'), widgets.badge(widgets.display(overview.freshness), overview.freshness) ],
			[ _('SIM present'), sim.present ],
			[ _('Primary SIM slot'), sim.slot ],
			[ _('SIM lock'), sim.lock ],
			[ _('Registration'), widgets.badge(widgets.display(registration), registration) ],
			[ _('Operator'), network.operator ],
			[ _('Access technology'), network.access ],
			[ _('Signal quality'), signal.quality != null ? widgets.progress(signal.quality) : null ],
			[ _('Signal is recent'), signal.recent ],
			[ _('Bearer connected'), bearer.connected ],
			[ _('Data interface'), bearer.interface ? E('code', {}, [ widgets.display(bearer.interface) ]) : null ],
			[ _('IP families'), bearer.ip_families ],
			[ _('OpenWrt interface'), openwrt.network ],
			[ _('OpenWrt interface up'), openwrt.up ],
			[ _('Last state change'), summary.last_changed_at ]
		], _('No overview fields are available for this modem.'))
	];

	if (warning)
		children.push(warning);

	return E('div', { 'class': 'cbi-section' }, children);
}

function renderSnapshots(snapshot) {
	const error = widgets.responseError(snapshot.list);

	if (error)
		return widgets.errorPanel({ transport_error: error });

	const children = [];
	const notice = widgets.schemaNotice(snapshot.list);
	const dependencies = widgets.dependencyRows(snapshot.list);

	if (notice)
		children.push(notice);

	if (dependencies.length) {
		children.push(E('div', { 'class': 'cbi-section' }, [
			E('h3', {}, [ _('Runtime dependencies') ]),
			widgets.table([
				_('Component'), _('State'), _('Version'), _('Reason')
			], dependencies)
		]));
	}

	if (!snapshot.entries.length) {
		children.push(E('div', { 'class': 'alert-message notice' }, [
			_('No Fibocom modem is currently exported by ModemManager.')
		]));
	}
	else
		snapshot.entries.forEach(function(entry) { children.push(renderDevice(entry)); });

	return E('div', {}, children);
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

		return E('div', { 'class': 'cbi-map' }, [
			E('h2', {}, [ _('Fibocom Modem Overview') ]),
			E('div', { 'class': 'cbi-map-descr' }, [
				_('ModemManager owns discovery and the data session. This page only reads normalized snapshots from the Fibocom bridge.')
			]),
			content
		]);
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
