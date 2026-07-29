<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Ubus API 1.0.0-alpha

The public contract is schema 4. `l850gl.mm` has exactly eight methods. The
optional `l850gl.mm.l850` object has five expert methods and is absent from a
base build.

## Common rules

Every reply contains:

```json
{
  "schema": 4,
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

Consumers must reject a missing/mistyped envelope, any schema other than 4,
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

## Base object: `l850gl.mm`

### `list_modems()`

No product arguments. Returns `modems`, newest live inventory only:

```json
{
  "schema": 4,
  "generated_at": 1780000000,
  "ok": true,
  "modems": [{
    "modem_id": "l850gl-<opaque>",
    "generation": 7,
    "manufacturer": "sanitized manufacturer text",
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
- `identity.imei`: full ModemManager equipment identifier, sanitized to at most
  64 Unicode code points;
- `usb_mode`: normalized `mbim`, `ncm`, or `unknown` composition;
- `modem`: normalized `state` and `power`, plus nullable typed `voltage_mv`;
- `sim`: boolean `present`, normalized `lock`, full `number` selected
  deterministically from at most 16 ModemManager OwnNumbers entries, and full
  cached `imsi`/`iccid`; number is bounded to 32 code points and identifiers to
  64, with an empty string when unavailable;
- `network`: `operator`, `registration`, `roaming`, and `access` array;
- `signal`: quality/recent and available `rsrp`, `rsrq`, `sinr` numbers;
- `bearer`: boolean `connected` and sanitized `interface`;
- `current_bands`: canonical ModemManager band names;
- `serving_cell`: `state` and `reason`; an `available` object also has validated
  `earfcn`, `pci`, `band`, and optional finite `rsrp`/`rsrq`;
- `capabilities`: `sms`, `band_lock`, and `pci_lock` feature objects;
- `warnings`: bounded machine-readable dependency/capability warnings.

The full IMEI, SIM number, IMSI, and ICCID are a schema-4 product-owner override
and are disclosed only by this authenticated Overview method. They must never
be logged, stored in fixtures/evidence, copied into list results, or exposed by
diagnostics. The response does not contain raw ports or paths, IP addresses,
gateway, DNS, APN, credentials, raw modem output, or a diagnostic dump.
`voltage_mv` is populated only from a valid, generation-bound expert-build
`AT+CBC` cache entry. A base build, missing cache, timeout, or malformed reply
serializes it as null without invalidating the rest of Overview; raw command
text is never returned.

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

## Expert object: `l850gl.mm.l850`

The object exists only when `L850GL_MM_EXPERT` is compiled. All methods
require exact current L850-GL/upstream-plugin/MBIM/`2cb7:0007` attestation and reject
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

### `get_carrier_info(modem_id, generation)`

This read-only expert method is absent from a base build. It requires exact
L850-GL/upstream-plugin/MBIM/`2cb7:0007` attestation, the sole allowlisted firmware,
live supported LTE bands, and the current generation. It dispatches only the
compiled-in `AT+GTCAINFO?` query through asynchronous ModemManager; no request
field can select or alter a command.

Only one carrier query may run per modem and it is mutually excluded with a
cell scan, voltage refresh, or any modem mutation. An overlap returns retryable `busy` without a
guessed duration. The operation timeout is 20 seconds and the ModemManager
command timeout is 15 seconds. Every terminal completion starts a five-second
cooldown. During cooldown, retryable `rate_limited` includes an accurate,
ceil-rounded `retry_after_ms`; rejected requests do not extend the deadline.
Removal, transport loss, cancellation, proxy replacement, or generation change
fails closed.

Success is bounded typed data only:

```json
{
  "schema": 4,
  "generated_at": 1780000000,
  "ok": true,
  "modem_id": "l850gl-<opaque>",
  "generation": 7,
  "state": "available",
  "source": "modemmanager",
  "method": "l850-gtcainfo",
  "active_bands": [5, 3],
  "primary": {
    "index": 1,
    "band": 5,
    "earfcn": 2450,
    "pci": 1,
    "dl_bandwidth_mhz": 10,
    "ul_bandwidth_mhz": 10
  },
  "secondary": [{
    "index": 2,
    "band": 3,
    "earfcn": 1325,
    "pci": 2,
    "dl_bandwidth_mhz": 20,
    "ul_bandwidth_mhz": null
  }],
  "active_carriers": 2
}
```

`active_bands` is the unique set of bands in the active carriers, not the Band
Lock configuration. `primary` must be index 1. `secondary` contains only active
index 2..8 slots; an exact inactive sentinel is counted in the declared modem
response but omitted here. `active_carriers` is therefore exactly one plus the
number of returned secondaries. Every carrier requires band 1..85, PCI 0..503,
a live SupportedBands match, and a reviewed own-band DL EARFCN plus numeric DL
bandwidth of 1.4, 3, 5, 10, 15, or 20 MHz. The primary additionally requires
an own-band UL EARFCN and numeric UL bandwidth from the same set. On the sole
allowlisted firmware, an active secondary is downlink-only: its raw UL
bandwidth must be the exact `255` sentinel and its raw UL EARFCN must equal the
validated primary UL EARFCN. Only then is its public `ul_bandwidth_mhz`
serialized as explicit `null`; no bandwidth is inferred. A numeric or missing
secondary UL bandwidth is rejected. An inactive secondary must match every
sentinel field and must also repeat the validated primary UL EARFCN.

The parser accepts at most 4,096 response bytes and eight declared slots. It
requires the 14-field primary grammar and 10-field secondary grammar, exact
slot count/index uniqueness, one active primary, and no duplicate public
carrier tuple. Sentinel, copied-primary-UL mismatch, unverified secondary
uplink, range, count, field-shape, overflow, unexpected-line, and band mismatch
errors reject the complete response. Although primary cellular identity and
signal fields are validated structurally, raw response, MCC/MNC/TAC/cell ID,
RSRP, RSRQ, and SINR are not exported. On an otherwise valid active secondary,
SINR code `127` is the sole live-verified unavailable-metric sentinel and is
accepted without inventing a numeric SINR; it remains invalid on the primary.

LuCI may use the validated primary carrier as a display-only serving EARFCN/PCI
fallback when `get_overview.serving_cell` is unavailable. This does not mutate
the backend serving-cell cache and is never attempted from a rejected or
malformed carrier response.

B29 and B32 are downlink-only LTE bands whose active `GTCAINFO` uplink/sentinel
representation has not been captured on the allowlisted firmware. An active
B29/B32 record therefore fails closed even if ModemManager lists that band in
SupportedBands. Admission requires a future sanitized live capture plus parser
fixtures; no value is guessed from 3GPP tables or reference-project code.

### `set_cell_lock(modem_id, generation, earfcn, pci?, confirm)`

### `clear_cell_lock(modem_id, generation, confirm)`

Confirmation is mandatory. EARFCN must map to a live supported band; optional
PCI is 0..503. The implementation validates typed input, exact hardware,
firmware, cooldown, and the shared mutation lock, then dispatches only the
compiled-in set or clear tuple. Exact command acknowledgement starts a
ten-second pre-reset verification deadline: two consecutive matching NVM reads
one second apart are required before one fixed reset. Hardware-slot
replacement correlation and registration follow. Post-reset NVM observations
use another ten-second deadline; an already dispatched query retains its
five-second command timeout, but a result after the stage deadline is rejected.
Set additionally verifies the serving EARFCN and optional PCI. Only the
read-only NVM query may repeat. The set/clear tuple and reset are never resent
automatically.

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
coordinator can establish all postconditions. Failures during either bounded
NVM window include top-level `verification_stage` as `pre_reset_nvm` or
`post_reset_nvm`; malformed data fails immediately, while a valid mismatch is
polled only until that stage's deadline.
The existing 130-second overall mutation timeout remains authoritative and may
end the deferred request before the sum of all worst-case phase deadlines.

## ACL split

| ACL | Access |
|---|---|
| `luci-app-l850gl-mm-overview` | read base `list_modems`, `get_overview`, `get_lock_status`; read expert `get_carrier_info` when that object exists |
| `luci-app-l850gl-mm-sms-read` | read `list_sms` |
| `luci-app-l850gl-mm-sms-write` | write `send_sms`, `delete_sms` |
| `luci-app-l850gl-mm-lock-band` | write `set_bands`, `set_modes` |
| `luci-app-l850gl-mm-lock-pci-expert` | read `cell_scan`, `get_carrier_info`, `cell_lock_status`; write `set_cell_lock`, `clear_cell_lock` |

There are no wildcard, file, filesystem, shell, cgi-io, or UCI permissions.

## Deliberately absent API

Schema 4 has no `get_status`, `get_capabilities`, `set_radio`, `reset`,
`set_primary_sim_slot`, dial/connect/disconnect/bearer methods, generic command,
device/path input, arbitrary UCI mutation, eSIM operation, rescan, or diagnostic
dump. The only persistent configuration mutation is the exact two-option
`set_modes` contract above.
