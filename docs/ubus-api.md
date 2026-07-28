<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Ubus API 0.4.0

The public contract is schema 3. `fibocom.mm` has exactly eight methods. The
optional `fibocom.mm.l850` object has four expert methods and is absent from a
base build.

## Common rules

Every reply contains:

```json
{
  "schema": 3,
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

Consumers must reject a missing/mistyped envelope, any schema other than 3,
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
  "schema": 3,
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
- `serving_cell`: `state` and `reason`; an `available` object also has validated
  `earfcn`, `pci`, `band`, and optional finite `rsrp`/`rsrq`;
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
mode_policy = { state, mutable, reason, configured, allowed, preferred, busy,
                optional retry_after_ms }
band_lock = { state, mutable, reason, busy, optional retry_after_ms }
pci_lock = { state, mutable, reason }
```

The base build always reports `pci_lock.state = unsupported_build` and does
not publish the expert object. An expert binary reports `available` only for
the exact live-validated hardware/firmware tuple, `busy` during mutation or
cooldown, and `unsupported_firmware` for every other revision.

### `set_bands(modem_id, generation, bands, confirm)`

`confirm` must be true. `bands` is either exactly `["any"]`, or a non-empty
array of unique canonical LTE bands. `any` cannot be mixed with explicit
values. Every explicit band must be in live SupportedBands and the current mode
mask must allow 4G. For an explicit request, the backend retains every live
supported band from other currently allowed families before enforcing the
complete-family invariant and calling ModemManager.

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

### `set_modes(modem_id, generation, allowed, preferred, confirm)`

`confirm` must be true. `allowed` is exactly `3g`, `4g`, or canonical `3g|4g`.
`preferred` is `none`, `3g`, or `4g`; a single allowed mode requires `none`.
The bridge uses the modem's internal Device value to resolve exactly one named,
safe `config interface` with `proto modemmanager`. The request never accepts or
returns the section name or device path.

After hardware/generation/shared-lock admission, the bridge writes only
`allowedmode` and `preferredmode`, commits once, and verifies both values by
readback. It then invokes `network.reload` asynchronously. Success contains:

```json
{
  "accepted": true,
  "operation": "set_modes",
  "persisted": true,
  "activation": "reloaded",
  "cooldown_ms": 10000
}
```

`activation` is `reloaded`, `pending`, `failed`, or `outcome_unknown`.
Persistence is reported separately because a missing network object or lost
reload response does not undo verified UCI intent. The UI must refresh rather
than blindly repeat a saved request. Missing, anonymous, unsafe, or ambiguous
bindings fail closed. No APN, PIN, username, password, route, DNS, or other UCI
option is read into an API response or changed.

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
  "state": "available",
  "mutable": true,
  "reason": "live-validated-firmware-and-nvm-state",
  "lock": {
    "state": "configured_exact",
    "enabled": true,
    "postcondition_verified": false,
    "earfcn": 1300,
    "pci": 0,
    "band": 3,
    "source": "l850-nvm-via-modemmanager"
  },
  "scan": {
    "state": "available",
    "available": true,
    "reason": "standard-with-live-validated-xmci-fallback",
    "source": "modemmanager"
  }
}
```

Scan state may instead be `busy`, `not_ready`, or `rate_limited`. `busy` means
that a scan or a per-modem mutation is still active and does not include
`retry_after_ms`, because the completion time is not predictable. A
`rate_limited` status includes the ceil-rounded remaining cooldown in
`retry_after_ms`. The nested lock is a bounded NVM configuration observation
(`clear`, `configured_earfcn`, or `configured_exact`), not a serving-cell
postcondition. Unsupported firmware still advertises a standard-only scan when
ready.

### `cell_scan(modem_id, generation)`

Only one scan may be active per modem. A request received while that scan is
active returns retryable `busy` without `retry_after_ms` and does not dispatch a
second ModemManager operation. The scan is button-driven and never polled. It
first calls asynchronous ModemManager `GetCellInfo`. Success is:

```json
{
  "state": "scan_ready",
  "source": "modemmanager",
  "method": "l850-xmci",
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
timeout, cancellation, and callback generation checks. Every terminal scan
completion, whether success or normalized failure, starts a five-second
per-modem cooldown. A request during that cooldown returns retryable
`rate_limited` with an accurate, ceil-rounded `retry_after_ms`; rejected `busy`
and `rate_limited` requests do not restart the cooldown.

On the allowlisted firmware, standard GetCellInfo is unsupported and the
bridge falls back to its fixed XMCI query through ModemManager. `method` is
`standard-cell-info` or `l850-xmci`. Other firmware receives no vendor
fallback.

### `set_cell_lock(modem_id, generation, earfcn, pci?, confirm)`

### `clear_cell_lock(modem_id, generation, confirm)`

Confirmation is mandatory. EARFCN must map to a live supported band; optional
PCI is 0..503. The implementation validates typed input, exact hardware,
firmware, cooldown, and the shared mutation lock, then dispatches only the
compiled-in set or clear tuple. Exact command acknowledgement is followed by
fixed reset, hardware-slot replacement correlation, and registration. NVM is
always verified; set additionally verifies the serving EARFCN and optional
PCI.

The ubus request remains deferred through verification. Success therefore
contains the replacement's new opaque identity, not the stale input identity:

```json
{
  "ok": true,
  "modem_id": "<new 32-hex opaque id>",
  "generation": 9,
  "replacement": true,
  "accepted": true,
  "operation": "set_cell_lock",
  "state": "applied_verified",
  "cooldown_ms": 60000,
  "verification": {
    "registration": true,
    "nvm": true,
    "serving_cell": true,
    "earfcn": 1300,
    "pci": 0,
    "band": 3
  }
}
```

Clear success is `cleared_verified` and requires the exact NVM clear sentinel;
its verification object has `registration` and `nvm` only. A new write always
requires the returned replacement to be refreshed and confirmed as a new
request.

The state policy models `available`, `scan_ready`,
`lock_applied_reset_required`, `resetting`, `applied_verified`,
`cleared_verified`, `reprobe_timeout`, `registration_timeout`,
`verification_mismatch`, and `outcome_unknown`. Command acknowledgement alone
never yields a verified state. Transport loss, timeout, or ambiguous reset
completion after dispatch is non-retryable `outcome_unknown` unless the
coordinator can establish all postconditions.

## ACL split

| ACL | Access |
|---|---|
| `luci-app-fibocom-overview` | read `list_modems`, `get_overview`, `get_lock_status` |
| `luci-app-fibocom-sms-read` | read `list_sms` |
| `luci-app-fibocom-sms-write` | write `send_sms`, `delete_sms` |
| `luci-app-fibocom-lock-band` | write `set_bands`, `set_modes` |
| `luci-app-fibocom-lock-pci-expert` | read `cell_scan`, `cell_lock_status`; write `set_cell_lock`, `clear_cell_lock` |

There are no wildcard, file, filesystem, shell, cgi-io, or UCI permissions.

## Deliberately absent API

Schema 3 has no `get_status`, `get_capabilities`, `set_radio`, `reset`,
`set_primary_sim_slot`, dial/connect/disconnect/bearer methods, generic command,
device/path input, arbitrary UCI mutation, eSIM operation, rescan, or diagnostic
dump. The only persistent configuration mutation is the exact two-option
`set_modes` contract above.
