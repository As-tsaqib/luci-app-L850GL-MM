<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Contributing

## Start with evidence

A supported-model contribution must include:

1. exact manufacturer, model, firmware, USB/PCI IDs, and composition;
2. sanitized sysfs topology and port realpaths;
3. command/response fixtures with sensitive identifiers removed;
4. connect, disconnect, replug, recovery, and data-plane results appropriate to the claimed capability;
5. provenance for every protocol fact;
6. tests proving the change does not select another modem's ports.

Do not infer support from a shared vendor ID or a similar product name.

## Profile-only contributions

A profile may declare match facts, port roles, backend identifiers, and capability states. It may not contain:

- shell fragments or executable paths;
- arbitrary AT commands;
- reset ladders;
- user-controlled device paths;
- undocumented NVM mutations.

If an existing backend is sufficient, add a profile and fixtures. A new transport or protocol requires an internal typed backend implementation and dedicated tests.

## Clean-room policy

QModem and the two historical XModem repositories use a non-standard license combination. Quectel-CM material also includes proprietary notices. Do not copy their source, comments, or shell functions.

Implement behavior independently from public specifications, libmbim APIs, sanitized hardware observations, and independently authored tests. Record those inputs in `docs/provenance.md`.

## Security rules

- No raw AT or arbitrary exec RPC.
- No global WDM, TTY, netdev, lock, or PID fallback.
- No secret in argv, environment, logs, fixtures, temporary files, or support bundles.
- Long operations must be bounded, cancellable, and generation-scoped.
- Ambiguous topology fails closed.
- Destructive features require exact capability proof and rollback tests.

## Development flow

1. Keep the daemon in shadow mode while developing discovery.
2. Add or update fixtures before changing matching logic.
3. Run `make check`.
4. Build in a current OpenWrt SDK/buildroot.
5. Attach sanitized hardware evidence to the pull request.
6. Update `memory.md` when a hardware claim or architectural decision changes.

Keep commits focused and include SPDX metadata on every new file.
