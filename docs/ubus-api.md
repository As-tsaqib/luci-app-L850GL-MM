<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# P0/P1 ubus API

Object: `fibocom`

Implemented methods are `list`, `status`, `capabilities`, `diagnostics`, and
`rescan`. Every normal or application-error reply contains:

```json
{
  "schema": 1,
  "shadow_mode": true
}
```

The daemon serves cached sysfs state. No method opens a modem port, probes AT or
MBIM, selects an NCM data interface, or changes networking.

## Shared device summary

A summary has these fields:

| Field | Type | P0/P1 meaning |
|---|---|---|
| `device_id` | string | opaque `l850-<sha256>` identity, with a slot suffix only on duplicate identities |
| `generation` | integer | changes when the cached topology/identity at a USB slot changes or is re-added |
| `present` | boolean | true for entries in the current cache; removed devices are omitted |
| `profile` | string | `fibocom-l850-gl` |
| `model` | string | `Fibocom L850-GL`, inferred from the USB profile |
| `model_confidence` | string | always `usb_id_only` in this phase |
| `identity_scope` | string | `usb-serial-hash` or fallback `path-scoped` |
| `composition` | string | `mbim` or `ncm` from exact VID/PID matching |
| `vid`, `pid` | string | normalized lowercase four-digit USB IDs |
| `physical_path` | string | bounded USB slot label, not an absolute sysfs path |
| `topology_status` | string | `complete`, `partial`, or `ambiguous` |
| `topology_reason` | string | stable machine-oriented classification detail |
| `ports` | object | cached names: `at_primary`, `at_secondary`, `ignored[]`, `wdm`, `netdevs[]` |

Empty string means a singleton port role was not found. Runtime node names are
observations, never persistent identifiers or accepted RPC selectors.

## Shared reconciliation object

`list`, `status`, `diagnostics`, and `rescan` include `reconcile`:

```json
{
  "scan_id": 4,
  "completed_at": 0,
  "device_count": 0,
  "added": 0,
  "removed": 0,
  "changed": 0,
  "unchanged": 0,
  "scan_in_progress": false,
  "scan_pending": true,
  "initialized": false,
  "ok": false,
  "error": ""
}
```

Values above illustrate types only. `completed_at` is Unix seconds after a
completed scan. `error` is empty on success and currently becomes
`sysfs-scan-failed` for a failed reconciliation; detailed filesystem errors are
logged locally rather than exposed through RPC.

## `list`

Input: no arguments.

Output:

```json
{
  "schema": 1,
  "shadow_mode": true,
  "devices": ["<shared device summary>"],
  "reconcile": {"...": "<shared reconciliation object>"}
}
```

## `status`

Input:

```json
{"device_id": "l850-<opaque digest>"}
```

No extra fields are allowed. Output nests the summary under `device`:

```json
{
  "schema": 1,
  "shadow_mode": true,
  "state": "shadow",
  "device": {"...": "<shared device summary>"},
  "reconcile": {"...": "<shared reconciliation object>"}
}
```

## `capabilities`

Input is the same exact `device_id` object as `status`.

Output contains `device_id`, `generation`, and a `capabilities` table. Every
feature is `{state, available, reason}`. Implemented feature names are:

- `shadow_inventory`;
- `mbim_topology`;
- `ncm_topology`;
- `bearer_connect`;
- `esim`;
- `usb_mode_switch`;
- `band_lock`;
- `sim_switch`;
- `cell_lock`.

`shadow_inventory` is available for an exact USB profile match. A topology
feature may be available when its sysfs shape is complete, but that is not a
dial or hardware-validation claim. `bearer_connect`, eSIM, and every mutation
remain unavailable. Clients must use the typed state and reason rather than
infer support from composition.

## `diagnostics`

Input: empty object or one `device_id`. No extra fields are allowed. If an ID is
provided, the `devices` array contains only that cached device.

Output contains:

- `daemon`: `mode: shadow`, live `ubus_connected`,
  `ownership: not-probed`, `fibocomd_claims_device: false`, and false
  `opens_tty`, `opens_wdm`, and `changes_network` flags;
- `profile`: profile ID/display name plus `loaded: true`,
  `schema_validated: true`, `hardware_validated: false`, and
  `match_confidence: usb_id_only`;
- `devices`: summaries extended with an `interfaces` array;
- `reconcile`: the shared scan object.

Each interface entry exposes `number`, numeric `index`, sanitized `driver`,
`role`, and `ttys`/`wdms`/`netdevs` arrays. Each port entry exposes only `name`,
`interface_number`, numeric `interface_index`, and `driver`. Canonical absolute
sysfs paths, USB serials, IMEI/IMSI/ICCID/EID, command output, arbitrary file
contents, and secrets are not serialized.

## `rescan`

Input fields are optional bounded hints:

| Field | Accepted values |
|---|---|
| `reason` | 1–64 ASCII alphanumeric/`-_.:` characters |
| `subsystem` | `usb`, `tty`, `net`, or `manual` |
| `action` | `add`, `remove`, `change`, `move`, `bind`, `unbind`, `online`, or `offline` |

No extra field or filesystem path is accepted. Hints only schedule a full
reconciled scan; they never choose a device or path. A successful reply is:

```json
{
  "schema": 1,
  "shadow_mode": true,
  "accepted": true,
  "scan_id": 5,
  "pending": true,
  "reconcile": {"...": "<previous/current reconciliation state>"}
}
```

The returned `scan_id` identifies the queued request; the scan is asynchronous
and may be debounced with other hints. Hotplug scripts call this method and exit
without waiting.

## Application errors

Invalid input is returned as a typed reply, so clients must inspect `error` even
when the ubus transport call itself succeeded:

```json
{
  "schema": 1,
  "shadow_mode": true,
  "error": {
    "code": "invalid_argument",
    "message": "a valid device_id is required"
  }
}
```

Implemented P0/P1 codes are `invalid_argument`, `not_found`, and
`internal_error`. Ambiguous topology is represented in the cached device fields,
not as a method error. Future long-running mutating operations will use job IDs;
none exist in this build.
