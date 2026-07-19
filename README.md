<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# luci-app-fibocom

OpenWrt management stack for Fibocom modems, initially scoped to the Fibocom L850-GL.

The current tree implements the P0/P1 **shadow-mode** foundation. It inventories
matching USB topology from sysfs and exposes sanitized cached state through typed
ubus methods. This build requires the explicit `--shadow` flag and cannot open
modem ports, dial, reset hardware, or alter networking.

The netifd protocol and LuCI protocol/settings surfaces are deliberately
fail-closed/read-only scaffolds for later phases. They are not connectivity
support.

## Product direction

- MBIM is the primary/default L850 composition and the transport required for eSIM.
- The P2 production MBIM backend will use libmbim directly through mbim-proxy.
- Intel XMM NCM RAW-IP is a later backend and remains unavailable until the active data interface is proven on hardware.
- From P2 onward, fibocomd will own modem bearer lifecycle, radio, and AT serialization.
- In the future bearer path, netifd will exclusively own addresses, routes, DNS, MTU, metrics, and firewall integration.
- LuCI is a typed ubus client and never executes modem tools.
- Future eSIM support will be an optional package built on official lpac/luci-app-lpac.

See [PRD.md](PRD.md) for the complete contract and [memory.md](memory.md) for the persistent audit record.

## Package status

| Package | Current behavior |
|---|---|
| `fibocomd` | P0/P1 strict profile loader, async sysfs discovery, generation tracking, and read-only `fibocom` ubus object |
| `fibocom-netifd` | placeholder `proto fibocom`; always fails with `SHADOW_MODE` and never starts a bearer |
| `luci-proto-fibocom` | Network → Interfaces form for the future protocol; does not make the placeholder dial |
| `luci-app-fibocom` | read-only Overview, Status, Settings guidance, and Diagnostics views backed by ubus |
| `luci-app-fibocom-esim` | not implemented; remains an optional future lpac/luci-app-lpac integration |

The L850 profile remains inside fibocomd until a second model justifies profile subpackages.

## Hardware scope

| Composition | USB ID | Initial role mapping |
|---|---|---|
| MBIM | `2cb7:0007` | interface 00 WDM/net pair; 02 AT primary, 04 ignored debug, 06 AT secondary |
| NCM | `8087:095a` | sysfs interface 00 AT primary, 02 ignored, 04 AT secondary, 06/08/0a NCM candidates |

The loader accepts only this reviewed L850 profile. P0 identifies the model from
the exact USB ID only; it does not open AT to verify `CGMM`, firmware, or the
active NCM data candidate. Runtime names such as `ttyACM2`, `cdc-wdm0`, and
`wwan0` are never stable identifiers.

`device_id` is `l850-` plus a SHA-256 digest derived from a usable USB serial.
If no usable serial is exposed, it falls back to a digest of the physical USB
slot and reports `identity_scope: path-scoped`. Neither identity mode exposes
the raw serial. Duplicate serial-derived identities are marked ambiguous.

## Safety status

The following are intentionally unavailable in the initial implementation:

- MBIM or NCM dialing;
- mode switching and reset;
- band, RAT, cell, or SIM switching;
- raw AT commands;
- route or UCI network mutation;
- eSIM profile mutation and online RSP.

Do not use a shadow build as a connectivity replacement.

## P0 runtime and dependencies

procd starts the only supported production invocation:

```sh
/usr/sbin/fibocomd --foreground --shadow
```

`--sysfs-root`, `--profile`, and `--ubus-socket` accept absolute paths and exist
for controlled fixture/integration testing. `--version` prints the daemon
version. Omitting `--shadow` is a hard startup error; there is no UCI option that
can enable mutation.

The current `fibocomd` package depends on GLib/GIO, json-c, libubus/libubox,
ubus/jshn, and the USB ACM/WDM/MBIM/NCM kernel packages selected by its OpenWrt
Makefile. It intentionally does **not** depend on `libmbim` or `mbim-utils` yet;
direct libmbim enters with the P2 bearer backend. It also does not depend on
ModemManager, Quectel-CM, or `jq`.

## Feed status

The package Makefiles exist, but this tree has not yet passed an OpenWrt
SDK/buildroot build and the intended GitHub feed URL has not been published.
Use a local `src-link` entry for development; do not advertise a `src-git` feed
until the remote repository exists and target builds pass.

The first hardware build should run alongside an existing manager only in
shadow mode. Before enabling any future bearer backend, stop QModem, XModem, or
ModemManager ownership of the same physical L850.

## Development

```sh
make check
```

The check suite validates JSON/JavaScript/shell syntax, the profile contract,
LuCI safety rules, and a compiled host discovery test over a generated fake
sysfs tree. The fixture covers MBIM/NCM grouping, lowercase `06/08/0a`, serial
identity, prefix-collision isolation, generation changes, unplug/replug, and
profile rejection cases. A separate case verifies that unexpected data drivers
fail closed as a partial `driver-mismatch` topology. Additional regressions
prove that split MBIM WDM/net parents are ambiguous and that an otherwise valid
pair on an interface other than reviewed L850 interface `00` remains partial.

It does not validate target linking, procd/hotplug behavior, live ubus
disconnect/reconnect, kernel-driver topology, or any real modem operation. A
real OpenWrt SDK/buildroot build and sanitized L850 evidence are required before
any runtime hardware capability is claimed. Accordingly, diagnostics always
reports `hardware_validated: false` in this phase.

## Licensing

- Core daemon, transport, netifd, and shell components: GPL-2.0-or-later.
- LuCI JavaScript and project documentation: Apache-2.0.
- No QModem/XModem/Quectel-CM source is copied into this repository.

License texts are stored in `LICENSES/`; every source file must carry SPDX metadata.
