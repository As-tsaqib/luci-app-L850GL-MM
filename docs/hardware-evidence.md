<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Hardware evidence

## Evidence boundary

The repository separates historical schema-1 v0.2 evidence from 2026-07-19,
the approved L850 firmware command/recovery matrix from 2026-07-27, and the
schema-2 package-level acceptance run. Source, fixture, SDK success, firmware
command evidence, and installed-package evidence are not interchangeable.

Never store IMEI, IMSI, ICCID, EID, phone numbers, SMS body, APN credentials,
PIN/PUK, activation codes, tokens, or assigned IP configuration. Evidence must
use an allowlist and sanitized fixtures rather than raw diagnostic dumps.

## Tested hardware context

| Item | Observed value |
|---|---|
| Dates | 2026-07-19 lifecycle/v0.2; 2026-07-27 PCI matrix |
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

## Cell-control observations and live matrix

The historical probes established why a stock build cannot provide the expert
path:

```text
ModemManager GetCellInfo
  Core.Unsupported on this L850/firmware/plugin combination

generic Modem.Command introspection
  Core.Unauthorized on the stock OpenWrt build
```

SupportedBands reported five UTRAN and 24 E-UTRAN entries. During the approved
2026-07-27 window, ModemManager was temporarily restarted in debug mode solely
to exercise its own command queue; it was restored to INFO afterward. No modem
device node was opened directly.

The firmware's help response proved the six typed `freq_lock` arguments. Live
tests then proved, one candidate at a time:

- fixed XMCI scan response shape and sanitized type-4/type-5 parsing;
- exact clear tuple and five-field NVM clear state;
- exact current-cell lock on a serving band-3 tuple;
- exact visible neighbor lock on a band-1 tuple, with the serving cell changing
  to the requested EARFCN/PCI;
- EARFCN-only lock using PCI sentinel `65535`;
- `CFUN=15` dispatch cancellation, old-object disappearance, replacement in
  approximately 14--15 seconds, registration, and bearer recovery;
- final clear/reset restoring automatic selection and a connected bearer.

The stored evidence uses sanitized values; live TAC, cell ID, subscriber data,
addresses, and command logs were not added as fixtures. This complete local
matrix, rather than community text, permits the one exact firmware allowlist
entry `18500.5001.00.05.27.30`.

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

The exact target firmware has incomplete and contradictory public
reports: one user described a temporary fix/CFUN4/unfix result, while another
reported that repeated `freq_lock` attempts did not produce aggregation. No
post supplies the target model/firmware/composition together with exact set,
clear, one reset/apply path, reprobe correlation, registration recovery, and a
serving EARFCN/PCI postcondition. The local 2026-07-27 matrix supplied that
missing proof independently; public posts are retained as provenance, not as
the allowlist authority.

## Current 0.3.0 evidence status

Implemented and covered by source/host/static tests:

- exact schema-2 base/expert method tables and build gate;
- CSPRNG identity, generation, cancellation, timeout, cooldown, and mutation
  policy;
- native SMS cache/send/delete flow and pagination;
- standard band policy and asynchronous SetCurrentBands call;
- structural malformed-blob tests;
- XMCI and NVM fixtures, including the sanitized live quote/no-OK shape, PCI 0,
  LTE types 4/5, type 6 rejection, required/allowed sentinels, malformed fields,
  overflow, duplicate/extra NVM keys, and oversized responses;
- asynchronous standard GetCellInfo normalization, fixed XMCI fallback, and
  64-cell bound;
- exact firmware allowlist, typed-only command builder, reset/reprobe/
  registration coordinator, NVM and serving-cell postconditions.

Not yet live-validated for 0.3.0:

- interactive browser rendering and schema-mismatch presentation;
- Overview field accuracy outside the connected state;
- SMS send/delete and pagination with more than 100 stored messages;
- Band Lock automatic/subset/recovery and outcome-unknown behavior;

Live-validated through installed 0.3.0-r1 packages on the allowlisted tuple:

- checksum-verified base and expert artifacts from SDK run `30261750513`;
- router-matched ModemManager 1.24.0-r10 expert replacement at INFO log level;
- exact seven-method base and four-method expert ubus tables;
- NVM clear status and bounded type-4/type-5 XMCI fallback scan;
- typed exact current-cell set with replacement identity, registration, NVM,
  and serving-cell postconditions;
- stale pre-reset identity rejection before dispatch;
- typed clear with replacement identity, registration, NVM postcondition, and
  final connected bearer;
- least-privilege HTTP `/ubus` schema-2 reads with rpcd session injection;
- installed three-menu layout and the five exact ACL grants;
- no raw PCI command text in the normal system log.

Live-validated at command/hardware level on the exact allowlisted tuple:

- XMCI scan, exact lock, EARFCN-only lock, clear, reset, reprobe, registration,
  bearer recovery, NVM verification, and serving-cell verification.

No live SMS send/delete was performed.

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

## Remaining PCI evidence

The allowlisted firmware has proven the positive set/clear/recovery matrix.
The following fault and persistence cases remain explicit follow-up work:

- unavailable-cell registration timeout;
- physical unplug during an operation;
- ModemManager restart mid-state-machine;
- full-router reboot persistence.

Runtime handling for these cases remains bounded and fail-closed: exact
identity/attestation checks, cancellation, deadlines, `outcome_unknown`, and no
automatic retry. No additional firmware may be allowlisted without its own
complete dated matrix.

## NCM boundary

L850 NCM USB `8087:095a` connectivity is not supported by this companion. A
future NCM data path belongs in ModemManager and needs its own bearer, RAW-IP,
addressing, teardown, and traffic evidence. It must not become a second dialer
inside this repository.
