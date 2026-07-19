// SPDX-FileCopyrightText: 2026 As Tsaqib
// SPDX-License-Identifier: Apache-2.0

'use strict';
'require view';

return view.extend({
	render: function() {
		return E('div', { 'class': 'cbi-map' }, [
			E('h2', {}, [ _('Fibocom Modem Settings') ]),
			E('div', { 'class': 'cbi-map-descr' }, [
				_('Connection settings are owned by netifd and the ModemManager protocol. They are intentionally not duplicated here.')
			]),
			E('div', { 'class': 'cbi-section' }, [
				E('h3', {}, [ _('Configure the data connection') ]),
				E('ol', {}, [
					E('li', {}, [ _('Open Network Interfaces and add a new interface, or edit the existing cellular interface.') ]),
					E('li', {}, [ _('Choose ModemManager as the protocol and select the Fibocom modem exposed by ModemManager.') ]),
					E('li', {}, [ _('Set the APN and, only when required by the provider, authentication, PIN, roaming, and IP-family options.') ]),
					E('li', {}, [ _('Save and apply the network interface. Netifd will own automatic connection, routes, and DNS.') ])
				]),
				E('p', {}, [
					E('a', {
						'class': 'cbi-button cbi-button-action',
						'href': L.url('admin/network/network')
					}, [ _('Open Network Interfaces') ])
				])
			]),
			E('div', { 'class': 'cbi-section' }, [
				E('h3', {}, [ _('Connection ownership') ]),
				E('p', {}, [
					_('ModemManager discovers the modem and manages radio, SIM, bearer, and SMS objects. The netifd ModemManager protocol applies persistent connection intent and publishes the resulting OpenWrt interface state.')
				]),
				E('p', {}, [
					_('Do not configure a second dialer for the same modem. This companion interface does not create, connect, disconnect, or delete bearers.')
				]),
				E('p', {}, [
					E('a', {
						'class': 'cbi-button',
						'href': L.url('admin/status/modemmanager')
					}, [ _('Open ModemManager status') ])
				])
			]),
			E('div', { 'class': 'alert-message notice' }, [
				_('The Advanced page provides capability-gated immediate radio actions. Persistent APN, allowed-mode, preferred-mode, and connection intent remain owned by the ModemManager network interface; optional eSIM management remains a separate application.')
			])
		]);
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
