<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Hardware evidence

## Evidence boundary

The repository separates historical schema-1 v0.2 evidence from 2026-07-19,
the approved L850 firmware command/recovery matrix and schema-2 package run
from 2026-07-27, installed schema-3 v0.4 evidence, schema-4 v0.5 work from
2026-07-28, and renamed v0.6 source work from 2026-07-29. Source, fixture, SDK
success, firmware-command evidence, and installed-package evidence are not
interchangeable.

Never store IMEI, IMSI, ICCID, EID, phone numbers, SMS body, APN credentials,
PIN/PUK, activation codes, tokens, or assigned IP configuration. Evidence must
use an allowlist and sanitized fixtures rather than raw diagnostic dumps.

## Tested hardware context

| Item | Observed value |
|---|---|
| Dates | 2026-07-19 lifecycle/v0.2; 2026-07-27 PCI matrix; 2026-07-28 CA/composition read-only observation |
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

## Current 0.6.0 evidence status

Implemented in the current source/host/static contract:

- active repository/application name `luci-app-L850GL-MM`, packages
  `luci-app-l850gl-mm` and `l850gl-mm-bridge`, and ubus objects
  `l850gl.mm` / optional `l850gl.mm.l850`;
- migration guards that stop, disable, and remove the retired service/packages
  before the renamed pair is installed, with no supported coexistence mode;
- a strict bounded voltage parser and expert-only asynchronous fixed `AT+CBC`
  refresh that exports only nullable `modem.voltage_mv` and never raw output;
- generation/freshness checks that make malformed or unavailable voltage data
  affect only that field rather than the complete Overview response;
- frontend retention of a bounded generation-matched last-known-good carrier
  snapshot only across transient `busy`/`rate_limited` polls, while malformed,
  stale, incompatible, and non-transient failures remain fail-closed.
- a target-firmware active-secondary grammar that requires own-band DL fields,
  UL bandwidth sentinel `255`, and an UL EARFCN equal to the validated primary
  UL, with explicit nullable secondary UL bandwidth at the API boundary;
- carrier-first LuCI polling so the optional voltage refresh cannot repeatedly
  win the same per-modem expert-command arbitration slot.

The supplied `+CBC: <status>,<millivolts>` shape and sanitized fixtures are
parser input evidence rather than proof by themselves. Installed 0.6.0-r1 was
subsequently accepted on 2026-07-29: static run `30416321185`, SDK run
`30416321209`, source `4783633`, checksum-verified renamed packages, exact 8/5
new method tables with the retired service/object absent, and matching
installed/served assets. The expert bridge returned live typed voltage
`3550 mV`; all other Overview data remained usable. Three six-second carrier
queries and three ten-second full polling cycles remained available, with one
intentional immediate query producing accurate `rate_limited` and succeeding
after its deadline. No live scan, SMS write/delete, Band/Mode Lock, or PCI
mutation was run during this acceptance.

Later on 2026-07-29, while LTE aggregation was active, authenticated HTTP ubus
and local ubus both reached the r1 backend but received
`malformed_response`; ACL/session/transport checks all passed. A separate
approved read-only ModemManager command observation showed one valid B5 primary
and active B3/B1 secondaries. After subscriber/location fields were discarded,
the relevant secondary grammar was: own-band DL EARFCN and numeric DL bandwidth,
an UL EARFCN copied from the B5 primary, and UL bandwidth sentinel `255`. That
shape explains why the original paired-UL parser passed a non-CA primary poll
but rejected polls when secondaries appeared. The r2 fixtures retain only
sanitized structural values. They admit this exact copied-primary-UL/sentinel
combination, export no raw response, serialize secondary UL bandwidth as null,
and reject independent secondary uplink until separately observed. This is
parser input and diagnosis evidence; r2 installed acceptance is recorded only
after its CI artifacts are installed and tested.

On 2026-07-29, before the renamed build was installed, the existing expert
bridge was used for a clean read-only `GTCAINFO` parser check. A recently
started direct-TTY `picocom` reader was first closed so ModemManager again had
exclusive port ownership; ModemManager itself was not stopped. Three typed
carrier queries spaced six seconds apart all returned one valid active primary
and no active secondary. The sanitized live grammar was exactly one header,
one 14-field primary, and one inactive 10-field secondary sentinel. The sample
signal codes supplied for review normalize to RSRP `-96 dBm`, RSRQ `-12.0 dB`,
and SINR `3.0 dB`; bandwidth code 5 normalizes to 20 MHz. No MCC, MNC, TAC,
cell ID, raw response, or subscriber identifier was retained. An earlier empty
response during direct-TTY contention is transport evidence only and is not a
parser defect or an explanation for the older Serving Cell UI flicker.

## Version 0.5.0 evidence status

Implemented in the current source/host/static contract:

- schema 4 with the same exact eight-method base table and a five-method expert
  table that adds only read-only `get_carrier_info`;
- normalized `mbim`/`ncm`/`unknown` USB mode and bounded full IMEI, SIM number,
  IMSI, and ICCID in authenticated Overview under the explicit product-owner
  disclosure override;
- identifier sanitization and absence from list responses, logs, fixtures,
  raw evidence, diagnostics, browser storage, and console output;
- the fixed asynchronous `AT+GTCAINFO?` expert query with exact target gate,
  generation/lifetime checks, 20-second operation and 15-second command
  deadlines, mutual exclusion with scans/mutations, and five-second
  completion-based cooldown;
- a 4,096-byte/eight-slot typed parser for the 14-field primary and 10-field
  secondary grammar, with exact inactive-sentinel omission, supported-band and
  paired DL/UL EARFCN, PCI, and bandwidth validation, bounded output, and
  valid/invalid fixtures;
- schema-4 frontend structural validation, a display-only serving EARFCN/PCI
  fallback from the validated primary carrier, and fail-closed unavailable
  state on base builds or rejected expert responses.

Before implementation, an approved read-only command-level observation on
2026-07-28 established the exact target input shape without recording raw
output or identifiers. Firmware `18500.5001.00.05.27.30` reported an index-1
B3 primary at EARFCN 1325 / PCI 381 with 20 MHz downlink/uplink bandwidth, one
active carrier total, and the exact inactive index-2 sentinel. A separate
`GTUSBMODE` value 7 observation and ModemManager's composition independently
agreed on MBIM. This evidence validates the target grammar and expected
normalization only; it is not a live `get_carrier_info` ubus or LuCI result.
No active B29/B32 `GTCAINFO` capture exists. Those downlink-only bands therefore
remain fail-closed as active carriers until their exact uplink/sentinel form is
observed on the allowlisted firmware and added with fixtures; SupportedBands
advertisement alone is not sufficient evidence.

Live-validated through installed 0.5.0-r1 packages:

- static run `30365531206` and SDK run `30365531207` for commit `56dbc0e`;
- checksum-verified base/expert artifacts, with expert object/command absent
  from base and present in expert;
- exact installed schema-4 8/5 method tables, three-menu package, and ACL;
- MBIM plus bounded IMEI/IMSI/ICCID presence without retaining values;
- one live B3 primary carrier and no active secondary;
- one-available/one-busy parallel query admission, a 4,981 ms immediate
  completion-based cooldown, and successful post-cooldown query;
- serving EARFCN/PCI display fallback from that validated primary without an
  XMCI scan;
- matching installed/served LuCI asset hashes, connected netifd/bearer state,
  no identifier leakage in logs/process arguments, and zero bridge warnings.

ModemManager reported no OwnNumbers value, so SIM Number remains visibly
`Unavailable`. No SMS, Band/Mode, PCI, reset, or cell-scan action was run. The
historical evidence below remains scoped to its recorded release.

## Version 0.4.0 evidence status

Implemented in source and covered by static/host contracts:

- exact eight-method schema-3 base table and unchanged four-method expert gate;
- persistent `set_modes` grammar, unique internal netifd binding resolution,
  two-option UCI commit/readback, credential preservation, shared lock,
  asynchronous network reload, and activation uncertainty reporting;
- LTE-only Band Lock UI with non-LTE allowed-family preservation;
- generation-bound serving-cell cache populated only by validated standard
  CellInfo, explicit expert scan, or PCI postcondition, with freshness/backoff;
- schema-3 frontend fail-closed validation and responsive mode controls.

A read-only pre-install probe on 2026-07-28 reconfirmed that this exact L850
returns `Core.Unsupported` for standard CellInfo and that an explicitly invoked
expert scan succeeds through `l850-xmci`. Therefore Overview does not claim
automatic serving-cell availability: it never starts vendor scan itself, and
its cache becomes available only after an explicit validated expert result or
verified PCI postcondition.

Live-validated through installed 0.4.0-r2 packages:

- static run `30348512717` and OpenWrt 25.12.5 `ipq40xx/generic` SDK run
  `30348512557`, source
  `10fa6a6868bd9ee423ad3473a897107194f8481e`;
- checksum-verified bridge and LuCI 0.4.0-r2 packages installed while the
  identical existing expert ModemManager 1.24.0-r10 was left installed;
- one 50 ms overlap batch admitted exactly one XMCI scan and rejected the
  second as retryable `busy` without `retry_after_ms`;
- immediate cooldown values of 4933 ms and 3888 ms across a measured 1040 ms
  interval differed by only 5 ms from the measured drop and were not extended
  by rejected requests;
- three sequential scans after the five-second post-completion interval
  returned bounded 4/5/5-cell responses of 722/847/839 bytes, with the modem
  connected after each;
- final cell lock was clear, current bands were unchanged, and modem,
  bearer, and netifd ownership state were healthy.

The first validation harness used an unsupported fractional BusyBox `sleep`
and abandoned its client after dispatch. The modem later entered disabled/low
power and a netifd `Invalid` loop; a ModemManager service restart, bounded
netifd settle cycles, and one ModemManager power-on attempt did not recover it,
while a physical replug did. Clean acceptance was performed only after
recovery. This is retained as a harness/recovery caveat, not as proof that scan
caused the state transition.
No PCI set/clear/reset, Band Lock, mode, or SMS mutation was run. Release r2
also contains separately requested LuCI/SMS-view changes; the scan matrix
validates only the expert backend admission/cooldown delta.

Live-validated through installed 0.4.0-r1 expert packages:

- static run `30314503929` and SDK run `30314503962`, source `06eb8df`;
- checksum-verified expert bridge, LuCI, and router-matched ModemManager APKs;
- exact eight-method schema-3 base and four-method expert tables;
- combined and 4G-only persistence/readback/activation, followed by restoration
  to combined preferred-4G intent with connected/home bearer;
- stale-generation and inconsistent-preference rejection before write;
- LTE-only Band Lock request with all five hidden UTRAN bands preserved;
- explicit XMCI scan populating the bounded validated Overview serving cache;
- schema-3 SMS read/cache metadata without exposing number or body;
- netifd up/available, services running, and zero bridge warning/error entries.

The installed v0.4 run did not repeat PCI set/clear/reset or SMS send/delete.
PCI mutation remains command-level and schema-2-package live-verified on the
same exact allowlisted tuple; v0.4 additionally verified expert capability,
NVM status, and XMCI scan while leaving the existing lock untouched.

## Version 0.3.0 evidence status

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
- SMS content/numbers and every identifier outside the four explicitly approved
  Overview fields absent from logs, process lists, diagnostics, and artifacts;
- approved Overview IMEI/SIM-number/IMSI/ICCID values absent from logs,
  fixtures, evidence capture, browser storage, and console output.

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

L850 NCM USB `8087:095a` connectivity is not supported by this companion.
Schema-4 Overview may identify the composition as `ncm`; that observation does
not imply bearer/data-path support. A future NCM data path belongs in
ModemManager and needs its own bearer, RAW-IP,
addressing, teardown, and traffic evidence. It must not become a second dialer
inside this repository.
