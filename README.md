<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# luci-app-L850GL-MM

Repository: <https://github.com/As-tsaqib/luci-app-L850GL-MM>

Version 1.0.0 is a small LuCI companion for the L850-GL modem already managed by
ModemManager and OpenWrt netifd. Its public API is schema 4 and its menu is
exactly:

```text
Modem
`-- L850GL MM
    |-- Overview
    |-- Lock
    `-- SMS
```

Overview, Lock, and SMS share native LuCI `cbi-*` markup plus one scoped
responsive stylesheet. The browser renders one information tree at every
viewport width: phone layouts only reflow panels and actions, without hiding
status fields or maintaining a second mobile UI. Focus guards keep active Lock
and SMS inputs intact during polling.

ModemManager remains the only owner of modem objects, ports, SIMs, SMS, radio,
and bearers. netifd remains the owner of connection intent, APN, routes, and
DNS. The companion neither dials nor creates bearers, scans sysfs, opens modem
device nodes, or executes external programs. Its sole UCI write path changes
only `allowedmode` and `preferredmode` on the uniquely resolved
`proto modemmanager` interface; browser input never selects a UCI section.

## Product scope

Overview returns a compact, sanitized snapshot: manufacturer/model/revision,
USB composition, modem state and power, SIM presence and lock,
registration/operator/roaming, access technology, signal quality and available
RSRP/RSRQ/SINR, bearer state and data interface, current bands, and capability
summaries. At the product owner's explicit direction, schema 4 also exposes the
full IMEI and the ModemManager-provided SIM number, IMSI, and ICCID to users who
hold the authenticated Overview ACL. These bounded values are never logged or
stored in fixtures or evidence. Serving EARFCN/PCI is cached only from validated
standard CellInfo, an explicit expert scan, or a verified PCI postcondition.
Overview never launches an automatic XMCI scan. It never returns raw D-Bus or
sysfs paths, port tables, IP configuration, credentials, or diagnostic dumps.
On an expert build, a validated `GTCAINFO` primary carrier also supplies the
display-only serving EARFCN/PCI fallback when that standard cache is empty.
The same gated, asynchronous command path parses a fixed `AT+CBC` response into
a nullable modem-voltage value in millivolts. Invalid or unavailable voltage
data never hides the rest of Overview, and raw command output is never exposed.
LuCI resolves the carrier query before fetching Overview/Lock so the optional
voltage refresh cannot repeatedly take the carrier query's arbitration slot.

An expert build also adds read-only LTE carrier aggregation details to
Overview: active LTE bands, active-carrier count, and compact per-carrier
band/EARFCN/PCI details plus aggregate DL/UL bandwidth. This uses
only the fixed `AT+GTCAINFO?` query through asynchronous ModemManager command
arbitration. The typed parser accepts the L850 primary slot's 14-field grammar
and secondary slots' 10-field grammar, omits inactive sentinel slots, bounds
the result to eight declared slots/4,096 response bytes, and validates the
primary's paired DL/UL EARFCNs. An active secondary is accepted only in the
live-verified downlink-only shape: its own band/DL EARFCN and DL bandwidth are
valid, its UL bandwidth is sentinel `255`, and its UL EARFCN exactly repeats
the validated primary UL EARFCN. The API publishes that secondary UL bandwidth
as `null`; it never fabricates a value. Independent secondary uplink and active
secondary SINR code `127` is accepted only as an unavailable, non-exported
metric after every carrier-defining field passes. Other unexpected signal
sentinels, independent secondary uplink, and active B29/B32 shapes remain
fail-closed unless their response shape passes the bounded typed parser.
The API never returns the raw response or cellular subscriber/location fields.
A base build has no expert object and renders these details unavailable.

Lock contains:

- Persistent allowed/preferred mode selection (`3g`, `4g`, or `3g|4g`) owned
  and activated by netifd. The bridge resolves the exact bound interface
  internally, commits only the two mode options, verifies readback, and asks
  the `network` ubus service to reload asynchronously.
- Band Lock through asynchronous libmm-glib `SetCurrentBands`, including
  automatic `["any"]` selection, supported-band and allowed-family validation,
  exact L850-GL MBIM `2cb7:0007` attestation, confirmation, WAN warning,
  generation checks, timeout, cooldown, and single-flight mutation handling.
- A build-gated L850 PCI/EARFCN section. The base binary does not contain or
  publish `l850gl.mm.l850`. An explicit expert build first tries standard
  ModemManager `GetCellInfo`, normalizes at most 64 LTE cells, and validates
  PCI and EARFCN against live supported bands. When standard CellInfo returns
  `Core.Unsupported`, the bridge tries its fixed XMCI query and accepts it only
  when the bounded typed response validates. Scans are single-flight and have
  a five-second cooldown measured from completion. Set/clear use fixed typed
  tuples and require two matching NVM reads one second apart before the single
  fixed `CFUN=15`. The coordinator then performs exact hardware-slot reprobe
  correlation, registration wait, bounded post-reset NVM polling, and
  serving-cell verification. Each mutation starts with a read-only NVM
  protocol probe; unrecognized command grammars fail closed before any write.

SMS uses ModemManager Messaging/Sms only. The cache consumes Added, Deleted,
and property-change signals, reconciles every 30 seconds, keeps the newest
1,024 entries, and supports Inbox, Outbox, Draft, Unknown, and All. LuCI polls
every 10 seconds and follows opaque cursor pages past the backend's 100-message
per-call limit. Send uses Messaging.Create followed by Sms.Send; delete uses
Messaging.Delete and requires confirmation. Binary payloads and raw PDUs are
never exported. Numeric tokens in either direction are directly copyable. A
normal tap opens an in-tab exact-number conversation, while a long press
selects one or more cards for confirmed sequential deletion; `Delete all`
first freezes a complete, stable snapshot of the active folder.

Send retry tokens are CSPRNG values bound to a digest of recipient and body.
The in-memory dedupe cache holds at most 64 retained tokens; each retained
token expires 300 seconds after its most recent stored state, but an older
token may be evicted sooner when capacity is exceeded. It is a short retry aid,
not exactly-once delivery and does not survive a bridge restart. An uncertain
post-dispatch send returns `outcome_unknown` and is never resent automatically.

## API and permissions

The base object `l850gl.mm` exposes exactly eight schema-4 methods:

```text
list_modems
get_overview
get_lock_status
set_bands
set_modes
list_sms
send_sms
delete_sms
```

The expert object, when explicitly compiled, exposes only:

```text
cell_scan
get_carrier_info
cell_lock_status
set_cell_lock
clear_cell_lock
```

The five rpcd groups are `luci-app-l850gl-mm-overview`,
`luci-app-l850gl-mm-sms-read`, `luci-app-l850gl-mm-sms-write`,
`luci-app-l850gl-mm-lock-band`, and
`luci-app-l850gl-mm-lock-pci-expert`. They grant exact ubus methods only: no
wildcards, filesystem access, shell execution, cgi-io, or UCI writes.

LuCI treats malformed responses and every schema other than 4 as a
compatibility failure. All mutations are disabled until a matching schema,
opaque ID, modem generation, and (for SMS) messaging generation are present.

## Packaging

Version 1.0.0 packages the accepted r6 behavior with a distinguishable expert
ModemManager identity:

```text
l850gl-mm-bridge               1.0.0-r3 expert libmm-glib/GDBus to ubus bridge
luci-app-l850gl-mm             1.0.0-r3 Overview, Lock, and SMS views
modemmanager-l850gl-expert     matching upstream OpenWrt recipe with reviewed AT transport
```

The retired Status, old Advanced, Settings, radio toggle, generic reset,
primary SIM-slot switch, and old eSIM addon are not part of 1.0.0. APN,
route, DNS, credentials, and all connection settings remain in OpenWrt's
existing Network / Interfaces UI.

The v1.0.0 release publishes ten target-specific expert bundles and no base
release asset:

| OpenWrt | ModemManager expert | Format | Architectures |
|---|---|---|---|
| 24.10.8 | 1.22.0-r20 | IPK | `aarch64_cortex-a53`, `aarch64_generic`, `arm_cortex-a7_neon-vfpv4`, `mipsel_24kc`, `x86_64` |
| 25.12.5 | 1.24.0-r10 | APK | `aarch64_cortex-a53`, `aarch64_generic`, `arm_cortex-a7_neon-vfpv4`, `mipsel_24kc`, `x86_64` |

Each ZIP is for exactly one OpenWrt version and package architecture. It
contains `modemmanager-l850gl-expert`, the matching expert bridge and LuCI
package, `SHA256SUMS`, build metadata, and target-specific installation and
rollback instructions. Do not mix packages from different bundles.

CI still builds and verifies a safe base binary with generic AT-over-D-Bus
disabled, but that artifact is verification-only. Every published bundle is
expert-only and contains `modemmanager-l850gl-expert`, which provides and
conflicts with the stock `modemmanager` package. Follow its `INSTALL.txt`: the
stock package's world constraint must be removed before installing all three
local packages together. Its fixed command surface remains firmware-,
hardware-, ACL-, and build-gated.

## Evidence status

Version 1.0.0 keeps API schema 4 and packages the accepted modem state
machines under the final identities. Release r2 replaced the firmware revision
comparison with exact hardware attestation plus bounded runtime protocol
probing. Release r3 additionally handles an observed reset-completion variant:
an unclassified `CFUN=15` command failure is not accepted as success or treated
as a terminal result. It enters the same bounded hardware-slot reprobe and must
still prove replacement, registration, NVM, and (for set) serving-cell
postconditions. Known permission, policy, busy, and unsupported failures remain
terminal, and no write or reset is resent. The release gate requires each
three-package expert ZIP and checksum manifest to pass its target SDK build and
package checks. Live hardware results remain scoped to the dated target and
are not generalized to compile-only targets.

For r3 source `c08f8fc`, static run `31129696815` and the exact OpenWrt
24.10.8 ARMv7 bundle run `31129696709` passed. The checksum-verified bridge and
LuCI pair was installed on `.27.16` with ModemManager preserved. A complete
HTTP `/ubus` exact-current-cell set returned `applied_verified`, its first
clear returned `cleared_verified`, and final NVM clear, registration, bearer,
carrier, modem-bound data path, served assets, and bridge logs passed. See the
dated live record for the sanitized boundary.

Follow-up source `7f42a55` passed static run `31131317605`; full release-bundle
run `31131317610` built and verified all ten configured OpenWrt 24.10.8/25.12.5
architecture combinations without a target failure. Those additional targets
are compile/package evidence, while the live r3 acceptance remains scoped to
the exact OpenWrt 24.10.8 ARMv7 router above.

Version 0.6.0 renames the active packages, service, ACLs, paths, and ubus
objects; the retired names are migration/history identifiers only and must not
coexist with the new pair. It also adds the strict expert-build `AT+CBC`
voltage parser/cache. To prevent a globally scheduled poll from making
validated CA information flicker while radio state is reconciling, LuCI may
retain one structurally valid carrier snapshot for at most 30 seconds across
canonical retryable `busy`, `rate_limited`, `not_ready`, `timeout`, or
`dependency_unavailable` responses. Identity-bearing responses must match the
same opaque modem ID and generation; `not_ready` and `timeout` are never
accepted without that identity. Expiry, generation change, malformed data,
schema mismatch, browser transport failure, or any non-retryable error still
clears it and fails closed. A later valid 3CA, 2CA, or 1CA response replaces
the cache without a page reload. For the previous `0.6.0-r1`, static run
`30416321185` and SDK run `30416321209` passed for source `4783633`; its
checksum-verified expert pair was installed on 2026-07-29 with exact 8/5 new
method tables and no retired object. That r1 read-only acceptance returned
`3550 mV`, stable ten-second effective Serving Cell fallback, matching served
assets, and a connected bearer. Release r2 added the active-secondary uplink
shape but live repetition exposed a second, metric-only SINR sentinel. Release
r3 handles that exact observation. Static run `30423022209` and SDK run
`30423022228` passed for source `a1047fc`; the checksum-verified pair was
installed with ModemManager preserved. Twenty spaced carrier queries all
returned `available` across one/three-carrier transitions, cooldown recovery
was exact, and the actual LuCI validator/renderer displayed B5+B3+B1 plus the
serving-cell fallback without an unavailable CA row. See the dated validation
document for hashes and bounded test details.

Release r4 fixes CA presentation cache eviction for the reviewed
retryable radio states and makes valid 3CA/2CA/1CA responses replace each other
without a page reload. It also updates the PCI Apply button directly on every
input event without a DOM redraw, bounds EARFCN to 0..70545, retains optional
PCI 0..503, and uses the concise Overview description. Static run
`30434665005` and SDK run `30434665450` passed for source `955a3d0`; the
checksum-verified r4 bridge/LuCI pair was installed while the byte-identical
ModemManager process was preserved. Runtime schema/method, served-asset,
cooldown, stale-identity, process-ownership, bearer, and repeated-carrier
checks passed without a live mutation.

Release r5 is a presentation-only hotfix: the compact loaded-message badge no
longer overrides the LuCI theme's foreground color, so its text remains
readable in both light and dark themes. A static regression prevents a future
theme-contrast override. Backend behavior, API schema 4, and modem mutations
are unchanged.

Release r6 hardens PCI set/clear persistence around the single reset. It
requires two consecutive exact pre-reset NVM observations, polls a valid
post-reset mismatch for a bounded window, reports the failing verification
stage, and never resends the write or reset. Its pure policy tests model
delayed commit, streak reset, malformed input, and post-reset convergence.
Static run `30457585212` and SDK run `30457585130` passed for `6ce14aa`; the
checksum-verified pair was installed with ModemManager preserved. Three live
sets and all three first clear attempts verified successfully. An isolated
`CFUN=1,1` comparator also passed three cycles and proved modem-object
replacement without rebooting the router, but production retains the existing
fixed `CFUN=15` because the prior mismatch did not recur in any of the three r6
cycles and no second reset strategy was needed. Final NVM was clear and modem,
bearer, and netifd were connected/healthy.

Live testing on 2026-07-27 validated the fixed grammar and recovery matrix on
an L850-GL MBIM `2cb7:0007` running firmware
`18500.5001.00.05.27.30`: sanitized XMCI scan, exact current-cell lock,
exact neighbor-cell lock, EARFCN-only lock, clear, `CFUN=15` removal/reprobe,
registration, bearer recovery, NVM postconditions, and serving-cell changes.
The router was returned to clear/automatic state with its bearer connected.
That historical release allowed only that exact firmware. The current bridge
instead identifies compatible firmware from validated command responses. The
installed 0.3.0-r1 expert
packages were validated through schema-2 local ubus and an authorized
HTTP `/ubus` rpcd session: XMCI fallback, exact current-cell set, replacement identity,
stale-identity rejection, clear, NVM/registration/serving-cell postconditions,
and final connected-bearer recovery all passed. No live SMS send/delete was
performed. On 2026-07-28, static run `30314503929` and SDK run `30314503962`
built schema-3 v0.4 artifacts from `06eb8df`; their checksums passed locally and
on the router. Installed v0.4 validation proved combined/4G-only mode
persistence and restoration, LTE-only Band Lock with hidden UTRAN preservation,
explicit-scan Serving Cell cache, schema-3 SMS read metadata, and connected
netifd recovery. No v0.4 SMS write or PCI mutation was run. See
[hardware evidence](docs/hardware-evidence.md) and the
[live record](docs/live-router-validation.md).

Before the 0.5.0 implementation, an approved read-only query on 2026-07-28
confirmed the target firmware's `GTCAINFO` primary/secondary grammar: one
active B3 primary carrier at EARFCN 1325 / PCI 381 with 20 MHz bandwidth, plus
an inactive secondary sentinel. `GTUSBMODE` value 7 and ModemManager's live
composition independently agreed on MBIM. This is command-level input evidence,
not an installed schema-4 package claim. The later schema-4 packages from
static run `30365531206` and SDK run `30365531207` were checksum-verified,
installed as 0.5.0-r1, and accepted with one live B3 primary, exact 8/5 method
tables, responsive served assets, and a healthy connected bearer. See the
[live record](docs/live-router-validation.md).

On 2026-07-29 a later read-only live query captured active aggregation with a
B5 primary and B3/B1 downlink-only secondaries. Both secondaries repeated the
primary UL EARFCN and reported UL bandwidth sentinel `255`. This exposed an
over-strict r1 parser that rejected a valid modem response as
`malformed_response`; release r2 admitted only that exact cross-record shape.
Repeated installed-r2 testing then captured SINR code `127` on otherwise valid
active secondaries. Release r3 treats only that secondary SINR metric as
unavailable while keeping every unobserved structural or secondary-uplink form
fail-closed.

## Development

Run the available checks with:

```sh
make check
```

The suite covers package/API/menu/ACL contracts, JavaScript and JSON syntax,
CSPRNG IDs, hardware attestation, band policy, SMS policy and dedupe
eviction/expiry, malformed ubus blobs, and valid/invalid PCI parser fixtures.
The SDK workflow builds the base binary and an explicit expert variant
separately. It verifies that the base binary does not contain the expert object
name, carrier query, or voltage query and rebuilds/packages ModemManager with
reviewed AT-via-D-Bus transport only for the expert artifact.

Further design details are in [architecture](docs/architecture.md), the
[ubus API](docs/ubus-api.md), [threat model](docs/threat-model.md), and
[PCI design](docs/pci-cell-lock.md).

## Licensing

Backend and system integration code is GPL-2.0-or-later. LuCI JavaScript and
documentation are Apache-2.0. No QModem/XModem source is copied; every file is
covered by SPDX/REUSE metadata.
