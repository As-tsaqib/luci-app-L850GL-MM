# SPDX-FileCopyrightText: 2026 As Tsaqib
# SPDX-License-Identifier: Apache-2.0

include $(TOPDIR)/rules.mk

LUCI_TITLE:=LuCI protocol support for Fibocom modems
LUCI_DESCRIPTION:=Network interface form for the fibocom netifd protocol
LUCI_DEPENDS:=+fibocom-netifd
LUCI_PKGARCH:=all

PKG_MAINTAINER:=As Tsaqib <0xfiqks@gmail.com>
PKG_LICENSE:=Apache-2.0

include $(TOPDIR)/feeds/luci/luci.mk

# call BuildPackage - OpenWrt buildroot signature
