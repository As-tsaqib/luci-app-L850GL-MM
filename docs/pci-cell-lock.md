<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# L850 LTE neighbor scan and PCI/EARFCN lock

## Current verdict

Version 0.6.0 retains the PCI expert contract, build/ACL gates, standard
GetCellInfo-first scan path, bounded XMCI and NVM parsers, typed command
builders, rate limiting, cancellation, timeout, shared mutation locking, and
the reset/reprobe/registration/postcondition state machine. It adds the
read-only `get_carrier_info` sibling method described below. Version 0.6 also
adds an expert-only fixed `AT+CBC` Overview voltage refresh, which does not
change this object or the PCI mutation grammar, allowlist, recovery sequence,
or postconditions.

Firmware `18500.5001.00.05.27.30` completed the approved live matrix on
2026-07-27 and is the only allowlist entry. Exact and EARFCN-only set, clear,
`CFUN=15`, object replacement, registration and bearer recovery, NVM state,
and serving-cell postconditions were observed through ModemManager's command
queue. Every other firmware, non-L850 model, non-MBIM composition, plugin
mismatch, or failed `2cb7:0007` attestation remains fail-closed.

Status is therefore: implemented and live-verified for that one
hardware/firmware tuple at command level and through the prior installed
schema-2 expert package. Installed schema-3 v0.4 additionally verified the
expert capability, NVM status, and XMCI scan on the same tuple while leaving the
existing lock untouched. Exact set and clear returned replacement opaque
identities and verified postconditions; an authorized HTTP `/ubus` session also
returned the typed expert status. Interactive browser rendering remains a
separate UI observation, not a blocker on the verified backend state machine.

The five-second completion-based scan cooldown and per-modem scan single-flight
policy described below were built and installed in 0.4.0-r2. Live admission
testing produced exactly one `scan_ready` and one retryable `busy` without
`retry_after_ms` from a 50 ms overlap batch. Immediate cooldown values decreased
from 4933 ms to 3888 ms across a measured 1040 ms interval without either
rejection extending the deadline. Three further post-cooldown XMCI scans
completed sequentially with bounded 4/5/5-cell results while the modem remained
connected. The detailed record, including the discarded first-harness attempt
and recovery caveat, is in `live-router-validation.md`.

Release r2 also contains separately requested LuCI and SMS-view work. The live
claim in the preceding paragraph applies specifically to the expert backend
scan admission/cooldown delta; PCI mutation behavior was not changed or rerun.
Schema-4 0.5.0-r1 SDK/package installation and read-only post-install
validation passed on 2026-07-28. The CA addition does not extend the PCI
mutation allowlist, and no PCI mutation or cell scan was repeated in that run.

## 4PDA community evidence

The public [Fibocom L8x0-GL discussion](https://4pda.to/forum/index.php?showtopic=1066668)
was reviewed through 2026-07-27. It supplies useful candidate grammar and
negative evidence, but it is not local hardware validation:

- [post 121801248](https://4pda.to/forum/index.php?showtopic=1066668&view=findpost&p=121801248)
  publishes a six-field `freq_lock` help signature. The same post describes
  both logical and encoded band values and several different apply paths, with
  no exact firmware attached.
- [posts 126464498](https://4pda.to/forum/index.php?showtopic=1066668&view=findpost&p=126464498)
  and [126477912](https://4pda.to/forum/index.php?showtopic=1066668&view=findpost&p=126477912)
  report L850 NVM observations, candidate `255`/`65535` clear and wildcard
  sentinels, persistence, and frequency-only locking. Their enable-field
  interpretation conflicts with the earlier post and neither report names an
  exact firmware/composition or proves a complete recovery matrix.
- [posts 129670484](https://4pda.to/forum/index.php?showtopic=1066668&view=findpost&p=129670484),
  [129670766](https://4pda.to/forum/index.php?showtopic=1066668&view=findpost&p=129670766),
  and [129673899](https://4pda.to/forum/index.php?showtopic=1066668&view=findpost&p=129673899)
  show why guessing is unsafe: a malformed/different tuple was followed by a
  registration cycle, while the reported recovery coincided with renewal of
  the subscriber's tariff. The proposed clear operation is therefore
  community evidence, not an isolated postcondition.
- [post 139046345](https://4pda.to/forum/index.php?showtopic=1066668&view=findpost&p=139046345)
  reports a two-stage exact-PCI then PCI-wildcard workflow on an L850-GL, but
  does not identify firmware or prove clear, reprobe, or persistence.
- For target firmware `18500.5001.00.05.27.30`,
  [posts 142119412](https://4pda.to/forum/index.php?showtopic=1066668&view=findpost&p=142119412)
  and [142133341](https://4pda.to/forum/index.php?showtopic=1066668&view=findpost&p=142133341)
  lead to a report that a fix/CFUN4/unfix sequence was temporary. A later user
  with the same firmware reports unsuccessful `freq_lock` and aggregation in
  [post 143570066](https://4pda.to/forum/index.php?showtopic=1066668&view=findpost&p=143570066),
  still without a public successful resolution in
  [post 143656918](https://4pda.to/forum/index.php?showtopic=1066668&view=findpost&p=143656918).

These reports narrowed the live-test candidates but were not treated as proof.
The embedded tuple was selected only after the target firmware's own help
signature and a local set/clear/recovery matrix established logical LTE band
encoding, wildcard and clear sentinels, NVM semantics, and `CFUN=15` behavior.
No community-only alternative, direct NVM write, fallback ladder, or direct
device access is embedded.

## Ownership and build gate

The expert path is:

```text
LuCI Lock
  -> typed l850gl.mm.l850
  -> opaque ID + generation + exact hardware gate
  -> asynchronous libmm-glib / ModemManager queue
  -> bounded normalized result
```

It never opens a TTY/WDM device and never accepts command, port, D-Bus path,
sysfs path, device path, band encoding, wildcard, or NVM path from the browser.

The base build keeps generic AT-over-D-Bus disabled and does not contain or
publish the expert object. The object exists only with both:

```text
CONFIG_MODEMMANAGER_WITH_AT_COMMAND_VIA_DBUS=y
CONFIG_L850GL_MM_BRIDGE_EXPERT=y
```

PCI scan/status/mutation is protected by
`luci-app-l850gl-mm-lock-pci-expert`, separate from Band Lock. Read-only carrier
status is additionally granted by the exact Overview ACL for its Overview
panel.
SMS, Band Lock, and PCI operations share the existing per-modem mutation lock;
an internal hardware-slot coordinator keeps that exclusion active across the
new opaque modem object created by reset.

## Typed API

```text
cell_scan(modem_id, generation)
get_carrier_info(modem_id, generation)
cell_lock_status(modem_id, generation)
set_cell_lock(modem_id, generation, earfcn, optional pci, confirm)
clear_cell_lock(modem_id, generation, confirm)
```

Every method resolves the exact current opaque modem and generation and
attests L850-GL, the required upstream plugin, MBIM composition, and USB
`2cb7:0007`.
The in-flight reset coordinator may observe and verify a replacement at the
same internal hardware slot. Its final response carries the replacement's new
opaque ID and generation. No subsequent write is retargeted; a new mutation
requires a new snapshot and confirmation.

`cell_lock_status` keeps two independent capability surfaces:

- top-level `state`, `mutable`, and `reason` describe set/clear mutation;
- nested `scan` describes whether a button-driven standard GetCellInfo attempt
  is currently available, busy, not ready, or rate limited. An active scan is
  `busy`; because its completion cannot be predicted, that state has no
  `retry_after_ms`.

Unsupported firmware makes mutation `unsupported_firmware` while standard scan
may still be advertised. On the exact allowlisted firmware, `cell_lock_status`
queries a fixed NVM path asynchronously and exports only `clear`,
`configured_earfcn`, or `configured_exact`; this status read is not itself a
serving-cell postcondition.

`get_carrier_info` is a read-only expert method used by Overview. It is exact
hardware/firmware gated and dispatches only the fixed `AT+GTCAINFO?` command
through asynchronous ModemManager. It is single-flight and mutually excluded
with cell scan and mutation, has a 20-second operation deadline around a
15-second command timeout, and starts a five-second cooldown after every
terminal completion. Its 4,096-byte/eight-slot parser requires an index-1
14-field primary record and index-2..8 10-field secondary records, ignores only
the exact inactive secondary sentinel, and exports only active bands, primary
and active secondary band/EARFCN/PCI/bandwidth, and the normalized active
carrier count. The primary requires own-band DL/UL EARFCNs and numeric
bandwidths. A secondary requires own-band DL values plus the live-verified UL
bandwidth sentinel `255` and an UL EARFCN equal to the primary UL; it exports
`ul_bandwidth_mhz: null`. Any independent secondary uplink and active B29/B32
remain fail-closed until their exact representation is captured live on the
allowlisted firmware.
Raw response and MCC/MNC/TAC/cell ID/signal fields never cross ubus. It performs
no set, clear, reset, NVM write, or serving-cache mutation.
Fixtures under `tests/fixtures/ca` cover the sanitized live primary/inactive
shape, the live B5 primary plus B3/B1 downlink-only secondaries, and invalid
sentinels, copied-primary-UL mismatch, unverified secondary uplink, bandwidth,
PCI, counts, field shape, indexes, duplicates, and terminal text.

## Standard scan path

`cell_scan` first calls asynchronous libmm-glib `mm_modem_get_cell_info()`.
Exactly one scan may be active per modem. A concurrent request fails as
retryable `busy` before another operation is dispatched. Scan remains blocked
while another per-modem mutation is active, cancelled with the modem/ubus
lifetime, and timed out after 45 seconds.

Terminal completion starts a five-second per-modem cooldown for successful,
failed, timed-out, or cancelled scans. During that window a request fails as
retryable `rate_limited` with the ceil-rounded remaining milliseconds in
`retry_after_ms`. An active scan has no honest completion estimate, so `busy`
does not carry that field. Rejected overlap and cooldown requests do not extend
the cooldown.

The callback rejects removal, proxy mismatch, and generation changes. It
normalizes LTE objects only:

- serving becomes type 4; neighbor becomes type 5;
- physical cell ID is parsed from ModemManager's hexadecimal PCI string and
  must be 0..503, including 0;
- EARFCN must map to an LTE band present in live SupportedBands;
- unavailable `-G_MAXDOUBLE` RSRP/RSRQ is omitted;
- non-finite metrics, malformed objects, duplicate LTE serving records, and
  more than 64 LTE cells reject the complete response;
- MCC/MNC, TAC, cell ID, and other raw identifiers are not exported.

Success is `scan_ready`, `source: modemmanager`, and a bounded `cells` array.
The target L850 returns `Core.Unsupported` for GetCellInfo. Only on the exact
allowlisted firmware does that error dispatch fixed `AT+XMCI=1` through
asynchronous `mm_modem_command()`; the browser cannot supply or alter it.
Success identifies `method` as `standard-cell-info` or `l850-xmci`.

## Vendor parser contract

The Fibocom L8/L860 manual describes an XMCI response with 14 LTE fields. The
live ModemManager response contains `+XMCI:` records with blank lines and no
terminal `OK`; the parser accepts that shape or exactly one optional trailing
`OK`, and:

- LTE type 4 serving and type 5 neighbor only; type 6 is rejected;
- at most one serving record and at most 64 total records;
- decimal or explicitly prefixed hexadecimal fields, optionally wrapped in
  one pair of quotes;
- PCI 0..503, including zero;
- an EARFCN that maps through the complete LTE band table;
- RSRP/RSRQ required-field sentinel rejection and reviewed ranges;
- signed RSSNR, and known sentinels only in discarded CI, uplink EARFCN,
  pathloss, RSSNR, and timing-advance fields;
- bounded input no larger than 16,384 bytes;
- no embedded NUL, truncated/extra field, integer overflow, trailing record
  after `OK`, or ambiguous encoding.

RSRP fixture conversion is raw minus 141 dBm. RSRQ uses the reviewed half-dB
mapping stored as tenths of dB. MCC/MNC/TAC/cell ID fields are validated for
shape/range but deliberately discarded from normalized output.

Fixtures under `tests/fixtures/pci` cover the sanitized live quoted/no-OK
shape, valid serving/neighbor data, hex PCI zero, type 6, PCI 504, required
sentinels, allowed discarded sentinels, malformed fields, overflow, invalid
encoding, and data after `OK`. Separate fixtures cover clear, exact, and
EARFCN-only NVM states plus inconsistent/extra-field rejection.

## Set/clear policy

The handlers enforce typed fields and confirmation. EARFCN must map to a live
supported LTE band, optional PCI must be 0..503, and hardware, generation,
firmware, cooldown, and the shared mutation lock are checked before dispatch.
The target firmware's own help response established this six-field signature:

```text
freq_lock(sim_id, rat, band, inter_frequency_lock_enable, frequency, psc_pci)
```

The only compiled-in target-firmware grammar is:

```text
exact:       AT@SIC:FREQ_LOCK(0,3,<logical-band>,1,<earfcn>,<pci>)
EARFCN-only: AT@SIC:FREQ_LOCK(0,3,<logical-band>,1,<earfcn>,65535)
clear:       AT@SIC:FREQ_LOCK(0,3,255,0,65535,65535)
apply:       AT+CFUN=15
state query: AT@NVM:DYN_CPS.NAS_ASM.FREQ_LOCK_PARAMS.*??
```

Set and clear require the exact response
`Frequency Lock Configuration Success CPS_MSG_TYPE_ASM_EM_CTRL_CNF`; a bare
`OK` is rejected. `CFUN=15` normally completes with `Core.Cancelled` after
dispatch because the modem object disappears. The coordinator retains only
internal hardware-slot correlation data, observes the new attested object,
waits for registered/connected state, then queries NVM. Set additionally runs
XMCI and matches the serving EARFCN and, when requested, PCI. Clear succeeds
only when the five-field NVM clear sentinel is exact.

The observed target-firmware NVM invariants are:

```text
rat=3
band_info=0
inter_freq_lock_support=1 and frequency/requested PCI (or 65535) when locked
inter_freq_lock_support=0 and frequency/PCI=65535 when clear
```

Unexpected response shape, state, replacement identity, attestation, NVM, or
serving cell fails closed. Timeout or transport loss after dispatch becomes
`outcome_unknown` and is never retried automatically.

## State policy

The modeled states are:

```text
available
unsupported_build
unsupported_firmware
scan_ready
lock_applied_reset_required
resetting
applied_verified
cleared_verified
reprobe_timeout
registration_timeout
verification_mismatch
outcome_unknown
```

The transition policy allows a reviewed flow from available to scan-ready,
then dispatch/reset, and finally only a verified or explicit failure state.
The runtime revalidates immediately before dispatch, marks post-dispatch
uncertainty, observes object replacement read-only, waits for registration,
and compares NVM plus serving-cell postconditions. `applied_verified` requires
matching NVM and serving EARFCN/PCI; `cleared_verified` requires the exact NVM
clear sentinel. Neither state is inferred from command acknowledgement alone.

## Why the previous implementation is not copied

The audited XModem path provided useful evidence but accepted non-LTE type 6,
lost valid PCI 0 through truthiness checks, converted sentinels, inferred lock
state from incomplete data, exposed broad command/device choices, opened modem
ports outside ModemManager, and treated command success as final despite reset
and postcondition uncertainty. Those are design warnings, not reusable source.

## Hardware matrix status

The user approved disruptive testing with alternate hotspot access on
2026-07-27. All commands used ModemManager's queue; no TTY or WDM device was
opened. Stock ModemManager required temporary debug mode for discovery. The
production expert artifact instead rebuilds ModemManager with the reviewed
AT-via-D-Bus build option and returns normal logging to INFO.

Completed on `18500.5001.00.05.27.30` / L850-GL / upstream plugin / MBIM
`2cb7:0007`:

1. Sanitized XMCI serving and neighbor scan, including real quote/blank-line/
   sentinel behavior.
2. Clear plus exact NVM clear postcondition.
3. Exact current-cell lock and matching NVM/serving postcondition.
4. Exact visible neighbor lock; serving changed to the requested EARFCN/PCI.
5. EARFCN-only lock with PCI wildcard `65535`.
6. `CFUN=15` cancellation, object disappearance, replacement in about
   14--15 seconds, registration, bearer recovery, and final clear/automatic
   restoration.

Still not claimed by this matrix:

- unavailable-cell registration timeout behavior;
- physical unplug or ModemManager restart during the state machine;
- persistence across a full router reboot.

Those remaining fault/persistence cases do not broaden the allowlist and do
not weaken the runtime timeout, cancellation, identity, attestation, or
fail-closed checks. They remain explicit follow-up evidence rather than an
invitation to guess recovery commands.
