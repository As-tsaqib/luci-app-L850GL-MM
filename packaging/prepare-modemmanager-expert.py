# SPDX-FileCopyrightText: 2026 As Tsaqib
# SPDX-License-Identifier: Apache-2.0

"""Add the reviewed L850-GL expert binary package to OpenWrt's pinned recipe."""

from pathlib import Path
import sys


EXPERT_DEFINITION = r'''
define Package/modemmanager-l850gl-expert
  $(Package/modemmanager)
  TITLE:=ModemManager expert transport for the L850-GL companion
  PROVIDES:=modemmanager
  CONFLICTS:=modemmanager
endef

define Package/modemmanager-l850gl-expert/config
  $(Package/modemmanager/config)
endef

define Package/modemmanager-l850gl-expert/description
 The upstream OpenWrt ModemManager 1.24.0-r10 package rebuilt with the
 reviewed AT-over-D-Bus transport required by the firmware-gated L850-GL
 expert bridge. It provides and conflicts with the stock modemmanager package.
endef
'''

EXPERT_INSTALL = r'''
define Package/modemmanager-l850gl-expert/install
  $(Package/modemmanager/install)
endef
'''


def insert_once(source: str, marker: str, insertion: str) -> str:
    if source.count(marker) != 1:
        raise ValueError(f"expected one recipe marker, found {source.count(marker)}: {marker!r}")
    return source.replace(marker, insertion + "\n" + marker, 1)


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {Path(sys.argv[0]).name} OPENWRT_MODEMMANAGER_MAKEFILE", file=sys.stderr)
        return 2

    makefile = Path(sys.argv[1])
    source = makefile.read_text(encoding="utf-8").replace("\r\n", "\n")
    for required in (
        "PKG_VERSION:=1.24.0",
        "PKG_RELEASE:=10",
        "-Dat_command_via_dbus=$(if $(CONFIG_MODEMMANAGER_WITH_AT_COMMAND_VIA_DBUS),true,false)",
        "define Package/modemmanager/install",
    ):
        if required not in source:
            raise ValueError(f"pinned ModemManager recipe is missing {required!r}")
    if "Package/modemmanager-l850gl-expert" in source:
        raise ValueError("expert package definition already exists")

    source = insert_once(source, "MESON_ARGS += \\\n", EXPERT_DEFINITION)
    source = insert_once(
        source,
        "define Package/modemmanager-rpcd/install\n",
        EXPERT_INSTALL,
    )
    source = source.replace(
        "$(eval $(call BuildPackage,modemmanager))\n",
        "$(eval $(call BuildPackage,modemmanager))\n"
        "$(eval $(call BuildPackage,modemmanager-l850gl-expert))\n",
        1,
    )
    if source.count("$(eval $(call BuildPackage,modemmanager-l850gl-expert))") != 1:
        raise ValueError("failed to register exactly one expert package")

    makefile.write_text(source, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
