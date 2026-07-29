// SPDX-FileCopyrightText: 2026 As Tsaqib
// SPDX-License-Identifier: Apache-2.0

'use strict';

const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const appRoot = path.join(root, 'luci-app-l850gl-mm');
const outputPath = path.join(appRoot, 'po/templates/l850gl-mm.pot');
const messages = new Map();

function filesUnder(directory) {
	return fs.readdirSync(directory, { withFileTypes: true }).flatMap(function(entry) {
		const child = path.join(directory, entry.name);

		return entry.isDirectory() ? filesUnder(child) : [ child ];
	});
}

function reference(file, offset, source) {
	const line = source.slice(0, offset).split('\n').length;

	return `${path.relative(appRoot, file).replaceAll(path.sep, '/')}:${line}`;
}

function add(message, sourceReference) {
	if (!message)
		return;
	if (!messages.has(message))
		messages.set(message, new Set());
	messages.get(message).add(sourceReference);
}

function decodeLiteral(literal) {
	return Function(`"use strict"; return '${literal}';`)();
}

filesUnder(path.join(appRoot, 'htdocs/luci-static/resources')).filter(function(file) {
	return file.endsWith('.js');
}).forEach(function(file) {
	const source = fs.readFileSync(file, 'utf8');
	const expression = /_\(\s*'((?:\\.|[^'\\])*)'\s*\)/gs;
	let match;

	while ((match = expression.exec(source)) !== null)
		add(decodeLiteral(match[1]), reference(file, match.index, source));
});

const menuFile = path.join(
	appRoot, 'root/usr/share/luci/menu.d/luci-app-l850gl-mm.json');
const menu = JSON.parse(fs.readFileSync(menuFile, 'utf8'));
Object.keys(menu).forEach(function(key) {
	if (typeof menu[key].title === 'string')
		add(menu[key].title, 'root/usr/share/luci/menu.d/luci-app-l850gl-mm.json');
});

function quote(value) {
	return `"${value.replaceAll('\\', '\\\\').replaceAll('"', '\\"')
		.replaceAll('\t', '\\t').replaceAll('\r', '\\r').replaceAll('\n', '\\n')}"`;
}

const lines = [
	'# SPDX-' + 'FileCopyrightText: 2026 As Tsaqib',
	'# SPDX-' + 'License-Identifier: Apache-2.0',
	'#',
	'msgid ""',
	'msgstr ""',
	'"Project-Id-Version: luci-app-L850GL-MM 0.6.0\\n"',
	'"Report-Msgid-Bugs-To: \\n"',
	'"POT-Creation-Date: 2026-07-27 00:00+0800\\n"',
	'"MIME-Version: 1.0\\n"',
	'"Content-Type: text/plain; charset=UTF-8\\n"',
	'"Content-Transfer-Encoding: 8bit\\n"',
	''
];

Array.from(messages.keys()).sort(function(left, right) {
	return left.localeCompare(right, 'en');
}).forEach(function(message) {
	lines.push('#: ' + Array.from(messages.get(message)).sort().join(' '));
	lines.push('msgid ' + quote(message));
	lines.push('msgstr ""');
	lines.push('');
});

const generated = lines.join('\n');
if (process.argv.includes('--check')) {
	if (!fs.existsSync(outputPath) || fs.readFileSync(outputPath, 'utf8') !== generated) {
		process.stderr.write('l850gl-mm.pot is not synchronized; run node tests/generate-pot.js\n');
		process.exit(1);
	}
}
else {
	fs.writeFileSync(outputPath, generated, 'utf8');
	process.stdout.write(`generated ${path.relative(root, outputPath)}\n`);
}
