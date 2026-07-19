<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Hardware evidence

## Privacy rule

Never commit IMEI, IMSI, ICCID, EID, phone numbers, SMS text, APN credentials,
PIN/PUK, activation/confirmation codes, tokens, or public IP configuration.
Capture only allowlisted fields or replace values with stable redaction markers.

## Validated baseline

Read-only live validation completed on 2026-07-19:

| Item | Evidence |
|---|---|
| Router | Linksys EA6350v3 |
| OpenWrt | 25.12.5, kernel 6.12.94 |
| ModemManager | 1.24.0-r10 |
| Modem | Fibocom L850-GL |
| Firmware | `18500.5001.00.05.27.30` |
| Composition | MBIM `2cb7:0007` |
| Plugin | `fibocom` |
| Network | `proto modemmanager` |
| Messaging | storage `mt` available |

Observed port grouping:

```text
cdc-wdm0 (mbim)
ttyACM0  (at)
ttyACM1  (ignored)
ttyACM2  (at)
wwan0    (net)
```

The names are observations only. See `live-router-validation.md` for the
sanitized replug timeline.

Validated behaviors:

- unplug removes the old ModemManager object and brings netifd down;
- replug adds MBIM/net/AT ports and creates a new modem object;
- the OpenWrt monitor marks the configured interface available;
- netifd automatically enables, registers, and connects;
- bearer and logical interface return online in about eleven seconds;
- final state is connected/attached and netifd up/available;
- one connection attempt, no failed attempt in the observed cycle.

Not validated by this baseline:

- SMS send/receive/delete;
- band/mode/reset/SIM-slot mutation;
- eSIM operations in this exact companion architecture;
- PCI/EARFCN scan/lock;
- OpenWrt target build of the new bridge;
- NCM connectivity.

## Capability observations

```text
GetCellInfo
  unsupported on the tested L850 MBIM combination

Modem.Command
  unauthorized on the stock OpenWrt build

SupportedBands
  5 UTRAN entries and 24 E-UTRAN entries reported

CurrentBands at capture
  UTRAN 1/8 and E-UTRAN 1/3/5
```

Do not infer PCI support from the existence of an AT string in XModem.

## Bridge read/status acceptance

For every normalized field, record:

- exact libmm-glib property/method;
- absent/null/placeholder cases;
- privacy/redaction rule;
- behavior while disabled, registered, connected, resetting, and absent;
- firmware and fixture name.

Required runtime cases:

- 10 cold boots;
- 20 unplug/replug cycles;
- ModemManager restart;
- netifd down/up;
- D-Bus loss/recovery if safely reproducible;
- two identical modems;
- unplug during every bridge mutation;
- ModemManager path/index reuse.

## MBIM data-plane acceptance

- plugin Fibocom/XMM selected for `2cb7:0007`;
- WDM/net/AT mapping matches the same physical modem;
- exactly one `proto modemmanager` connection owner;
- registration and packet service reach expected state;
- requested and activated IP families recorded;
- bearer interface matches netifd L3 device;
- IP, route, DNS, MTU, metric, and firewall state correct;
- IPv4/IPv6 claimed only where traffic passes;
- sustained two-way traffic;
- disconnect cleanup;
- replug/reconnect without saved MM index or runtime path.

## SMS acceptance

- Messaging available and unavailable fixtures;
- list/read received and sent SMS;
- GSM-7 and Unicode/UCS-2 content;
- short and multipart send;
- pending/sent/failed/received state transitions;
- delete allowed states;
- modem/SIM storage differences and storage-full behavior;
- replug reconciliation;
- content and numbers absent from logs, argv, general status, and events;
- L850 multipart-index workaround exercised if a multipart SMS is available.

## Standard Advanced acceptance

### Bands

- read supported/current bands;
- automatic → reviewed subset → automatic;
- reject empty/unsupported/duplicate values;
- registration and traffic recovery;
- warn that a wrong subset can remove remote access.

### Modes

Persistent mode changes belong to `/etc/config/network`, because
`proto modemmanager` re-applies `allowedmode`/`preferredmode` on setup.
Test Settings/network apply and reconnect; do not treat a direct transient MM
call as saved configuration.

### Radio and reset

- disable/enable behavior with netifd;
- standard MM Reset only;
- cooldown/no reset loop;
- object removal/reprobe and WAN recovery;
- clear error when remote recovery fails.

### SIM slot

- method offered only if more than one valid slot is advertised;
- profile/SIM state and bearer recovery after switch;
- no NVM fallback.

## Optional eSIM acceptance

- exact `luci-app-lpac` and lpac versions;
- `LPAC_WITH_MBIM=y`;
- MBIM backend, correct WDM, `proxy=1`;
- EID and profiles with identifiers redacted;
- enable/disable/nickname/delete within supported lpac scope;
- SIM refresh and MM/netifd reconnect;
- active-bearer coexistence;
- lock/timeout/recovery;
- base image has no lpac/eSIM menu;
- optional image contains no TgBot/Telegram artifacts.

Initial claim is one L850. Multi-modem support is blocked until lpac settings
bind to stable selected-device identity instead of a global WDM path.

## PCI/EARFCN evidence required

Testing is disruptive and must be announced before execution.

### Read-only fixture phase

Through the ModemManager AT queue on an expert build, capture sanitized:

```text
AT+XMCI=?
AT+XMCI=1
AT@SIC:FREQ_LOCK()??
candidate lock-state query for SIM instance 0
candidate lock-state query for SIM instance 1, only when relevant
```

Fixture requirements:

- type 4 serving and type 5 neighbor;
- explicit hex/decimal variants;
- invalid sentinels;
- PCI 0 synthetic case;
- RSRP/RSRQ boundary values;
- truncated and oversized response.

### Mutation matrix

- frequency-only lock;
- exact EARFCN+PCI lock;
- clear/unlock;
- current and alternative visible cell;
- each candidate reset/apply mechanism tested separately;
- object reprobe and netifd reconnect;
- post-lock serving tuple verification;
- mismatch and rollback;
- persistence across ModemManager restart, USB replug, and router reboot;
- wrong band/PCI/EARFCN rejected before AT;
- unplug during set/reset/verification.

The current firmware is not allowlisted until this matrix passes.

## NCM evidence for future ModemManager contribution

NCM is not a companion-app connectivity feature. Evidence may support a separate
ModemManager backend contribution:

- `8087:095a` descriptors and all CDC-NCM candidates;
- mapping to `/USBHS/NCM/0`;
- consistent PDP CID;
- `XDATACHANNEL`, `CGDATA="M-RAW_IP"`, IP query, and teardown trace;
- netdev, ARP, address, route, DNS, and traffic proof;
- boot/replug/error/cleanup cases;
- target class actually advertising data-net support.

Only a tested ModemManager implementation can change NCM from
`detected/unsupported` to `supported`.

## Evidence storage

Raw fixtures must be:

- sanitized before entering Git;
- named by model, firmware, operation, and expected result;
- accompanied by a source/date note;
- bounded so fuzz/unit tests do not depend on device access.

A synthetic fixture or USB ID alone never establishes hardware support.
