<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# luci-app-fibocom

LuCI companion for Fibocom modems owned by ModemManager. The base UI contains
**Overview**, **Status**, **SMS**, **Advanced**, and **Settings** under **Modem
-> Fibocom Modem**. Optional eSIM management is supplied by the separate addon.

The browser talks only to the typed `fibocom.mm` ubus facade supplied by
`fibocom-mm-bridge`:

- `list_modems`;
- `get_overview`;
- `get_status`;
- `get_capabilities`;
- `list_sms`, `send_sms`, and `delete_sms`;
- `set_bands`, `set_radio`, `reset`, and `set_primary_sim_slot`.

It does not call D-Bus, modem command-line tools, files, host commands, or UCI
directly. Separate rpcd ACL groups grant the minimum read, SMS, and standard
radio methods. Every mutation is capability-gated and bound to an opaque modem
ID plus its current generation. ModemManager remains responsible for discovery
and modem objects; netifd's ModemManager protocol remains responsible for
automatic connection, addresses, routes, and DNS.

Connection and mode configuration is deliberately linked to **Network ->
Interfaces** instead of being duplicated. Advanced exposes only standard
ModemManager band, immediate radio, reset, and physical SIM-slot operations
advertised as mutable by the bridge. It has no generic command console, direct
mode mutation, or data-session lifecycle controls.

The bridge reads UCI only to correlate an exact `proto modemmanager` section;
it never changes UCI or returns connection secrets/device paths. Immediate
radio control is unavailable for a modem with an exact netifd binding. Dynamic
OpenWrt interface state and traffic counters are not implemented in schema 1,
so the UI reports them as unavailable instead of deriving them from UCI.

SMS inventory synchronizes automatically from ModemManager Added, Deleted,
and property-change signals, with a 30-second backend reconciliation fallback.
While open, the SMS view polls the bridge cache every 10 seconds; an update
deferred to protect a focused compose editor is rendered when focus leaves.
The bounded backend cache retains newest messages first. No `sms-tool`, direct
TTY polling, or manual rescan is involved. Send deduplication is an in-memory
five-minute retry aid, not an exactly-once guarantee across restarts.

The package requires ModemManager built with MBIM and netifd support. Official
OpenWrt 25.12.5 builds the Fibocom plugin into ModemManager. A downstream build
that modularizes plugins must add its matching Fibocom plugin package at image
level; the package name is not portable across both layouts.

The dependency is OpenWrt's native, unmodified ModemManager; this repository
does not build or ship a patched ModemManager fork.

Run the dependency-free frontend checks with:

```sh
node tests/static.js
```
