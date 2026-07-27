<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Threat model

## Security objective

An authenticated LuCI user may view a narrow modem/SMS snapshot and invoke
only reviewed SMS, band, or explicitly gated expert actions. Browser input must
never become a path, command, process argument, arbitrary D-Bus call, UCI
write, or direct device operation.

Protected assets include modem firmware and registration state, SIM and SMS
content, phone numbers, subscriber/device identifiers, network credentials and
topology, WAN availability, and the integrity of ModemManager/netifd ownership.

## Trust boundaries

```text
browser (untrusted values)
  -> LuCI rpc + rpcd ACL
  -> libubox request parser
  -> fibocom-mm-bridge policy/cache
  -> asynchronous typed libmm-glib
  -> ModemManager/plugins
  -> modem hardware
```

netifd is a separate authority for persistent connection intent. Its APN,
routes, addresses, DNS, PIN, and credentials are outside the API.

## Primary threats

1. A crafted blob causes out-of-bounds access before libubox validates it.
2. Unknown or duplicated fields bypass typed request policy.
3. A D-Bus/sysfs/device path or raw command crosses from the browser.
4. An opaque ID is predictable, reused after replug, or retargets a replacement
   modem/SMS.
5. A stale callback mutates or reports success for the wrong generation.
6. Concurrent SMS/band/PCI writes race ModemManager or each other.
7. Timeout or transport loss is mistaken for a definitely failed dispatched
   write, causing a blind retry.
8. A band choice disables the current allowed radio family or disconnects WAN.
9. A guessed PCI lock/clear/reset sequence leaves the router offline or camps
   on an unintended cell.
10. Malformed or oversized SMS/cell data consumes memory or reaches the DOM.
11. SMS send retries duplicate a chargeable/private message.
12. Logs, general status, or binary/PDU fields leak phone numbers or content.
13. Schema drift leaves old UI mutation controls enabled.
14. Broad ACL, shell, file, cgi-io, or UCI grants expand compromise impact.
15. A second dialer, direct TTY reader, or SMS tool races ModemManager.

## Controls

### Parsing and API shape

- Every untrusted blob attribute passes structural length validation before
  `blobmsg_name`, type, or data access.
- Requests accept exact typed fields plus at most one validated canonical
  `ubus_rpc_session`; malformed, missing, duplicate, and unknown fields fail.
- The base method table is fixed to seven schema-2 methods.
- Raw AT, D-Bus/sysfs/device paths, shell/process execution, arbitrary ubus,
  and bearer lifecycle calls are absent.
- Text is UTF-8/control-character checked and bounded before serialization.

Malformed-blob behavior is tested against pinned libubox source, including bad
length, bad name termination, duplicate fields, and malformed session data.

### Identity and asynchronous lifetime

- Modem, SMS, and client-token IDs use kernel CSPRNG bytes and canonical opaque
  formats.
- Every write carries modem generation; SMS also carries
  `messaging_generation`.
- Removal marks objects non-live, cancels operations, and creates a new opaque
  identity on reprobe.
- Callbacks revalidate the original live proxy and generation.
- All D-Bus work is asynchronous and cancellable with bounded timeouts.
- A dispatched send/band operation with uncertain completion returns
  `outcome_unknown`, is not automatically retried, and requires refreshed live
  state before another request.

### Ownership and concurrency

- ModemManager exclusively owns modem ports, SIM, SMS, radio, and bearers.
- netifd exclusively owns persistent connection configuration and network
  state.
- SMS, band, and expert PCI mutations share one per-modem single-flight lock.
  The PCI coordinator preserves hardware-slot exclusion across reset/reprobe.
- Cell scan is blocked while a per-modem mutation is active and is rate-limited
  to one attempt per minute.
- Band Lock uses only standard `SetCurrentBands`, exact supported/current-mode
  validation, confirmation, cooldown, and a prominent WAN interruption warning.

### Hardware and PCI gates

- Mutations require exact live L850-GL, Fibocom plugin, MBIM composition, and
  USB `2cb7:0007` attestation.
- The expert object is absent from base binaries and has a separate ACL.
- Standard GetCellInfo is tried asynchronously before any fallback.
- Standard cell results are limited to 64 LTE records, PCI 0..503, valid
  EARFCN-to-live-supported-band mappings, and finite metrics.
- The vendor parser accepts LTE types 4/5 only, rejects sentinels in exported
  PCI/EARFCN/RSRP/RSRQ, permits only reviewed sentinels in discarded fields,
  and rejects truncation, extra fields, encoding errors, overflow, and
  oversized output.
- The firmware allowlist contains exactly `18500.5001.00.05.27.30`, added only
  after the dated live command/recovery matrix. Other revisions cannot use the
  XMCI fallback or any mutation.
- Commands are compiled-in fixed grammar built only from typed bounded
  integers; no browser command, path, wildcard, RAT, SIM ID, or band encoding
  is accepted.
- Exact acknowledgement is followed by fixed reset, re-attested hardware-slot
  replacement, registration, NVM state, and (for set) serving-cell checks.
  No post-reprobe write is issued.
- A mutation cannot report verified success until all applicable
  postconditions pass; post-dispatch ambiguity is `outcome_unknown` with no
  automatic retry.

### SMS safety and privacy

- Inventory is bounded to the newest 1,024 messages; each API page is at most
  100 entries.
- Outbound text is valid UTF-8 and bounded by character and byte limits.
- Send uses Messaging.Create/Sms.Send; delete uses Messaging.Delete and a
  confirmation dialog. There is no direct PDU or SMS-tool fallback.
- Client tokens are bound to a digest. At most 64 entries are retained for up
  to 300 seconds; capacity eviction and restart are documented honestly.
- Unknown send outcome is cached and never resent automatically.
- Phone number/body may appear only in an authorized SMS response and UI.
  They are excluded from normal logs, Overview, diagnostics, process arguments,
  binary payloads, and raw PDU exports.

### Frontend and ACL

- LuCI requires schema 2 and complete typed success objects. Unknown schema or
  malformed data disables every mutation.
- Cell records are structurally validated again before rendering.
- DOM nodes are built without `innerHTML`; private data is not written to
  localStorage or console output.
- Five ACL groups grant exact read/write ubus methods only. There are no
  wildcards, filesystem, cgi-io, shell/file execution, or UCI-write grants.

## Residual risk and validation boundary

ModemManager, modem firmware, rpcd, and authenticated LuCI are privileged
dependencies. A browser poll can lag state by up to its interval. Cancellable
D-Bus APIs cannot prove rollback after a write reached hardware. In-memory SMS
dedupe cannot provide exactly-once delivery across eviction or restart.

The PCI command/recovery matrix is live-validated only for one exact
hardware/firmware tuple; it does not establish behavior for another firmware.
Unavailable-cell timeout, unplug mid-operation, ModemManager restart
mid-state-machine, and full-router reboot persistence remain unverified. The
schema-1 v0.2 read/incoming-SMS evidence still does not establish current live
SMS send/delete, Band Lock mutation, or pagination beyond 100 stored messages.
