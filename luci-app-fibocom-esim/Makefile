# SPDX-FileCopyrightText: 2026 As Tsaqib
# SPDX-License-Identifier: Apache-2.0

include $(TOPDIR)/rules.mk

LUCI_TITLE:=Optional eSIM tab for luci-app-fibocom
LUCI_DESCRIPTION:=Link Fibocom modem management to the existing luci-app-lpac UI
LUCI_DEPENDS:=@LPAC_WITH_MBIM @MODEMMANAGER_WITH_MBIM +luci-app-fibocom +luci-app-lpac
LUCI_PKGARCH:=all

PKG_MAINTAINER:=As Tsaqib <0xfiqks@gmail.com>
PKG_LICENSE:=Apache-2.0

include $(TOPDIR)/feeds/luci/luci.mk

# call BuildPackage - OpenWrt buildroot signature
