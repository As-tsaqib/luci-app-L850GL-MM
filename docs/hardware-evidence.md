<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Hardware evidence

## Evidence boundary

The live record in this repository is historical schema-1 v0.2 evidence from
2026-07-19. It does not validate the current schema-2 0.3.0 packages. Source,
fixture, host test, or SDK success is not a substitute for a dated live result.

Never store IMEI, IMSI, ICCID, EID, phone numbers, SMS body, APN credentials,
PIN/PUK, activation codes, tokens, or assigned IP configuration. Evidence must
use an allowlist and sanitized fixtures rather than raw diagnostic dumps.

## Tested hardware context

| Item | Historical observed value |
|---|---|
| Date | 2026-07-19 |
| Router | Linksys EA6350v3 |
| CPU / target | ARMv7 / `ipq40xx/generic` |
| OpenWrt | 25.12.5, kernel 6.12.94 |
| ModemManager | 1.24.0-r10 |
| Modem | Fibocom L850-GL |
| Firmware | `18500.5001.00.05.27.30` |
| Composition / USB ID | MBIM / `2cb7:0007` |
| Plugin | `fibocom` |
| Network owner | netifd `proto modemmanager` |
| Messaging storage | `mt` available |

Observed runtime port names were `cdc-wdm0`, `ttyACM0`, `ttyACM1`, `ttyACM2`,
and `wwan0`. They are evidence only and are never accepted as stable product
identity or browser input.

## What the historical run proved

The ModemManager/netifd lifecycle was live-validated:

- unplug removed the old object and brought the netifd interface down;
- replug regrouped MBIM/net/AT ports into a new modem object;
- the OpenWrt monitor marked the configured interface available;
- netifd enabled, registered, connected, and restored the interface in about
  eleven seconds;
- a later non-replug bearer recovery returned online in about five seconds.

Staged v0.2.0-r3 schema-1 testing also proved:

- `fibocom.mm` registration and authenticated LuCI/rpcd transport after the
  canonical `ubus_rpc_session` parser fix;
- successful read-only list/overview/status/capability/SMS calls;
- one externally delivered inbound SMS appeared automatically in the cache;
- revision-only reconciliation continued approximately every 29-30 seconds;
- no SMS number, body, opaque ID, or D-Bus path was retained in evidence.

That historical run did not send or delete a live SMS and did not execute a
band, radio, reset, SIM-slot, PCI, or connection mutation. Old eSIM menu/probe
observations concern a retired package and make no 0.3 product claim.

## Cell-control observations

Two read-only historical probes established the current blocker:

```text
ModemManager GetCellInfo
  Core.Unsupported on this L850/firmware/plugin combination

generic Modem.Command introspection
  Core.Unauthorized on the stock OpenWrt build
```

SupportedBands reported five UTRAN and 24 E-UTRAN entries. These observations
do not prove PCI lock support. Firmware `18500.5001.00.05.27.30` is deliberately
not allowlisted merely because manuals or community projects mention a command
family.

## Community evidence is not local validation

The [4PDA Fibocom L8x0-GL thread](https://4pda.to/forum/index.php?showtopic=1066668)
was re-reviewed on 2026-07-27. Relevant direct posts are recorded in
`pci-cell-lock.md` and `provenance.md`. They corroborate a six-argument command
family, candidate PCI/frequency wildcard and clear sentinels, and the fact that
lock state may affect registration and persist in NVM.

They also conflict on logical versus encoded band values, enable/clear
semantics, and whether apply needs pre-registration, CFUN4, CFUN15, a band
toggle, or two separate lock steps. One malformed/different tuple preceded a
registration cycle; its apparent recovery was confounded by a tariff payment.

The exact target firmware has only incomplete and contradictory public
reports: one user described a temporary fix/CFUN4/unfix result, while another
reported that repeated `freq_lock` attempts did not produce aggregation. No
post supplies the target model/firmware/composition together with exact set,
clear, one reset/apply path, reprobe correlation, registration recovery, and a
serving EARFCN/PCI postcondition. None of these commands were run during this
repository work, so the local allowlist stays empty.

## Current 0.3.0 evidence status

Implemented and testable offline:

- exact schema-2 base/expert method tables and build gate;
- CSPRNG identity, generation, cancellation, timeout, cooldown, and mutation
  policy;
- native SMS cache/send/delete flow and pagination;
- standard band policy and asynchronous SetCurrentBands call;
- structural malformed-blob tests;
- PCI vendor-response parser fixtures, including PCI 0, LTE types 4/5, type 6
  rejection, sentinels, malformed fields, overflow, and oversized responses;
- asynchronous standard GetCellInfo normalization and 64-cell bound;
- empty firmware mutation allowlist and no embedded vendor command tuple.

Not yet live-validated for 0.3.0:

- package install/upgrade and schema-2 browser behavior;
- Overview field accuracy across modem states;
- SMS send/delete and pagination with more than 100 stored messages;
- Band Lock automatic/subset/recovery and outcome-unknown behavior;
- standard cell scan on current packages;
- any PCI set/clear/reset/reprobe/postcondition behavior.

## Required non-disruptive acceptance

Before enabling writes, record sanitized results for:

- cold boot, unplug/replug, ModemManager restart, and netifd down/up;
- two identical modems and ModemManager path/index reuse;
- schema mismatch and malformed response behavior in LuCI;
- Overview in absent, locked, registered, and connected states;
- SMS initial list, signals, 30-second reconciliation, 10-second UI poll,
  folder filtering, stale cursor, and more than 100 messages;
- private content absent from logs, process lists, Overview, and artifacts.

Live SMS send/delete still requires explicit user permission even if it is not
expected to interrupt WAN.

## Required Band Lock acceptance

In a maintenance window with alternate management access:

- automatic to reviewed LTE subset and back to automatic;
- unsupported, duplicate, mixed-any, and disabled-family rejection;
- stale generation, unplug, timeout, and transport-loss behavior;
- registration and traffic recovery;
- no claim of persistence across reset/replug unless separately proven.

## PCI evidence required before allowlisting

Read-only fixture work must establish, for one exact firmware:

- command capability/introspection and complete serving/neighbor responses;
- exact type, field, encoding, PCI, EARFCN, RSRP, and RSRQ sentinel behavior;
- lock-state query semantics and supported SIM instance;
- safe timeout/cancellation behavior through ModemManager's queue.

A separately approved disruptive matrix must then prove:

- frequency-only and exact EARFCN+PCI lock, including PCI 0 policy;
- the exact band encoding and optional-PCI representation;
- exact clear/unlock tuple;
- exactly one reset/apply sequence, not a fallback ladder;
- object disappearance, reprobe correlation, registration, bearer/netifd
  recovery, and serving-cell postcondition;
- mismatch, timeout, unplug, rollback, reboot, and persistence behavior.

Only after that matrix may the exact model/firmware tuple enter the allowlist.
Until then the expert implementation is correctly reported as implemented but
fail-closed.

## NCM boundary

L850 NCM USB `8087:095a` connectivity is not supported by this companion. A
future NCM data path belongs in ModemManager and needs its own bearer, RAW-IP,
addressing, teardown, and traffic evidence. It must not become a second dialer
inside this repository.
