// SPDX-FileCopyrightText: 2026 As Tsaqib
// SPDX-License-Identifier: Apache-2.0

'use strict';
'require view';

return view.extend({
	render: function() {
		return E('div', { 'class': 'cbi-map' }, [
			E('h2', {}, [ _('Fibocom Modem Settings') ]),
			E('div', { 'class': 'alert-message warning' }, [
				E('strong', {}, [ _('Settings are read-only during shadow mode.') ]),
				' ',
				_('The discovery service currently observes hardware only and cannot change radio, SIM, USB composition, or bearer state.')
			]),
			E('div', { 'class': 'cbi-section' }, [
				E('h3', {}, [ _('Connection configuration') ]),
				E('p', {}, [
					_('APN, authentication, IP family, DNS, metric, and roaming policy belong to one logical netifd interface. They are not duplicated in the Fibocom management configuration.')
				]),
				E('p', {}, [
					_('When the fibocom network protocol is enabled, create or edit the connection under Network Interfaces. Runtime port names, addresses, routes, DNS servers, and session identifiers are never stored here.')
				]),
				E('p', {}, [
					E('a', {
						'class': 'cbi-button cbi-button-action',
						'href': L.url('admin/network/network')
					}, [ _('Open Network Interfaces') ])
				])
			]),
			E('div', { 'class': 'cbi-section' }, [
				E('h3', {}, [ _('Ownership and migration') ]),
				E('p', {}, [
					_('Keep the existing dialer as the sole modem owner while shadow-mode evidence is collected. Do not enable two dialers for the same physical modem.')
				]),
				E('p', {}, [
					_('Radio controls, USB mode switching, reboot, band policy, SIM operations, and eSIM lifecycle controls are deliberately absent from this phase.')
				])
			])
		]);
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
