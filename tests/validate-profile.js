// SPDX-FileCopyrightText: 2026 As Tsaqib
// SPDX-License-Identifier: Apache-2.0

'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const profilePath = path.join(
	root,
	'fibocomd/files/usr/share/fibocom/profiles/l850-gl.json'
);

if (!fs.existsSync(profilePath)) {
	console.error('profile missing:', profilePath);
	process.exit(1);
}

const profile = JSON.parse(fs.readFileSync(profilePath, 'utf8'));
const hex4 = /^[0-9a-f]{4}$/;
const iface = /^[0-9a-f]{2}$/;
const identifier = /^[a-z0-9][a-z0-9-]{2,63}$/;
const capabilityStates = new Set([
	'supported',
	'planned',
	'mbim-only-planned',
	'hardware-validation-required',
	'probe-required',
	'experimental-disabled',
	'unavailable'
]);

assert.strictEqual(profile.schema, 1, 'schema must be exactly 1');
assert.ok(identifier.test(profile.id), 'profile id must be normalized');
assert.ok(typeof profile.display_name === 'string' && profile.display_name,
	'display_name is required');
assert.ok(Array.isArray(profile.match?.models_exact) &&
	profile.match.models_exact.length > 0, 'exact model list is required');
assert.ok(Array.isArray(profile.match?.usb) &&
	profile.match.usb.length > 0, 'USB match list is required');

const usbKeys = new Set();
for (const match of profile.match.usb) {
	assert.ok(hex4.test(match.vid), 'VID must be lowercase four-digit hex');
	assert.ok(hex4.test(match.pid), 'PID must be lowercase four-digit hex');
	assert.ok([ 'mbim', 'ncm' ].includes(match.composition),
		'composition must be mbim or ncm');
	const key = `${match.vid}:${match.pid}:${match.composition}`;
	assert.ok(!usbKeys.has(key), `duplicate USB match: ${key}`);
	usbKeys.add(key);
}

for (const [ composition, portMap ] of Object.entries(profile.ports || {})) {
	assert.ok([ 'mbim', 'ncm' ].includes(composition),
		`unknown port map: ${composition}`);
	assert.ok(iface.test(portMap.at_primary),
		`${composition} primary AT interface is invalid`);
	for (const key of [ 'ignored', 'data_candidates' ]) {
		if (portMap[key] == null)
			continue;
		assert.ok(Array.isArray(portMap[key]), `${key} must be an array`);
		for (const number of portMap[key])
			assert.ok(iface.test(number), `invalid interface number: ${number}`);
	}
}

if (profile.ncm) {
	assert.ok(Number.isInteger(profile.ncm.session_cid) &&
		profile.ncm.session_cid >= 0 && profile.ncm.session_cid <= 16,
		'NCM session CID must be a bounded integer');
}

for (const [ feature, state ] of Object.entries(profile.capabilities || {}))
	assert.ok(capabilityStates.has(state),
		`unsupported capability state for ${feature}: ${state}`);

const serialized = JSON.stringify(profile);
for (const forbidden of [ 'command', 'executable', 'script', 'device_path' ])
	assert.ok(!new RegExp(`"${forbidden}"\\s*:`).test(serialized),
		`profile must not define ${forbidden}`);

console.log('profile validation passed');
