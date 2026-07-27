<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Contributing

## Preserve ownership

ModemManager is the only owner of modem objects, ports, SIM, SMS, radio, and
bearers. netifd is the only owner of APN, persistent connection intent, routes,
and DNS. Do not add a dialer, bearer lifecycle API, hotplug/sysfs scanner,
custom netifd protocol, direct TTY/WDM access, SMS tool, or external process.

The browser must never submit raw AT, a D-Bus/sysfs/device path, or an arbitrary
ubus method. Prefer typed asynchronous libmm-glib and fail closed when the
standard capability is absent.

## Keep the 0.4 surface small

The menu is exactly Overview, Lock, and SMS. The base `fibocom.mm` object is
exactly eight schema-3 methods. Do not restore Status, Settings, old Advanced,
radio toggle, generic reset, SIM-slot switching, eSIM, rescan, diagnostic dump,
or connection controls without a new product decision.

The expert object remains behind `FIBOCOM_MM_L850_EXPERT` and a separate ACL.
The base binary must not contain its object name. Enabling the build gate is
not permission to populate a firmware allowlist.

## Identity and asynchronous safety

- Keep CSPRNG opaque modem/SMS/client-token IDs.
- Require modem generation for every mutation and messaging generation for SMS.
- Cancel on removal/transport loss and revalidate proxy/liveness/generation in
  every callback.
- Keep one shared per-modem lock across SMS, persistent mode, band, and PCI writes.
- Bound timeouts/cooldowns and distinguish pre-dispatch failure from
  post-dispatch `outcome_unknown`.
- Never retarget a write to a replacement modem after reprobe.

## Parsing, privacy, and ACL

- Structurally validate every untrusted blob attribute before accessing its
  name, type, or data. Reject malformed, unknown, duplicate, missing, and
  mistyped fields.
- Accept only one canonical rpcd session transport field.
- Bound every text/list/response and keep frontend structural validation.
- Never log or include in general status: phone numbers, SMS body, subscriber
  identifiers, credentials, addresses, binary SMS, raw PDU, or raw modem output.
- Grant only the five exact ACL groups and exact ubus methods. Never add
  wildcard, filesystem, cgi-io, shell/file execution, or UCI-write access.
- Keep LuCI fail-closed for every schema other than 3.

## SMS contract

Use native Messaging.List/Create/Delete and Sms.Send. Preserve signals plus
30-second reconciliation, 10-second UI polling, newest-first 1,024 cache, and
100-entry backend pages with Load more. Dedupe documentation/tests must state
the real policy: at most 64 retained tokens, each expiring 300 seconds after
its most recent stored state, with possible earlier capacity eviction and no
restart persistence.

## Band and PCI changes

Mode selection remains netifd-owned. `set_modes` may resolve the bound section
only from the internal ModemManager Device value and may set only
`allowedmode`/`preferredmode`, commit once, verify readback, and request exact
`network.reload`. Never accept or return a section/device path, touch connection
secrets, or grant browser UCI/network wildcard access.

Band Lock must continue using asynchronous SetCurrentBands with automatic
`["any"]`, exact supported-band/current-family validation, confirmation, WAN
warning, hardware attestation, shared lock, timeout/cooldown, and stale outcome
handling. Do not embed XACT commands.

PCI work requires exact model/firmware/composition evidence, bounded parser
fixtures, ModemManager arbitration, exact clear/reset/recovery behavior, and
serving-cell postcondition. Never guess a band encoding, wildcard, NVM path,
unlock tuple, or reset sequence. No live scan, lock, clear, reset, or SMS
mutation may be run without explicit user permission.

## Development flow

1. Update package/static tests to express the contract first.
2. Add malformed, boundary, stale-generation, cancellation, timeout, eviction,
   and expiry coverage appropriate to the change.
3. Regenerate translations with `node tests/generate-pot.js`.
4. Run `make check` where a POSIX compiler/toolchain is available, plus
   `git diff --check`.
5. Run separate base and expert OpenWrt SDK builds. Verify the base object is
   absent from the binary and record artifact checksums.
6. Label offline, SDK, historical live, and current live evidence separately.

Every source/data file needs SPDX/REUSE coverage. Keep the clean-room boundary:
reference projects may support facts and test design, but their source is not
copied.
