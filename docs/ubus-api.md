<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Ubus API 0.3.0

The public contract is schema 2. `fibocom.mm` has exactly seven methods. The
optional `fibocom.mm.l850` object has four expert methods and is absent from a
base build.

## Common rules

Every reply contains:

```json
{
  "schema": 2,
  "generated_at": 1780000000,
  "ok": true
}
```

`generated_at` is Unix time in seconds. A failed product request has
`ok: false` and:

```json
{
  "error": {
    "code": "invalid_argument",
    "message": "safe human-readable text",
    "retryable": false
  }
}
```

Consumers must reject a missing/mistyped envelope, any schema other than 2,
and incomplete success objects. LuCI disables mutations on compatibility or
structural errors.

Requests reject missing, unknown, duplicate, or mistyped fields. rpcd may add
exactly one canonical 32-hex `ubus_rpc_session`; the parser validates
and ignores that transport field. No other transport or product field is
ignored. Each blob attribute is length/structure checked before its name,
type, or data is read.

`modem_id`, `sms_id`, scan cursors, and client tokens are opaque. They are not
D-Bus paths and must not be parsed. A modem-scoped success also contains the
current `modem_id` and `generation`. SMS writes additionally require the
current `messaging_generation`.

## Base object: `fibocom.mm`

### `list_modems()`

No product arguments. Returns `modems`, newest live inventory only:

```json
{
  "schema": 2,
  "generated_at": 1780000000,
  "ok": true,
  "modems": [{
    "modem_id": "fibocom-<opaque>",
    "generation": 7,
    "manufacturer": "Fibocom Wireless Inc.",
    "model": "L850-GL",
    "revision": "sanitized firmware text",
    "state": "connected",
    "power": "on"
  }]
}
```

### `get_overview(modem_id)`

Returns a compact snapshot with these top-level objects:

- `identity`: sanitized `manufacturer`, `model`, and `revision`;
- `modem`: normalized `state` and `power`;
- `sim`: boolean `present` and normalized `lock`;
- `network`: `operator`, `registration`, `roaming`, and `access` array;
- `signal`: quality/recent and available `rsrp`, `rsrq`, `sinr` numbers;
- `bearer`: boolean `connected` and sanitized `interface`;
- `current_bands`: canonical ModemManager band names;
- `serving_cell`: `state` and `reason`, with EARFCN/PCI only after validation;
- `capabilities`: `sms`, `band_lock`, and `pci_lock` feature objects;
- `warnings`: bounded machine-readable dependency/capability warnings.

The response does not contain raw ports, paths, IMEI/IMSI/ICCID, IP addresses,
gateway, DNS, APN, credentials, or a diagnostic dump.

### `get_lock_status(modem_id)`

Returns:

```text
supported_bands[]
current_bands[]
band_selection = automatic | explicit | unknown
current_modes = { known, allowed[], preferred }
band_lock = { state, mutable, reason, busy, optional retry_after_ms }
pci_lock = { state, mutable, reason }
```

The base build always reports `pci_lock.state = unsupported_build` and does
not publish the expert object. An expert binary reports
`unsupported_firmware` while its mutation allowlist is empty.

### `set_bands(modem_id, generation, bands, confirm)`

`confirm` must be true. `bands` is either exactly `["any"]`, or a non-empty
array of unique canonical explicit bands. `any` cannot be mixed with explicit
values. Every explicit band must be in live SupportedBands and in a family
allowed by the modem's current mode mask.

After exact hardware attestation and shared-lock admission, the bridge calls
asynchronous `SetCurrentBands`. Success contains:

```json
{
  "accepted": true,
  "operation": "set_bands",
  "cooldown_ms": 10000
}
```

A timeout, removal, or transport failure after dispatch can be
`outcome_unknown`; the caller must refresh and must not retry blindly.

### `list_sms(modem_id, folder?, limit?, cursor?)`

`folder` defaults to `all` and is one of `all`, `inbox`, `outbox`, `draft`, or
`unknown`. `limit` defaults to 50 and is 1..100. Empty `cursor` starts at the
newest message. A non-empty cursor is the last `sms_id` returned by the prior
page; if it no longer exists in that filtered view the call fails with
`stale_cursor` and pagination must restart.

The response contains:

```text
modem_id, generation, messaging_generation
revision, cache_state, cache_truncated
dedupe_capacity = 64
dedupe_window_seconds = 300
folder, limit, messages[], has_more, next_cursor
```

Each message contains opaque `sms_id`, normalized folder/direction/state,
sanitized number, UTF-8 text, `text_truncated`, timestamps, PDU type, delivery
state, message reference, storage, and `has_binary_data`. Binary bytes and raw
PDU are never returned. The backend cache is newest-first and bounded to 1,024
entries.

### `send_sms(modem_id, generation, messaging_generation, recipient, text, client_token)`

All fields are required. Recipient and UTF-8 text are validated; outbound text
is at most 1,600 Unicode characters and 6,400 bytes. `client_token` is a CSPRNG
opaque SMS-operation token. It is bound to the digest of recipient and body,
so reuse for different content is rejected.

The bridge calls Messaging.Create and then Sms.Send asynchronously. Success
contains `sms_id`, normalized `state`, and `deduplicated`. At most 64 token
records are retained; each retained record expires 300 seconds after its most
recent stored state, but capacity eviction may remove the oldest sooner.
Restart clears the cache. This is not an exactly-once guarantee. Uncertainty
after Sms.Send dispatch is `outcome_unknown` and is cached to prevent automatic
resend.

### `delete_sms(modem_id, generation, messaging_generation, sms_id, confirm)`

`confirm` must be true. The SMS must still belong to the selected live
inventory and must not be in `sending` state. The asynchronous
Messaging.Delete success contains `sms_id` and `deleted: true`.

## Expert object: `fibocom.mm.l850`

The object exists only when `FIBOCOM_MM_L850_EXPERT` is compiled. All methods
require exact current L850-GL/Fibocom/MBIM/`2cb7:0007` attestation and reject
stale identity or generation.

### `cell_lock_status(modem_id, generation)`

Returns the mutation state independently from scan capability:

```json
{
  "state": "unsupported_firmware",
  "mutable": false,
  "reason": "firmware-allowlist-empty",
  "scan": {
    "state": "available",
    "available": true,
    "reason": "standard-modemmanager-get-cell-info",
    "source": "modemmanager"
  }
}
```

Scan state may instead be `busy`, `not_ready`, or `rate_limited`; a rate-limited
status includes `retry_after_ms`. `scan.available` is true only for an allowed
button-driven standard scan attempt. It does not imply vendor fallback support
or mutation support.

### `cell_scan(modem_id, generation)`

Rate-limited to one attempt per modem per 60 seconds and never polled. It first
calls asynchronous ModemManager `GetCellInfo`. Success is:

```json
{
  "state": "scan_ready",
  "source": "modemmanager",
  "cells": [{
    "type": 4,
    "serving": true,
    "earfcn": 1300,
    "pci": 0,
    "band": 3,
    "rsrp": -91.0,
    "rsrq": -10.5
  }]
}
```

Only LTE type 4 serving and type 5 neighbor records are emitted. PCI is
0..503, including zero. EARFCN must map to a live supported LTE band. RSRP and
RSRQ are omitted when ModemManager reports its unavailable sentinel. More than
64 LTE records or malformed data fail closed. The operation has a 45-second
timeout, cancellation, and callback generation checks.

On the historically tested firmware, standard GetCellInfo was unsupported.
Because the vendor grammar/firmware/recovery tuple is unverified and the
allowlist is empty, that result is `unsupported_firmware`; no command is sent.

### `set_cell_lock(modem_id, generation, earfcn, pci?, confirm)`

### `clear_cell_lock(modem_id, generation, confirm)`

Confirmation is mandatory. EARFCN must map to a live supported band; optional
PCI is 0..503. The offline implementation validates these typed inputs, exact
hardware, firmware allowlist, and the shared mutation lock. The allowlist is
empty, so both methods currently return `unsupported_firmware` and dispatch no
lock, clear, reset, or recovery action.

The state policy models `available`, `scan_ready`,
`lock_applied_reset_required`, `resetting`, `applied_verified`,
`cleared_verified`, `reprobe_timeout`, `registration_timeout`,
`verification_mismatch`, and `outcome_unknown`, but no verified state is
reported without a live postcondition.

## ACL split

| ACL | Access |
|---|---|
| `luci-app-fibocom-overview` | read `list_modems`, `get_overview`, `get_lock_status` |
| `luci-app-fibocom-sms-read` | read `list_sms` |
| `luci-app-fibocom-sms-write` | write `send_sms`, `delete_sms` |
| `luci-app-fibocom-lock-band` | write `set_bands` |
| `luci-app-fibocom-lock-pci-expert` | read `cell_scan`, `cell_lock_status`; write `set_cell_lock`, `clear_cell_lock` |

There are no wildcard, file, filesystem, shell, cgi-io, or UCI permissions.

## Deliberately absent API

Schema 2 has no `get_status`, `get_capabilities`, `set_radio`, `reset`,
`set_primary_sim_slot`, dial/connect/disconnect/bearer methods, generic command,
device/path input, UCI mutation, eSIM operation, rescan, or diagnostic dump.
