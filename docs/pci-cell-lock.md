<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# L850 LTE neighbor scan and PCI/EARFCN lock

## Current verdict

Version 0.3.0 implements the expert contract, build/ACL gates, standard
GetCellInfo scan path, bounded vendor-response parser and fixtures, input
policy, state names, rate limiting, cancellation, timeout, and fail-closed
LuCI section.

It does not implement or dispatch a vendor scan fallback, lock tuple, clear
tuple, NVM query/write, reset, or recovery sequence. The firmware allowlist is
empty. Firmware `18500.5001.00.05.27.30` was historically observed but has not
passed a mutation/recovery matrix and is not allowlisted.

Status is therefore: implemented offline but fail-closed. No PCI claim is live
verified for schema-2 0.3.0.

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

These reports narrow the live test candidates, but do not settle band
encoding, enable/clear semantics, the one safe apply sequence, durability, or
serving-cell verification. No raw tuple from the thread is embedded or
dispatched by 0.3.0, direct NVM writes remain excluded, and the target firmware
remains outside the allowlist.

## Ownership and build gate

The expert path is:

```text
LuCI Lock
  -> typed fibocom.mm.l850
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
CONFIG_FIBOCOM_MM_BRIDGE_L850_EXPERT=y
```

It is protected by `luci-app-fibocom-lock-pci-expert`, separate from Band Lock.
The same daemon/cache is used so a future set/clear operation can share the
existing per-modem mutation lock.

## Typed API

```text
cell_scan(modem_id, generation)
cell_lock_status(modem_id, generation)
set_cell_lock(modem_id, generation, earfcn, optional pci, confirm)
clear_cell_lock(modem_id, generation, confirm)
```

Every method resolves the exact current opaque modem and generation and
attests L850-GL, Fibocom plugin, MBIM composition, and USB `2cb7:0007`.
Replacement objects are read-only observations; no old request is retargeted.

`cell_lock_status` keeps two independent capability surfaces:

- top-level `state`, `mutable`, and `reason` describe set/clear mutation;
- nested `scan` describes whether a button-driven standard GetCellInfo attempt
  is currently available, busy, not ready, or rate limited.

The empty allowlist makes mutation `unsupported_firmware` while standard scan
may still be advertised.

## Standard scan path

`cell_scan` first calls asynchronous libmm-glib `mm_modem_get_cell_info()`.
It is limited to one attempt per modem every 60 seconds, blocked while another
per-modem mutation is active, cancelled with the modem/ubus lifetime, and timed
out after 45 seconds.

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
The historically tested L850 returned Core.Unsupported for GetCellInfo. With
no proven vendor fallback, that case returns `unsupported_firmware` and sends
no raw command.

## Offline vendor parser contract

The Fibocom L8/L860 manual describes an XMCI response with 14 LTE fields. The
offline parser accepts only complete `+XMCI:` records followed by one `OK` and:

- LTE type 4 serving and type 5 neighbor only; type 6 is rejected;
- at most one serving record and at most 64 total records;
- decimal fields, with hexadecimal accepted only when explicitly prefixed;
- PCI 0..503, including zero;
- an EARFCN that maps through the complete LTE band table;
- documented measurement ranges and explicit sentinel rejection;
- bounded input no larger than 16,384 bytes;
- no embedded NUL, truncated/extra field, integer overflow, trailing record
  after `OK`, or ambiguous encoding.

RSRP fixture conversion is raw minus 141 dBm. RSRQ uses the reviewed half-dB
mapping stored as tenths of dB. MCC/MNC/TAC/cell ID fields are validated for
shape/range but deliberately discarded from normalized output.

Fixtures under `tests/fixtures/pci` cover valid serving/neighbor data, hex PCI
zero, type 6, PCI 504, sentinels, malformed fields, overflow, and invalid
encoding. These fixtures prove parser behavior only; they do not prove that a
firmware command is safe or supported.

## Set/clear policy

The current handlers enforce typed fields and confirmation. EARFCN must map to
a live supported band, and optional PCI must be 0..503. Exact hardware and
generation are checked again before any potential dispatch.

The following facts remain unproven and are intentionally absent from code:

- firmware-specific lock argument order and band encoding;
- whether frequency-only lock has a safe wildcard representation;
- exact clear/unlock tuple;
- NVM instance/path, active-lock flag, and query semantics;
- exact one reset/apply sequence;
- persistence across ModemManager restart, USB replug, or router reboot;
- reliable post-reprobe correlation and rollback behavior.

Consequently `set_cell_lock` and `clear_cell_lock` return
`unsupported_firmware`; an `OK` string alone could never become final success.

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
No current runtime mutation enters the dispatch/reset states. A future write
must revalidate immediately before dispatch, mark post-dispatch uncertainty,
observe object replacement read-only, wait for registration, and compare the
serving EARFCN/PCI postcondition before reporting `applied_verified` or
`cleared_verified`.

## Why the previous implementation is not copied

The audited XModem path provided useful evidence but accepted non-LTE type 6,
lost valid PCI 0 through truthiness checks, converted sentinels, inferred lock
state from incomplete data, exposed broad command/device choices, opened modem
ports outside ModemManager, and treated command success as final despite reset
and postcondition uncertainty. Those are design warnings, not reusable source.

## Hardware matrix required

No live scan, lock, clear, or reset may be run without explicit user approval.
A PCI maintenance window needs alternate access or physical recovery.

Read-only phase:

1. Record exact sanitized model, firmware, composition, plugin, bands, and
   current serving tuple.
2. Prove ModemManager command transport and command capability without direct
   device access.
3. Capture bounded sanitized serving/type-5 neighbor fixtures and all sentinel
   encodings.
4. Establish the exact lock-state query and SIM instance.

Disruptive phase, one candidate at a time:

1. Apply a lock to a currently visible frequency-only target.
2. Apply an exact EARFCN+PCI lock, including a synthetic parser test for PCI 0.
3. Prove the exact clear operation.
4. Prove exactly one reset/apply sequence; do not try a fallback ladder.
5. Observe removal/reprobe, registration, bearer/netifd recovery, and serving
   postcondition.
6. Exercise mismatch, timeout, unplug, rollback, restart, replug, and reboot.

Only a complete dated matrix may add one exact firmware to the allowlist and
change the status from fail-closed to live-verified.
