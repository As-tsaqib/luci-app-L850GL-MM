<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# luci-app-L850GL-MM

Repository: <https://github.com/As-tsaqib/luci-app-L850GL-MM>

Version 0.6.0 is a small LuCI companion for the L850-GL modem already managed by
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
Overview: active LTE bands, primary and secondary LTE bands, active-carrier
count, and per-carrier EARFCN, PCI, and bandwidth. This uses
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
fail-closed until captured on the allowlisted firmware.
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
  PCI and EARFCN against live supported bands. For the single allowlisted
  firmware `18500.5001.00.05.27.30`, `Core.Unsupported` falls back to a fixed
  XMCI query through ModemManager. Scans are single-flight per modem and have
  a five-second cooldown measured from completion. Set/clear use fixed typed tuples, `CFUN=15`,
  exact hardware-slot reprobe correlation, registration wait, NVM verification,
  and serving-cell verification. Every other firmware fails closed.

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

The current source targets the following r3 package metadata; installed r3
acceptance is recorded only after its CI artifacts are deployed:

```text
l850gl-mm-bridge   0.6.0-r3 native libmm-glib/GDBus to ubus bridge
luci-app-l850gl-mm 0.6.0-r3 Overview, Lock, and SMS views
```

The retired Status, old Advanced, Settings, radio toggle, generic reset,
primary SIM-slot switch, and old eSIM addon are not part of 0.6.0. APN,
route, DNS, credentials, and all connection settings remain in OpenWrt's
existing Network / Interfaces UI.

The normal build requires ModemManager with MBIM and netifd support and keeps
generic AT-over-D-Bus disabled. `CONFIG_L850GL_MM_BRIDGE_EXPERT` is off
by default and depends on an explicitly enabled
`MODEMMANAGER_WITH_AT_COMMAND_VIA_DBUS` build.

## Evidence status

Version 0.6.0 renames the active packages, service, ACLs, paths, and ubus
objects; the retired names are migration/history identifiers only and must not
coexist with the new pair. It also adds the strict expert-build `AT+CBC`
voltage parser/cache. To prevent a globally scheduled poll from making a valid
serving fallback flicker, LuCI may retain one structurally valid carrier
snapshot for at most 30 seconds across only `busy` or `rate_limited` responses
from the identical opaque modem ID and generation. Expiry, generation change,
malformed data, schema mismatch, transport failure, or any non-transient error
still clears it and fails closed. For the previous `0.6.0-r1`, static run
`30416321185` and SDK run `30416321209` passed for source `4783633`; its
checksum-verified expert pair was installed on 2026-07-29 with exact 8/5 new
method tables and no retired object. That r1 read-only acceptance returned
`3550 mV`, stable ten-second effective Serving Cell fallback, matching served
assets, and a connected bearer. Release r2 added the active-secondary uplink
shape but live repetition exposed a second, metric-only SINR sentinel. Release
r3 handles that exact observation and remains pending CI/install acceptance
until recorded in the dated validation document.

Live testing on 2026-07-27 validated the fixed grammar and recovery matrix on
an L850-GL MBIM `2cb7:0007` running firmware
`18500.5001.00.05.27.30`: sanitized XMCI scan, exact current-cell lock,
exact neighbor-cell lock, EARFCN-only lock, clear, `CFUN=15` removal/reprobe,
registration, bearer recovery, NVM postconditions, and serving-cell changes.
The router was returned to clear/automatic state with its bearer connected.
The allowlist contains only that exact firmware. The installed 0.3.0-r1 expert
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
