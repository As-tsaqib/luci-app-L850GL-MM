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
			return Promise.all([
				api.getStatus(summary.modem_id).catch(transportResult),
				api.getCapabilities(summary.modem_id).catch(transportResult)
			]).then(function(results) {
				return { summary: summary, status: results[0], capabilities: results[1] };
			});
		});

		return Promise.all(requests).then(function(entries) {
			return { list: listResult, entries: entries };
		});
	}).catch(function(error) {
		return { list: transportResult(error), entries: [] };
	});
}

function section(title, content) {
	return E('div', { 'class': 'cbi-section' }, [ E('h4', {}, [ title ]), content ]);
}

function renderGeneral(entry, status) {
	const general = widgets.object(status.general);
	const state = general.state || entry.summary.state || 'unknown';

	return section(_('General'), widgets.keyValueTable([
		[ _('Manufacturer'), general.manufacturer || entry.summary.manufacturer ],
		[ _('Model'), general.model || entry.summary.model ],
		[ _('Revision'), general.revision ],
		[ _('ModemManager plugin'), general.plugin || entry.summary.plugin ],
		[ _('Drivers'), general.drivers ],
		[ _('Equipment identifier'), general.equipment_identifier || general.equipment_id ],
		[ _('State'), widgets.badge(widgets.display(state), state) ],
		[ _('Power state'), general.power_state ],
		[ _('Failure reason'), general.failure_reason ],
		[ _('Generation'), status.generation ],
		[ _('Snapshot generated'), status.generated_at ]
	], _('No general modem information was reported.')));
}

function renderPorts(status) {
	return section(_('Ports'), widgets.table([
		_('Role'), _('Name'), _('Type'), _('Primary')
	], widgets.portRows(status), _('No display-safe ports were reported.')));
}

function renderSim(status) {
	const sim = widgets.object(status.sim);
	const children = [
		widgets.keyValueTable([
			[ _('SIM present'), sim.present ],
			[ _('Snapshot state'), sim.cache_state ],
			[ _('Primary SIM slot'), sim.primary_slot || sim.slot ],
			[ _('SIM active'), sim.active ],
			[ _('ICCID'), sim.iccid ],
			[ _('IMSI'), sim.imsi ],
			[ _('Operator'), sim.operator || sim.operator_name ],
			[ _('Operator code'), sim.operator_code || sim.operator_id ],
			[ _('SIM lock'), sim.lock || sim.locks ]
		], _('No SIM information was reported.'))
	];
	const slots = widgets.simSlotRows(sim);

	if (slots.length) {
		children.push(E('h5', {}, [ _('Physical SIM slots') ]));
		children.push(widgets.table([
			_('Slot'), _('Present'), _('Active'), _('ICCID'), _('IMSI'), _('Operator')
		], slots));
	}

	return section(_('SIM'), E('div', {}, children));
}

function renderNetwork(status) {
	const network = widgets.object(status.network);
	const registration = network.registration || network.registration_state || 'unknown';

	return section(_('Mobile network'), widgets.keyValueTable([
		[ _('Registration'), widgets.badge(widgets.display(registration), registration) ],
		[ _('Operator'), network.operator || network.operator_name ],
		[ _('Operator code'), network.operator_code ],
		[ _('Roaming'), network.roaming ],
		[ _('Packet service'), network.packet_service ],
		[ _('Access technology'), network.access || network.access_technologies ]
	], _('No mobile-network information was reported.')));
}

function renderSignal(status) {
	const signal = widgets.object(status.signal);
	const metrics = widgets.signalRows(signal);
	const children = [
		widgets.keyValueTable([
			[ _('Signal quality'), signal.quality != null ? widgets.progress(signal.quality) : null ],
			[ _('Signal is recent'), signal.recent ]
		], _('No signal summary was reported.'))
	];

	if (metrics.length) {
		children.push(widgets.table([
			_('Technology'), _('Metric'), _('Value')
		], metrics));
	}

	return section(_('Signal'), E('div', {}, children));
}

function renderCell(status) {
	const cell = widgets.object(status.cell);
	const state = cell.state || (cell.supported === false ? 'unsupported' : null);
	const cells = widgets.cellRows(cell);
	const children = [
		widgets.keyValueTable([
			[ _('State'), state ? widgets.badge(widgets.display(state), state) : null ],
			[ _('Reason'), cell.reason ]
		], _('No standard cell-information state was reported.'))
	];

	if (cells.length) {
		children.push(widgets.table([
			_('Cell'), _('Operator'), _('TAC/LAC'), _('Cell ID'), _('PCI'),
			_('Frequency'), _('RSRP'), _('RSRQ')
		], cells));
	}

	return section(_('Cell information'), E('div', {}, children));
}

function renderBearers(status) {
	return section(_('Data bearers'), E('div', {}, [
		widgets.keyValueTable([
			[ _('Snapshot state'), status.bearer_cache_state ]
		]),
		widgets.table([
			_('Connected'), _('Interface'), _('IP families'), _('Addresses'), _('Gateways'), _('DNS'), _('MTU')
		], widgets.bearerRows(status.bearers), _('No bearer information was reported.'))
	]));
}

function renderOpenWrt(status) {
	const openwrt = widgets.object(status.openwrt);
	const counters = widgets.object(openwrt.counters);

	return section(_('OpenWrt network'), widgets.keyValueTable([
		[ _('State'), openwrt.state ],
		[ _('Reason'), openwrt.reason ],
		[ _('Logical interface'), openwrt.network || openwrt.interface ],
		[ _('Interface up'), openwrt.up ],
		[ _('Available'), openwrt.available ],
		[ _('Uptime'), openwrt.uptime ],
		[ _('Received bytes'), openwrt.rx_bytes || counters.rx_bytes ],
		[ _('Transmitted bytes'), openwrt.tx_bytes || counters.tx_bytes ],
		[ _('Error'), openwrt.error ]
	], _('No matching netifd interface was reported.')));
}

function renderDiagnostics(status) {
	const diagnostics = widgets.object(status.diagnostics);
	const dependencies = widgets.dependencyRows({ dependencies: diagnostics.dependencies });
	const children = [
		widgets.keyValueTable([
			[ _('Bridge version'), diagnostics.bridge_version ],
			[ _('ModemManager version'), diagnostics.modemmanager_version ],
			[ _('Read-only milestone'), diagnostics.read_only ],
			[ _('Raw D-Bus paths exposed'), diagnostics.raw_dbus_paths_exposed ],
			[ _('AT commands enabled'), diagnostics.at_commands_enabled ],
			[ _('Netifd protocol available'), diagnostics.netifd_proto ],
			[ _('Fibocom plugin available'), diagnostics.fibocom_plugin ],
			[ _('MBIM available'), diagnostics.mbim ],
			[ _('Last error code'), diagnostics.last_error_code ],
			[ _('Last error reason'), diagnostics.last_error_reason ]
		], _('No diagnostic summary was reported.'))
	];

	if (dependencies.length) {
		children.push(widgets.table([
			_('Component'), _('State'), _('Version'), _('Reason')
		], dependencies));
	}

	return section(_('Dependencies'), E('div', {}, children));
}

function renderCapabilities(result) {
	const error = widgets.responseError(result);

	if (error)
		return section(_('Capabilities'), widgets.errorPanel({ transport_error: error }));

	return section(_('Capabilities'), widgets.table([
		_('Feature'), _('State'), _('Mutable'), _('Reason')
	], widgets.capabilityRows(result), _('No capabilities were reported.')));
}

function renderDevice(entry) {
	const error = widgets.responseError(entry.status);
	const summary = widgets.object(entry.summary);
	const title = widgets.display(summary.model, _('Fibocom modem'));

	if (error) {
		return E('div', { 'class': 'cbi-section' }, [
			E('h3', {}, [ title ]),
			widgets.errorPanel({ transport_error: error }),
			renderCapabilities(entry.capabilities)
		]);
	}

	const status = widgets.object(entry.status);
	const notice = widgets.schemaNotice(status);
	const children = [ E('h3', {}, [ widgets.display(widgets.object(status.general).model, title) ]) ];

	if (notice)
		children.push(notice);

	children.push(renderGeneral(entry, status));
	children.push(renderPorts(status));
	children.push(renderSim(status));
	children.push(renderNetwork(status));
	children.push(renderSignal(status));
	children.push(renderCell(status));
	children.push(renderBearers(status));
	children.push(renderOpenWrt(status));
	children.push(renderDiagnostics(status));
	children.push(renderCapabilities(entry.capabilities));

	return E('div', { 'class': 'cbi-section' }, children);
}

function renderSnapshots(snapshot) {
	const error = widgets.responseError(snapshot.list);

	if (error)
		return widgets.errorPanel({ transport_error: error });

	if (!snapshot.entries.length) {
		return E('div', { 'class': 'alert-message notice' }, [
			_('No Fibocom modem is currently exported by ModemManager.')
		]);
	}

	return E('div', {}, snapshot.entries.map(renderDevice));
}

return view.extend({
	load: loadSnapshots,

	render: function(snapshot) {
		const content = E('div', { 'id': 'fibocom-status' }, [ renderSnapshots(snapshot) ]);

		poll.add(function() {
			return loadSnapshots().then(function(next) {
				dom.content(content, renderSnapshots(next));
			});
		}, 10);

		return E('div', { 'class': 'cbi-map' }, [
			E('h2', {}, [ _('Fibocom Modem Status') ]),
			E('div', { 'class': 'cbi-map-descr' }, [
				_('Detailed modem values are normalized from ModemManager. The matching network configuration is read safely from UCI; live netifd counters remain explicitly unavailable until runtime correlation is implemented. This page does not execute modem commands.')
			]),
			content
		]);
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
