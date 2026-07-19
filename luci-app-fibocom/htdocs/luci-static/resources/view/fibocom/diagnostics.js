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

function loadDiagnostics() {
	return Promise.all([
		api.list().catch(transportResult),
		api.diagnostics().catch(transportResult)
	]).then(function(results) {
		const listResult = results[0];
		const diagnosticResult = results[1];
		const diagnosticDevices = diagnosticResult != null &&
			Array.isArray(diagnosticResult.devices) ? diagnosticResult.devices : [];
		const summaries = widgets.devices(listResult).filter(function(summary) {
			return summary != null && typeof summary === 'object' && typeof summary.device_id === 'string';
		});

		return {
			list: listResult,
			daemon: diagnosticResult,
			devices: summaries.map(function(summary) {
				const match = diagnosticDevices.find(function(device) {
					return device != null && device.device_id === summary.device_id;
				});

				return {
					summary: summary,
					result: widgets.responseError(diagnosticResult) ? diagnosticResult : (match || summary)
				};
			})
		};
	});
}

function renderDaemon(result, count) {
	const error = widgets.responseError(result);

	if (error)
		return widgets.errorPanel({ transport_error: error });

	const record = widgets.diagnostics(result);
	const daemon = record.daemon != null && typeof record.daemon === 'object' ? record.daemon : record;
	const profile = record.profile != null && typeof record.profile === 'object' ? record.profile : {};
	const reconcile = record.reconcile != null && typeof record.reconcile === 'object' ?
		record.reconcile : (record.scan != null && typeof record.scan === 'object' ? record.scan : {});
	const notice = widgets.schemaNotice(result);
	const children = [];

	if (notice)
		children.push(notice);

	children.push(widgets.keyValueTable([
		[ _('API schema'), result.schema != null ? result.schema : daemon.schema ],
		[ _('Daemon mode'), daemon.mode || daemon.state || daemon.service_state ],
		[ _('Shadow mode'), result.shadow_mode != null ? result.shadow_mode : daemon.shadow_mode ],
		[ _('ubus connected'), daemon.ubus_connected ],
		[ _('Modem ownership'), daemon.ownership ],
		[ _('Opens TTY ports'), daemon.opens_tty ],
		[ _('Opens WDM ports'), daemon.opens_wdm ],
		[ _('Changes network state'), daemon.changes_network ],
		[ _('Detected devices'), daemon.device_count != null ? daemon.device_count :
			(reconcile.device_count != null ? reconcile.device_count : count) ],
		[ _('Last scan'), daemon.last_scan || daemon.last_scan_at || reconcile.completed_at ],
		[ _('Scan generation'), daemon.scan_generation || reconcile.scan_id ],
		[ _('Devices added'), reconcile.added ],
		[ _('Devices removed'), reconcile.removed ],
		[ _('Devices changed'), reconcile.changed ],
		[ _('Scan in progress'), reconcile.scan_in_progress ],
		[ _('Rescan pending'), daemon.rescan_pending != null ? daemon.rescan_pending : reconcile.scan_pending ],
		[ _('Last scan successful'), reconcile.ok ],
		[ _('Last error code'), daemon.last_error_code || (daemon.last_error && daemon.last_error.code) ||
			(reconcile.last_error && reconcile.last_error.code) ],
		[ _('Last error reason'), daemon.last_error_reason ||
			(typeof daemon.last_error === 'string' ? daemon.last_error :
				(daemon.last_error && daemon.last_error.reason)) ||
			(typeof reconcile.last_error === 'string' ? reconcile.last_error :
				(reconcile.last_error && reconcile.last_error.reason)) || reconcile.error ]
	], _('No daemon health fields were reported.')));

	const dependencies = widgets.dependencyRows(result);

	if (dependencies.length) {
		children.push(E('h4', {}, [ _('Dependencies') ]));
		children.push(widgets.table([
			_('Component'), _('State'), _('Version'), _('Reason')
		], dependencies));
	}

	if (Object.keys(profile).length) {
		children.push(E('h4', {}, [ _('Loaded hardware profile') ]));
		children.push(widgets.keyValueTable([
			[ _('Profile ID'), profile.id ],
			[ _('Display name'), profile.display_name ],
			[ _('Schema validated'), profile.schema_validated ],
			[ _('Hardware validated'), profile.hardware_validated ]
		]));
	}

	return E('div', {}, children);
}

function renderDevice(entry) {
	const error = widgets.responseError(entry.result);

	if (error) {
		return E('div', { 'class': 'cbi-section' }, [
			E('h3', {}, [ widgets.display(entry.summary.model || entry.summary.profile, _('Fibocom modem')) ]),
			widgets.errorPanel({ transport_error: error })
		]);
	}

	const diagnostics = widgets.diagnostics(entry.result);
	const topology = diagnostics.topology != null && typeof diagnostics.topology === 'object' ?
		diagnostics.topology : diagnostics;
	const record = Object.assign({}, entry.summary,
		diagnostics.device != null && typeof diagnostics.device === 'object' ? diagnostics.device : {}, topology);
	const owner = diagnostics.ownership != null && typeof diagnostics.ownership === 'object' ?
		diagnostics.ownership : diagnostics;
	const topologyState = widgets.display(record.topology_status, _('Unknown'));
	const children = [
		E('h3', {}, [ widgets.display(record.model || record.profile, _('Fibocom modem')) ]),
		widgets.keyValueTable([
			[ _('Device ID'), E('code', {}, [ widgets.display(record.device_id) ]) ],
			[ _('Generation'), record.generation ],
			[ _('Model'), record.model ],
			[ _('Physical USB path'), record.physical_path ?
				E('code', {}, [ widgets.display(record.physical_path) ]) : null ],
			[ _('Identity scope'), record.identity_scope ],
			[ _('USB vendor ID'), record.vid ? E('code', {}, [ widgets.display(record.vid) ]) : null ],
			[ _('USB product ID'), record.pid ? E('code', {}, [ widgets.display(record.pid) ]) : null ],
			[ _('Composition'), record.composition ],
			[ _('Topology'), widgets.badge(topologyState, topologyState) ],
			[ _('Topology reason'), record.topology_reason ],
			[ _('Current owner'), owner.current_owner || owner.owner ],
			[ _('Ownership conflict'), owner.conflicting_owner != null ? owner.conflicting_owner : owner.conflict ],
			[ _('Last error code'), diagnostics.last_error_code ||
				(diagnostics.last_error && diagnostics.last_error.code) ],
			[ _('Last error reason'), diagnostics.last_error_reason ||
				(diagnostics.last_error && diagnostics.last_error.reason) ]
		], _('No sanitized topology facts were reported.')),
		E('h4', {}, [ _('Port association') ]),
		widgets.table([
			_('Role'), _('Node'), _('USB interface'), _('Driver'), _('State')
		], widgets.portRows(topology), _('No sanitized port association was reported.'))
	];

	return E('div', { 'class': 'cbi-section' }, children);
}

function renderDiagnostics(snapshot) {
	const listError = widgets.responseError(snapshot.list);
	const sections = [
		E('div', { 'class': 'cbi-section' }, [
			E('h3', {}, [ _('Daemon health') ]),
			renderDaemon(snapshot.daemon, snapshot.devices.length)
		])
	];

	if (listError)
		sections.push(widgets.errorPanel({ transport_error: listError }));
	else if (!snapshot.devices.length)
		sections.push(E('div', { 'class': 'alert-message notice' }, [
			_('There is no device topology to diagnose.')
		]));
	else
		snapshot.devices.forEach(function(entry) { sections.push(renderDevice(entry)); });

	return E('div', {}, sections);
}

return view.extend({
	load: loadDiagnostics,

	render: function(snapshot) {
		const diagnostics = E('div', { 'id': 'fibocom-diagnostics' }, [
			renderDiagnostics(snapshot)
		]);

		poll.add(function() {
			return loadDiagnostics().then(function(next) {
				dom.content(diagnostics, renderDiagnostics(next));
			}).catch(function(error) {
				dom.content(diagnostics, widgets.errorPanel(error));
			});
		}, 10);

		return E('div', { 'class': 'cbi-map' }, [
			E('h2', {}, [ _('Fibocom Diagnostics') ]),
			E('div', { 'class': 'cbi-map-descr' }, [
				_('Only sanitized cached health and topology fields are shown. Raw commands, modem identifiers, secrets, and arbitrary files are never requested by this page.')
			]),
			diagnostics
		]);
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
