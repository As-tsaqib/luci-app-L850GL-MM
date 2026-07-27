<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# luci-app-fibocom

Version 0.3.0 is a small LuCI companion for Fibocom modems already managed by
ModemManager and OpenWrt netifd. Its public API is schema 2 and its menu is
exactly:

```text
Modem
`-- Fibocom Modem
    |-- Overview
    |-- Lock
    `-- SMS
```

ModemManager remains the only owner of modem objects, ports, SIMs, SMS, radio,
and bearers. netifd remains the owner of connection intent, APN, routes, and
DNS. The companion neither dials nor creates bearers, scans sysfs, opens modem
device nodes, executes external programs, or changes UCI.

## Product scope

Overview returns a compact, sanitized snapshot: manufacturer/model/revision,
modem state and power, SIM presence and lock, registration/operator/roaming,
access technology, signal quality and available RSRP/RSRQ/SINR, bearer state
and data interface, current bands, and capability summaries. Serving
EARFCN/PCI is omitted unless a validated source exists. It never returns raw
D-Bus or sysfs paths, port tables, subscriber identifiers, IP configuration,
credentials, or diagnostic dumps.

Lock contains:

- Band Lock through asynchronous libmm-glib `SetCurrentBands`, including
  automatic `["any"]` selection, supported-band and allowed-family validation,
  exact L850-GL MBIM `2cb7:0007` attestation, confirmation, WAN warning,
  generation checks, timeout, cooldown, and single-flight mutation handling.
- A build-gated L850 PCI/EARFCN section. The base binary does not contain or
  publish `fibocom.mm.l850`. An explicit expert build first tries standard
  ModemManager `GetCellInfo`, normalizes at most 64 LTE cells, and validates
  PCI and EARFCN against live supported bands. For the single allowlisted
  firmware `18500.5001.00.05.27.30`, `Core.Unsupported` falls back to a fixed
  XMCI query through ModemManager. Set/clear use fixed typed tuples, `CFUN=15`,
  exact hardware-slot reprobe correlation, registration wait, NVM verification,
  and serving-cell verification. Every other firmware fails closed.

SMS uses ModemManager Messaging/Sms only. The cache consumes Added, Deleted,
and property-change signals, reconciles every 30 seconds, keeps the newest
1,024 entries, and supports Inbox, Outbox, Draft, Unknown, and All. LuCI polls
every 10 seconds and follows opaque cursor pages past the backend's 100-message
per-call limit. Send uses Messaging.Create followed by Sms.Send; delete uses
Messaging.Delete and requires confirmation. Binary payloads and raw PDUs are
never exported.

Send retry tokens are CSPRNG values bound to a digest of recipient and body.
The in-memory dedupe cache holds at most 64 retained tokens; each retained
token expires 300 seconds after its most recent stored state, but an older
token may be evicted sooner when capacity is exceeded. It is a short retry aid,
not exactly-once delivery and does not survive a bridge restart. An uncertain
post-dispatch send returns `outcome_unknown` and is never resent automatically.

## API and permissions

The base object `fibocom.mm` exposes exactly seven schema-2 methods:

```text
list_modems
get_overview
get_lock_status
set_bands
list_sms
send_sms
delete_sms
```

The expert object, when explicitly compiled, exposes only:

```text
cell_scan
cell_lock_status
set_cell_lock
clear_cell_lock
```

The five rpcd groups are `luci-app-fibocom-overview`,
`luci-app-fibocom-sms-read`, `luci-app-fibocom-sms-write`,
`luci-app-fibocom-lock-band`, and
`luci-app-fibocom-lock-pci-expert`. They grant exact ubus methods only: no
wildcards, filesystem access, shell execution, cgi-io, or UCI writes.

LuCI treats malformed responses and every schema other than 2 as a
compatibility failure. All mutations are disabled until a matching schema,
opaque ID, modem generation, and (for SMS) messaging generation are present.

## Packaging

The active package set is:

```text
fibocom-mm-bridge   0.3.0-r1 native libmm-glib/GDBus to ubus bridge
luci-app-fibocom   0.3.0-r1 Overview, Lock, and SMS views
```

The retired Status, old Advanced, Settings, radio toggle, generic reset,
primary SIM-slot switch, and Fibocom eSIM addon are not part of 0.3.0. Network
configuration remains in OpenWrt's existing Network / Interfaces UI.

The normal build requires ModemManager with MBIM and netifd support and keeps
generic AT-over-D-Bus disabled. `CONFIG_FIBOCOM_MM_BRIDGE_L850_EXPERT` is off
by default and depends on an explicitly enabled
`MODEMMANAGER_WITH_AT_COMMAND_VIA_DBUS` build.

## Evidence status

Live testing on 2026-07-27 validated the fixed grammar and recovery matrix on
an L850-GL MBIM `2cb7:0007` running firmware
`18500.5001.00.05.27.30`: sanitized XMCI scan, exact current-cell lock,
exact neighbor-cell lock, EARFCN-only lock, clear, `CFUN=15` removal/reprobe,
registration, bearer recovery, NVM postconditions, and serving-cell changes.
The router was returned to clear/automatic state with its bearer connected.
The allowlist contains only that exact firmware. Full schema-2 ubus/LuCI
package validation is recorded separately from this command-level matrix; no
live SMS send/delete was performed. See [hardware evidence](docs/hardware-evidence.md)
and the [live record](docs/live-router-validation.md).

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
name and rebuilds/packages ModemManager with reviewed AT-via-D-Bus transport
only for the expert artifact.

Further design details are in [architecture](docs/architecture.md), the
[ubus API](docs/ubus-api.md), [threat model](docs/threat-model.md), and
[PCI design](docs/pci-cell-lock.md).

## Licensing

Backend and system integration code is GPL-2.0-or-later. LuCI JavaScript and
documentation are Apache-2.0. No QModem/XModem source is copied; every file is
covered by SPDX/REUSE metadata.
