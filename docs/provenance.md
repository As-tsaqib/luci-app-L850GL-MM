<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Provenance

The project is a clean-room implementation.

## Behavioral references

- Linux sysfs and USB interface metadata.
- Public MBIM/libmbim APIs and specifications.
- ModemManager behavior for physical grouping, Basic Connect lifecycle, cleanup, and indications.
- OpenWrt netifd, procd, hotplug, ubus, and LuCI public interfaces.
- Sanitized observations from real L850 hardware.

These references define target behavior; they do not imply the referenced
component is linked or copied. In particular, P0/P1 does not link libmbim, run
ModemManager, or contain a bearer implementation.

## Audit snapshot ledger

The initial audit used these immutable snapshots or review URLs. Commit IDs are
evidence anchors, not source-import points.

| Reference | Snapshot/review | Used for |
|---|---|---|
| [FUjr/QModem](https://github.com/FUjr/QModem) | `3bd54c1334a66461587ca89b22ee2f71129fcad3` | upstream feasibility, product/scanner behavior |
| [As-tsaqib/L850GL-XModem](https://github.com/As-tsaqib/L850GL-XModem) | `eca5d92d31555777c8984ac3fe5b294eec0c9b39` | sanitized L850 USB/mode/libmbim evidence |
| [As-tsaqib/XModem](https://github.com/As-tsaqib/XModem) | `12cd90055525ab9f97644190a659645a47c5e244` | historical hardware behavior and migration risks |
| [ModemManager](https://gitlab.freedesktop.org/mobile-broadband/ModemManager) | `3568fb91a856d5e8de15dc7b2c2b80eecb46eb8e` | L850 interface roles and lifecycle semantics |
| [OpenWrt packages](https://github.com/openwrt/packages) | `9f76dfc43c63392621b44951a0a17f8d75245751` | package/procd/netifd conventions and libmbim packaging |
| [OpenWrt LuCI](https://github.com/openwrt/luci) | `112388301e8b920a7532065c498700131990dd13` | JavaScript view, protocol, menu, ACL, and i18n conventions |
| [4IceG/luci-app-modemband](https://github.com/4IceG/luci-app-modemband) | `9d2477269726` | UX/command-research reference only |
| [mrhaav/openwrt-packages](https://github.com/mrhaav/openwrt-packages) | `392fe64ed2ba` | independent L850 NCM RAW-IP evidence |
| [lpac L850 compatibility PR](https://github.com/estkme-group/lpac/pull/438) | merged review `#438` | future MBIM UICC/eSIM feasibility |

The full reference list and audit conclusions are retained in `memory.md`.

## Exact L850 facts

- MBIM USB ID `2cb7:0007`.
- NCM USB ID `8087:095a`.
- Port roles recorded in `docs/profile-schema.md`.
- `GTUSBMODE` mapping and Intel XMM NCM sequence are treated as protocol facts and must be reconfirmed on the target firmware.

The current daemon matches only USB IDs and sysfs shape. It does not query model
or firmware and therefore reports `model_confidence: usb_id_only` and
`hardware_validated: false`. The generated fake-sysfs host test validates parser
and grouping behavior, not the hardware facts themselves.

## Excluded source

No source is copied from:

- FUjr/QModem;
- As-tsaqib/L850GL-XModem;
- As-tsaqib/XModem;
- Quectel-CM variants;
- third-party modem shell scripts.

Reference repositories may inform tests and protocol facts, but implementation structure, source text, comments, and parsers are authored independently.

Add a dated entry here whenever a new command, quirk, profile, or hardware claim is introduced.

## Implementation record

- 2026-07-19: P0/P1 clean-room core added with strict L850 profile loading,
  USB-serial-hash/path-scoped identities, async sysfs reconciliation, typed
  read-only ubus, fail-closed netifd scaffolding, and read-only LuCI views. No
  real-device or OpenWrt runtime validation is recorded yet.
