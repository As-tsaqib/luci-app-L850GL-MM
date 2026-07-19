<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Target base ubus API

Status: PRD 3.1 design contract; not implemented.

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
- `ambiguous_device`;
- `unsupported`;
- `not_ready`;
- `busy`;
- `timeout`;
- `permission_denied`;
- `operation_failed`;
- `dependency_unavailable`;
- `internal_error`.

Unknown input fields are rejected. Empty input means `{}`, not arbitrary JSON.

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
  "openwrt": {"network": "wan", "up": true},
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
- `signal`: quality and per-technology RSSI/RSCP/ECIO/RSRP/RSRQ/SNR where live;
- `cell`: standard ModemManager cell information or an unsupported reason;
- `bearers`: connection, interface, IP method/address/prefix/gateway/DNS/MTU;
- `openwrt`: matching netifd logical-interface state and counters;
- `diagnostics`: dependency versions/flags and last normalized error.

Runtime port names may be displayed but are never round-tripped as selectors.
Secret network credentials are never returned.

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
`unknown`.

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
masked or authorized number, text, timestamp, PDU type, delivery state, and
message reference where reported by ModemManager. The API does not invent
multipart part/count metadata: ModemManager exposes a combined SMS object and
uses `receiving` versus `received` to indicate completeness. It never contains
raw PDU, SMSC secrets, or D-Bus paths.

## SMS write methods

### `send_sms`

Input:

```json
{
  "modem_id": "fibocom-<opaque>",
  "generation": 42,
  "recipient": "+<E.164>",
  "text": "hello",
  "client_token": "opaque-ui-generated-token"
}
```

Recipient syntax and text byte/codepoint length are bounded. The bridge creates
an SMS with the Messaging API and calls `Send`. `client_token` is used for a
short bounded deduplication window so a UI retry does not trivially send twice.
The response contains the resulting opaque `sms_id` and normalized state.

Neither recipient nor text is logged or placed in process argv.

### `delete_sms`

Input:

```json
{
  "modem_id": "fibocom-<opaque>",
  "generation": 42,
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
bridge invokes standard `SetCurrentBands` and does not create `AT+XACT`.

### `set_radio`

Input:

```json
{"modem_id": "fibocom-<opaque>", "generation": 42, "enabled": false, "confirm": true}
```

This calls the standard Modem enable/disable API. It is not bearer disconnect
and does not edit netifd intent.

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

## Deliberately absent methods

The object has no methods for:

- connect, disconnect, create bearer, delete bearer, attach, APN, or route;
- persistent allowed/preferred mode changes; Settings uses the exact network
  UCI section and netifd apply path;
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

Target ACL groups:

- `luci-app-fibocom-status`: read list/overview/status/capabilities;
- `luci-app-fibocom-sms-read`: read `list_sms`;
- `luci-app-fibocom-sms-write`: write `send_sms`/`delete_sms`;
- `luci-app-fibocom-radio`: write bands/radio/reset/SIM slot;
- `luci-app-fibocom-l850-expert`: conditional typed cell methods only;
- `luci-app-lpac`: unchanged, owned by the optional dependency.

No Fibocom ACL grants `cgi-io`, `file.exec`, wildcard ubus/UCI, raw D-Bus, or
filesystem access. Menu entries depend on the narrow ACL they actually need.
