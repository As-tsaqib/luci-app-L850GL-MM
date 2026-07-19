<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Hardware evidence checklist

No sensitive value should be committed. Replace IMEI, IMSI, ICCID, EID, phone
numbers, APN credentials, activation codes, public IPs where appropriate, and
tokens with stable redaction markers.

## Current validation status

As of 2026-07-19, this implementation has not been built in an OpenWrt
SDK/buildroot, exercised against a live ubus/procd/hotplug stack, or run against
an attached L850-GL in this workspace. No MBIM/NCM dial, eSIM, mode switch, or
advanced-control claim follows from the current code.

`make check` does compile the discovery/profile core on the host and exercises
it against a generated fake sysfs tree. That fixture verifies exact MBIM/NCM
grouping, interface `06/08/0a`, USB-serial-hash identity, cross-device prefix
isolation, stable/change/replug generations, and selected profile rejection
cases. It also verifies that synthetic `qmi_wwan`/`rndis_host` substitutions are
rejected as `driver-mismatch`. It cannot validate kernel enumeration or modem
behavior.

Until the evidence below is captured and reviewed, the ubus diagnostics
contract intentionally returns:

```json
{
  "profile": {
    "schema_validated": true,
    "hardware_validated": false,
    "match_confidence": "usb_id_only"
  }
}
```

Do not change `hardware_validated` based only on a successful synthetic test or
USB-ID match.

## Both compositions

- router model, OpenWrt version, kernel version;
- modem exact model and firmware;
- `lsusb -t` and sanitized descriptors;
- physical USB parent and every child interface;
- all TTY/net/WDM realpaths;
- sanitized USB `serial` presence/stability and whether identity falls back to
  `path-scoped`;
- driver and USB interface number;
- ten cold boots and twenty replug mappings.

## MBIM

- exact WDM/netdev same-parent proof;
- subscriber, registration, packet-service, connect, IP-config, and disconnect trace;
- requested versus activated IP family;
- stale-session and partial-failure cleanup;
- re-IP and two-way traffic;
- lpac EID/list behavior through mbim-proxy.

## NCM

- all three CDC-NCM candidates;
- proof that the selected interface maps to `/USBHS/NCM/0`;
- CID and full sanitized command/response transcript;
- `CGCONTRDP` variants;
- address, prefix, gateway, DNS, ARP state, and two-way traffic;
- teardown and re-IP behavior.

## Mode/reset

- `GTUSBMODE?` before and after;
- whether command alone re-enumerates;
- exact reset command, if required;
- remove/add timing and final topology;
- twenty MBIM→NCM→MBIM round trips before claiming support.
