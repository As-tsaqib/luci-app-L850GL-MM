<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Base ubus API (v0.2.0 beta)

Status: schema 1 contract. Read-only status, native SMS, and the reviewed
standard Advanced methods are implemented.

Object: `fibocom.mm`

This is a typed facade over standard ModemManager libmm-glib/D-Bus APIs. It is
not a generic D-Bus proxy and has no bearer connect/disconnect methods.
Vendor PCI/EARFCN methods are deliberately outside this base object; their
conditional expert contract is documented in `pci-cell-lock.md`.

## Common envelope

Successful replies contain:

```json
{
  "schema": 1,
  "generated_at": 1784419200,
  "ok": true
}
```

Application failures contain:

```json
{
  "schema": 1,
  "generated_at": 1784419200,
  "ok": false,
  "error": {
    "code": "unsupported",
    "message": "This operation is not supported by the modem",
    "retryable": false
  }
}
```

The bridge maps D-Bus errors to a stable allowlist and never returns raw D-Bus
paths, GError text containing secrets, command output, or stack traces.

Initial error codes:

- `invalid_argument`;
- `not_found`;
- `device_gone`;
- `stale_identity`;
- `stale_generation`;
- `stale_messaging_generation`;
- `stale_cursor`;
- `ambiguous_device`;
- `unsupported`;
- `not_ready`;
- `busy`;
- `timeout`;
- `outcome_unknown`;
- `storage_full`;
- `permission_denied`;
- `managed_by_netifd`;
- `operation_failed`;
- `dependency_unavailable`;
- `internal_error`.

Unknown input fields are rejected. Empty input means `{}`, not arbitrary JSON.
LuCI's authenticated JSON-RPC controller appends one transport-only
`ubus_rpc_session` string to every ubus argument object. The bridge accepts and
ignores exactly one canonical 32-hex instance of that field; it is not a product
argument and is never returned. Unknown, duplicate, or malformed fields remain
invalid.

## Identifiers

### `modem_id`

An opaque random 128-bit token created when the bridge admits a ModemManager
object. It is not derived from `DeviceIdentifier`, IMEI, a physical path, a
D-Bus path, or a port. It changes after object removal/re-addition and after a
bridge restart. ModemManager index, D-Bus path, primary port, `/dev/*`, and
sysfs path are never accepted from the browser.

Every exported modem snapshot also has a process-local integer `generation`.
Every mutation must include exact `{modem_id, generation}`. The bridge verifies
both immediately before dispatch and again before accepting an asynchronous
completion. A new object at the same `/Modem/N` or physical port cannot satisfy
an old request. Secure-random failure fails closed.

### `sms_id`

An opaque bridge identifier mapped to one live ModemManager SMS object and its
owning `{modem_id, generation}`. It is valid only while that generation is
live. Raw SMS D-Bus paths are not returned or accepted.

### `messaging_generation`

The bridge increments a second process-local generation whenever the
ModemManager Messaging interface or active SIM context changes. SMS mutations
must include exact `{modem_id, generation, messaging_generation}`. This stops a
compose form opened for one SIM/eSIM profile from being sent through another
profile even when ModemManager keeps the same Modem object.

## Read methods

### `list_modems`

Input: `{}`

Output adds:

```json
{
  "modems": [
    {
      "modem_id": "fibocom-<opaque>",
      "generation": 42,
      "manufacturer": "Fibocom Wireless Inc.",
      "model": "L850-GL",
      "plugin": "fibocom",
      "composition": "mbim",
      "state": "connected",
      "supported": true,
      "support_reason": "l850-mbim",
      "last_changed_at": 1784419198
    }
  ],
  "dependencies": {
    "modemmanager": "available",
    "netifd_proto": "available",
    "fibocom_plugin": "available",
    "mbim": "available"
  }
}
```

Unsupported Fibocom devices may be listed read-only with a reason. Non-Fibocom
devices are excluded by default.

### `get_overview`

Input:

```json
{"modem_id": "fibocom-<opaque>"}
```

Output is a compact snapshot:

```json
{
  "modem_id": "fibocom-<opaque>",
  "generation": 42,
  "freshness": "fresh",
  "modem": {"model": "L850-GL", "revision": "<sanitized>", "state": "connected"},
  "sim": {"present": true, "slot": 1, "lock": "none"},
  "network": {"registration": "home", "operator": "Example", "access": ["lte"]},
  "signal": {"quality": 72, "recent": true},
  "bearer": {"connected": true, "interface": "wwan0", "ip_families": ["ipv4"]},
  "openwrt": {"state": "unavailable", "network": "", "up": false},
  "warnings": []
}
```

Identifiers are masked by default.

### `get_status`

Input: exact `modem_id` object. Output includes its current `generation`.

Output adds normalized sections:

- `general`: manufacturer, model, revision, plugin, drivers, masked equipment
  identifier, state and failure reason;
- `ports`: sanitized port name/type/role for display only;
- `sim`: slots, primary slot, masked ICCID/IMSI/MSISDN, operator and locks;
- `network`: 3GPP registration, operator, roaming and access technologies;
- `radio`: normalized power state, supported/current bands, band-selection
  policy, supported mode combinations, and current allowed/preferred modes;
- `signal`: quality and per-technology RSSI/RSCP/ECIO/RSRP/RSRQ/SNR where live;
- `cell`: standard ModemManager cell information or an unsupported reason;
- `bearers`: connection, interface, IP method/address/prefix/gateway/DNS/MTU;
- `openwrt`: reserved netifd runtime status. Schema 1 currently reports
  `state=unavailable`; the safe UCI ownership correlation is provided
  separately by `network_binding` and does not claim that the interface is up;
- `network_binding`: a secret-free result for the exact matching
  `proto modemmanager` UCI section. It contains only lookup state, a safe named
  section, validated allowed/preferred-mode strings and `disable_modem`; it
  never returns device paths, APN, PIN, usernames, or passwords;
- `diagnostics`: dependency versions/flags and last normalized error.

Runtime port names may be displayed but are never round-tripped as selectors.
Secret network credentials are never returned.

Dynamic OpenWrt logical-interface state, uptime, availability, and RX/TX
counters are not implemented in schema 1. Consumers must not interpret the UCI
binding as proof that the interface is currently up.

### `get_capabilities`

Input: exact `modem_id` object.

Output:

```json
{
  "modem_id": "fibocom-<opaque>",
  "generation": 42,
  "capabilities": {
    "mbim_data": {"state": "available", "mutable": false, "reason": "modemmanager"},
    "ncm_data": {"state": "unsupported", "mutable": false, "reason": "missing-l850-mm-backend"},
    "messaging": {"state": "available", "mutable": true, "reason": "dbus-interface-present"},
    "bands": {"state": "available", "mutable": true, "reason": "supported-bands-present"},
    "modes": {"state": "available", "mutable": false, "reason": "network-uci-owned"},
    "reset": {"state": "available", "mutable": true, "reason": "standard-method"},
    "sim_slot": {"state": "unsupported", "mutable": false, "reason": "single-slot"},
    "cell_info": {"state": "unsupported", "mutable": false, "reason": "not-advertised"},
    "l850_cell_extension": {"state": "unavailable", "mutable": false, "reason": "expert-build-disabled"}
  }
}
```

Stable states are `available`, `unavailable`, `unsupported`, `busy`, and
`unknown`. Standard mutation capabilities also report `busy` and a bounded
`retry_after_ms` while the shared per-modem mutation lock or an Advanced
cooldown is active. A capability is mutable only for a live L850-GL MBIM
object whose sysfs device attests to exact USB ID `2cb7:0007`.

### `list_sms`

Input:

```json
{
  "modem_id": "fibocom-<opaque>",
  "folder": "all",
  "limit": 100,
  "cursor": ""
}
```

`folder` is one of `all`, `inbox`, `outbox`, `draft`, or `unknown`. It is a
bridge view derived from the standard `PduType` and `State` fields. ModemManager
1.24 has no `FAILED` SMS state; a send failure is returned as the result of the
send operation and must not be invented as a persistent message state.
Pagination values are bounded.

Output adds `messages`, each containing opaque `sms_id`, direction/state,
masked or authorized number, text, timestamp, discharge timestamp, storage,
PDU type, delivery state, message reference, and a binary-data presence flag
where reported by ModemManager. Binary payload bytes are never exported; the
LuCI view distinguishes a binary-only SMS from an empty text message. The API
does not invent multipart part/count metadata: ModemManager exposes a combined
SMS object and uses `receiving` versus `received` to indicate completeness. It
never contains raw PDU, SMSC secrets, or D-Bus paths.

The response also contains `generation`, `messaging_generation`, cache
`revision`, `cache_state`, `has_more`, and an opaque `next_cursor`.
`next_cursor` is the opaque `sms_id` of the last returned message when
`has_more=true`; otherwise it is empty. A later request re-filters and re-sorts
the current cache, locates that SMS ID, and resumes after it. The cursor does
not contain generation, revision, or filter metadata. It is accepted only
while the anchor remains in the selected modem's current filtered view;
otherwise the bridge returns `stale_cursor`.

Pagination is therefore live and anchor-based, not a revision-bound snapshot.
Clients that require a stable traversal must compare `generation`,
`messaging_generation`, and `revision` across pages and restart with an empty
cursor if any marker changes. `revision` is an observation marker, not an input
precondition or part of the cursor. It may advance for signals, property
changes, and full reconciliation, including a reconciliation that leaves the
visible inventory unchanged.

Cache synchronization is automatic. ModemManager `Added` and `Deleted`
signals and per-SMS property changes trigger refreshes; a 30-second full-list
reconciliation repairs missed/delayed signals. The LuCI SMS view reads that
cache every 10 seconds while open. A fetched update is held while a compose
control has focus and rendered when focus leaves. Inventories are ordered
newest-first before the 1,024-entry cache bound. This is bounded refresh, not
an immediate browser push guarantee.

## SMS write methods

### `send_sms`

Input:

```json
{
  "modem_id": "fibocom-<opaque>",
  "generation": 42,
  "messaging_generation": 7,
  "recipient": "+<E.164>",
  "text": "hello",
  "client_token": "opaque-ui-generated-token"
}
```

Recipient syntax and text byte/codepoint length are bounded. The bridge creates
an SMS with the Messaging API and calls `Send`. `client_token` is used for an
in-memory five-minute deduplication window so an immediate UI retry does not
trivially send twice. The cache is lost on bridge restart and does not provide
exactly-once delivery after the window. The response contains the resulting
opaque `sms_id` and normalized state.

Neither recipient nor text is logged or placed in process argv.

The bridge sets an explicit long D-Bus timeout because sending may take several
minutes. Once `Send` has been dispatched, a timeout or transport loss is
reported as `outcome_unknown` with `retryable=false`; LuCI must refresh the
message list and must not automatically create another send attempt.

### `delete_sms`

Input:

```json
{
  "modem_id": "fibocom-<opaque>",
  "generation": 42,
  "messaging_generation": 7,
  "sms_id": "sms-<opaque>",
  "confirm": true
}
```

The bridge verifies that the live SMS belongs to the selected modem. Received
and sent SMS are never confused with a newly reused object path.

## Radio write methods

### `set_bands`

Input:

```json
{
  "modem_id": "fibocom-<opaque>",
  "generation": 42,
  "bands": ["eutran-1", "eutran-3", "eutran-8"],
  "confirm": true
}
```

Every band must be present in the live supported set. Restore automatic uses
the single canonical value `any`; it cannot be mixed with explicit bands. The
bridge also prevalidates explicit selections against the current allowed-mode
families: every currently allowed cellular family needs at least one selected
band, and a band for a disabled family is rejected. The bridge invokes
standard `SetCurrentBands` and does not create `AT+XACT`.

### `set_radio`

Input:

```json
{"modem_id": "fibocom-<opaque>", "generation": 42, "enabled": false, "confirm": true}
```

This calls the standard Modem enable/disable API. It is not bearer disconnect
and does not edit netifd intent. Direct radio mutation is allowed only when the
exact modem has no matching `proto modemmanager` network section. A unique
binding returns `managed_by_netifd`; an ambiguous binding or a libuci lookup
failure also fails closed. Persistent radio/connection intent is changed from
Network → Interfaces so netifd cannot immediately fight a direct Disable.

### `reset`

Input:

```json
{"modem_id": "fibocom-<opaque>", "generation": 42, "confirm": true}
```

The bridge invokes the standard reset method once, establishes a cooldown, and
returns a resetting result. Object removal invalidates the request token. LuCI
must refresh `list_modems` and use the newly issued ID; the old operation may
observe recovery read-only but cannot write to the replacement object. The
bridge never sends a fallback chain of vendor reset commands.

### `set_primary_sim_slot`

Input:

```json
{"modem_id": "fibocom-<opaque>", "generation": 42, "slot": 2, "confirm": true}
```

Only available when ModemManager advertises multiple physical slots and the
slot is valid. eSIM profile selection is not represented as a SIM-slot call.

All four methods reject unknown, missing, duplicate, or mistyped fields. They
require exact `{modem_id, generation}` and `confirm=true`, take the same
per-modem single-flight mutation lock used by SMS, and revalidate liveness and
generation in their asynchronous callback. Successful replies contain
`accepted=true`; timeout or transport loss after dispatch is normalized to
`outcome_unknown` and is never automatically retried. Bands/radio use a short
cooldown; reset/SIM-slot use a longer cooldown because either may trigger a
reprobe. No callback is allowed to retarget a replacement ModemManager object.

## Deliberately absent methods

The object has no methods for:

- connect, disconnect, create bearer, delete bearer, attach, APN, or route;
- persistent allowed/preferred mode changes; Settings identifies the exact
  network UCI section read-only and links to netifd's standard editor/apply
  path;
- rescan/hotplug or runtime path selection;
- arbitrary AT or `mmcli --command`;
- arbitrary D-Bus access;
- cell scan fallback, PCI/EARFCN lock, or NVM in the base build/object;
- USB composition switch;
- IMEI, factory reset, firmware, or host-controller reset;
- eSIM/lpac operations.

eSIM remains on object `luci.lpac` with the ACL and method contract of the
user's `luci-app-lpac` repository.

An opt-in build may compile object `fibocom.mm.l850` and its separate expert
ACL. The base build does not compile or expose that object, and keeps
ModemManager AT-via-D-Bus disabled. The expert object shares the same daemon,
object cache, generation checks, and per-modem operation lock; it never exposes
a raw command method. See `pci-cell-lock.md`.

## ACL split

ACL groups:

- `luci-app-fibocom-status`: read list/overview/status/capabilities;
- `luci-app-fibocom-sms-read`: read `list_sms`;
- `luci-app-fibocom-sms-write`: write `send_sms`/`delete_sms`;
- `luci-app-fibocom-radio`: write bands/radio/reset/SIM slot;
- `luci-app-fibocom-l850-expert`: conditional typed cell methods only;
- `luci-app-lpac`: unchanged, owned by the optional dependency.

No Fibocom ACL grants `cgi-io`, `file.exec`, wildcard ubus/UCI, raw D-Bus, or
filesystem access. Menu entries depend on the narrow ACL they actually need.
