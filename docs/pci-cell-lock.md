<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# L850 LTE neighbor scan and PCI/EARFCN lock

## Verdict

The command family used by XModem is plausible and independently documented,
but the current XModem implementation must not be copied unchanged. The product
implementation needs a typed parser, exact model/firmware gates, ModemManager
AT serialization, and post-lock verification.

This feature is not available on the stock tested OpenWrt image:

- ModemManager `GetCellInfo` returns unsupported on the L850;
- generic `Modem.Command` is disabled by the OpenWrt build.

It remains a P3 expert milestone. The v0.2.0 base bridge neither compiles nor
exports the vendor object, and the standard Advanced tab does not claim
PCI/EARFCN scan or lock support.

## Evidence

### Fibocom AT manual

The L8/L860 AT manual documents `AT+XMCI=<meas>` as a snapshot of serving and
neighboring cells. Pages 167–169 define the LTE schema:

```text
+XMCI:
  <TYPE>,<MCC>,<MNC>,<TAC>,<CI>,<PCI>,<DLUARFCN>,<ULUARFCN>,
  <PATHLOSS_LTE>,<RSRP>,<RSRQ>,<RSSNR>,<TA>,<CQI>
```

Relevant types:

| Type | Meaning |
|---|---|
| 4 | LTE serving cell |
| 5 | LTE neighboring cell |
| 6 | 1xRTT serving cell; not LTE |

`AT+XMCI=1` requests fresh serving measurements plus available neighboring
measurements. Numeric fields may be decimal or hexadecimal depending on
firmware/output.

Reference:
<https://www.manualslib.com/manual/1655076/Fibocom-L860-Gl.html?page=167>.

### Internal cell-lock command

Fibocom L850/L860 community documentation and the modem's own function
introspection describe:

```text
AT@SIC:FREQ_LOCK(
    sim_id,
    rat,
    band,
    inter_frequency_lock_enable,
    frequency,
    psc_pci
)
```

Community LTE examples use:

- `sim_id=0`;
- `rat=3`;
- `enable=1` to apply and `enable=0` to clear;
- decimal EARFCN and PCI;
- either an LTE band number, an XACT-style `100 + band`, or wildcard
  `band=255` depending on the firmware/procedure;
- `psc_pci=65535` as a wildcard for frequency-only locking.

References:

- <https://wiki.vps-server.ru/doku.php/wiki:openwrt:fibocom-850>
- <https://forum.artnet.biz/threads/fibocom-l860-gl-16-lte-obsuzhdeniye-proshivka.444/>
- <https://www.drive2.ru/b/706927153861633966/>

These are behavioral evidence, not an official support guarantee for firmware
`18500.5001.00.05.27.30`.

## XModem audit

Backend reference:

```text
application/xmodem/files/usr/share/xmodem/vendor/fibocom.sh
  get_neighborcell_intel()
  lockcell_intel()
  set_neighborcell()
```

Frontend reference:

```text
luci/luci-app-xmodem-next/htdocs/luci-static/resources/view/
  xmodem/config_advanced.js
```

Useful UX behavior:

- an explicit Scan button;
- a table of detected cells;
- Copy/select action to populate lock fields;
- separate Lock and Unlock actions;
- visible lock status.

Implementation defects/gaps:

1. **Wrong cell type**

   The backend accepts types `4|5|6` as LTE. The manual defines type 6 as
   1xRTT serving with a different field layout. Only types 4 and 5 belong in the
   LTE list.

2. **PCI zero is lost**

   PCI 0 is valid in LTE's 0–503 range. Shell and UI truthiness checks currently
   convert it into “not supplied”.

3. **Invalid measurements are converted**

   Raw RSRP 255 means invalid in this family, but the current conversion can
   produce a bogus positive value.

4. **Lock state is inferred incorrectly**

   The NVM query reads an enable/support field but status is inferred only from
   frequency. A stale frequency can therefore be presented as an active lock.

5. **SIM-instance ambiguity**

   XModem queries:

   ```text
   at@nvm:dyn_cps.nas_asm.freq_lock_params.*??
   ```

   Community traces also show
   `dyn_cps.instance[0|1].nas_asm.freq_lock_params`. The correct path for the
   active SIM/firmware has not been proven.

6. **AT injection/range gap**

   Browser values become shell/AT strings without a strict integer grammar and
   complete model-specific range validation.

7. **Unsafe unlock fallback**

   If current lock parameters cannot be read, defaults are substituted and an
   unlock is still attempted. Product behavior must fail closed instead.

8. **Reset result is discarded**

   `CFUN=1,1` is run with output suppressed. The result of the lock command is
   returned as success even if reset/reconnect fails.

9. **No postcondition**

   `OK` is treated as success without checking that the modem re-registered
   and camped on the requested EARFCN/PCI.

10. **Frontend state defects**

    The view has missing validation and error paths, including a callback branch
    referring to an undefined `self`. Its UI ideas may be reimplemented, but
    it is not a safe API contract.

## ModemManager arbitration

### Forbidden approach

Do not open `ttyACM0` or `ttyACM2` while ModemManager exports the modem.
`flock` does not help because ModemManager does not participate in an
application-defined lock. Parallel reads can consume each other's responses and
parallel writes can corrupt modem state.

### Standard path

Use standard ModemManager methods first:

- `GetCellInfo` for cell discovery;
- `SetCurrentBands` for band locking;
- `Reset` for a modem reset.

On the tested L850, `GetCellInfo` is unsupported, so a fallback is needed only
for this capability.

### Typed vendor fallback

`Modem.Command` is acceptable only as a transport from a privileged typed
backend because ModemManager performs AT port selection and queueing.

OpenWrt default:

```text
CONFIG_MODEMMANAGER_WITH_AT_COMMAND_VIA_DBUS=n
```

An expert image may enable it without running ModemManager in debug mode. The
image must also review/restrict the system D-Bus policy because OpenWrt builds
ModemManager with `polkit=no`.

The LuCI/ubus API never accepts a command string. It accepts only validated
fields and constructs one fixed grammar internally.

### InhibitDevice

`InhibitDevice` causes ModemManager to disable the modem, release the bearer,
close all ports, and unexport the object. It is too disruptive for a cell scan
and is not a runtime fallback. It may be considered only for a separately
confirmed maintenance operation.

## Conditional expert API

Object: `fibocom.mm.l850`. It is absent from the base build and protected by a
separate expert ACL. The implementation is compiled into the same daemon only
for an explicit expert variant so it shares the base cache and per-modem
operation lock. It never exposes generic AT.

```text
cell_scan(modem_id, generation)
cell_lock_status(modem_id, generation)
set_cell_lock(modem_id, generation, earfcn, optional_pci, confirmation)
clear_cell_lock(modem_id, generation, confirmation)
```

There is no `command`, `device`, `port`, `band_code`, or D-Bus path input
from the browser.

The backend:

1. resolves and verifies exact `{modem_id, generation}` for the current object;
2. verifies VID:PID `2cb7:0007`, plugin `fibocom`, model L850-GL, and a tested
   firmware allowlist;
3. verifies the object generation again immediately before dispatch;
4. serializes one advanced mutation per modem;
5. builds the command from integer fields;
6. parses a bounded response;
7. correlates a replacement object for read-only post-reprobe observation;
8. verifies the postcondition without issuing another write to that object.

Object removal invalidates the original token. Any clear/rollback write after
reprobe is a new, explicitly confirmed request using the new
`{modem_id, generation}`; the old operation never retargets a replacement.

## Input and parser contract

### Cell scan

- command: exact `AT+XMCI=1`;
- accept LTE type 4 and 5 only;
- require the complete LTE field count;
- parse hexadecimal only when it has an explicit `0x` prefix;
- PCI range: 0–503, including 0;
- reject invalid/unknown PCI and EARFCN sentinels;
- raw RSRP 0–97 maps to `raw - 141` dBm; 255 is unavailable;
- raw RSRQ maps according to the manual/fixture and rejects 255;
- mark `serving=true` only for type 4;
- do not expose MCC/MNC/TAC/cell ID unless the UI contract needs and redacts
  them.

Scan is button-driven and rate-limited; it is never part of periodic polling.

### Cell lock

The release command cannot be frozen until hardware testing determines:

- actual band versus `100 + band` versus 255 for this firmware;
- whether `65535` reliably means PCI wildcard;
- correct NVM instance/path and active-lock flag;
- exact unlock tuple;
- which one reset/apply procedure works.

EARFCN must map to a band reported in ModemManager `SupportedBands`. Do not use
a generic `0..65535` check: L850 reports Band 66, whose EARFCNs can exceed
65535.

### Result state

Typed states:

```text
applied_verified
cleared_verified
lock_applied_reset_required
resetting
reprobe_timeout
registration_timeout
verification_mismatch
unsupported_build
unsupported_firmware
stale_modem
invalid_argument
```

Raw AT or NVM text is never returned to LuCI or written to normal logs.

## Reset matrix still required

Observed references differ:

| Source | Apply/reconnect action |
|---|---|
| XModem | `CFUN=1,1` |
| Fibocom generic ModemManager | `CFUN=15` |
| XMM ModemManager | `CFUN=16` |
| community/Keenetic | `CFUN=4`, `CFUN=15`, or XACT toggle |

Testing must compare one action at a time. Do not implement a fallback chain
that sends several CFUN variants.

## Hardware test plan

Testing cell lock is disruptive and requires an explicit maintenance window.

1. Record sanitized current firmware, current band/EARFCN/PCI, bearer, and route
   health.
2. Confirm a rollback WAN path or physical access.
3. Enable the expert MM build and verify `Modem.Command` through MM without
   opening TTY.
4. Run only introspection/read commands first:
   `AT+XMCI=?`, `AT+XMCI=1`, `AT@SIC:FREQ_LOCK()??`, and candidate
   lock-state queries.
5. Save sanitized raw fixtures.
6. Test frequency-only lock against a currently visible cell.
7. Test exact PCI lock.
8. Test clear/unlock.
9. For each reset candidate, verify object disappearance/reprobe, registration,
   bearer, serving tuple, IP, route, DNS, and traffic.
10. Reboot and replug to determine persistence.
11. Test PCI 0 with a synthetic parser fixture even if no live PCI 0 is visible.
12. Test malformed, sentinel, timeout, and unplug paths without sending a modem
    mutation.

No lock/reset command should be run remotely without telling the user it may
interrupt WAN.
