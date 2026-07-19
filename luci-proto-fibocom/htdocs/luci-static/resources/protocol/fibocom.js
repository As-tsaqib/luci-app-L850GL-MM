// SPDX-FileCopyrightText: 2026 As Tsaqib
// SPDX-License-Identifier: Apache-2.0

'use strict';
'require form';
'require network';
'require rpc';
'require uci';

const callFibocomList = rpc.declare({
	object: 'fibocom',
	method: 'list',
	expect: { devices: [] }
});

network.registerPatternVirtual(/^fibocom-.+$/);
network.registerErrorCode('SHADOW_MODE',
	_('Fibocom support is running in read-only shadow mode; dialing is disabled.'));

function deviceLabel(device) {
	const parts = [ device.device_id ];

	if (device.display_name || device.model || device.profile)
		parts.push(device.display_name || device.model || device.profile);
	if (device.composition)
		parts.push(device.composition.toUpperCase());
	if (device.topology_status || device.state)
		parts.push(device.topology_status || device.state);

	return parts.join(' — ');
}

return network.registerProtocol('fibocom', {
	getI18n: function() {
		return _('Fibocom cellular');
	},

	getIfname: function() {
		return this._ubus('l3_device') || 'fibocom-%s'.format(this.sid);
	},

	getPackageName: function() {
		return 'fibocom-netifd';
	},

	isFloating: function() {
		return true;
	},

	isVirtual: function() {
		return true;
	},

	getDevices: function() {
		return null;
	},

	containsDevice: function(ifname) {
		return network.getIfnameOf(ifname) == this.getIfname();
	},

	renderFormOptions: function(section) {
		let option;

		option = section.taboption('general', form.DummyValue, '_shadow_notice');
		option.renderWidget = function() {
			return E('div', { 'class': 'alert-message warning' }, [
				E('strong', {}, _('Shadow mode only.')), ' ',
				_('This build can inventory a Fibocom L850-GL, but it deliberately cannot dial or change modem state. Saving this interface will not provide connectivity.')
			]);
		};

		option = section.taboption('general', form.ListValue, '_fibocom_modem',
			_('Modem'),
			_('Stable logical identity reported by fibocomd; runtime /dev names are never stored.'));
		option.ucioption = 'device_id';
		option.rmempty = false;
		option.load = function(sectionId) {
			const current = uci.get('network', sectionId, 'device_id');

			if (current)
				this.value(current, '%s (%s)'.format(current, _('not currently detected')));

			return callFibocomList().catch(function() {
				return [];
			}).then(L.bind(function(devices) {
				for (const device of Array.isArray(devices) ? devices : []) {
					if (device && device.device_id)
						this.value(device.device_id, deviceLabel(device));
				}

				if (!current && (!Array.isArray(devices) || devices.length == 0))
					this.value('', _('No supported Fibocom modem detected'));

				return form.ListValue.prototype.load.apply(this, [ sectionId ]);
			}, this));
		};

		option = section.taboption('general', form.Value, 'apn', _('APN'));
		option.validate = function(sectionId, value) {
			if (value == null || value == '')
				return true;
			if (!/^[a-zA-Z0-9.-]*[a-zA-Z0-9]$/.test(value))
				return _('Invalid APN provided');
			return true;
		};

		option = section.taboption('general', form.ListValue, 'auth',
			_('Authentication type'));
		option.value('none', _('None'));
		option.value('pap', 'PAP');
		option.value('chap', 'CHAP');
		option.value('both', _('PAP or CHAP'));
		option.default = 'none';

		option = section.taboption('general', form.Value, 'username',
			_('Username'));
		option.depends('auth', 'pap');
		option.depends('auth', 'chap');
		option.depends('auth', 'both');

		option = section.taboption('general', form.Value, 'password',
			_('Password'));
		option.password = true;
		option.depends('auth', 'pap');
		option.depends('auth', 'chap');
		option.depends('auth', 'both');

		option = section.taboption('general', form.ListValue, 'ip_family',
			_('IP family'));
		option.value('ipv4', _('IPv4'));
		option.default = 'ipv4';

		option = section.taboption('general', form.Flag, 'roaming',
			_('Allow roaming'));
		option.default = option.disabled;

		option = section.taboption('advanced', form.Flag, 'defaultroute',
			_('Use default gateway'),
			_('If unchecked, no default route is configured.'));
		option.default = option.enabled;

		option = section.taboption('advanced', form.Value, 'metric',
			_('Gateway metric'));
		option.datatype = 'uinteger';
		option.placeholder = '0';
		option.depends('defaultroute', '1');

		option = section.taboption('advanced', form.Flag, 'peerdns',
			_('Use DNS servers advertised by peer'));
		option.default = option.enabled;

		option = section.taboption('advanced', form.Value, 'mtu',
			_('Override MTU'));
		option.datatype = 'and(uinteger,min(576),max(9200))';
		option.rmempty = true;
	}
});
