# SPDX-FileCopyrightText: 2026 As Tsaqib
# SPDX-License-Identifier: Apache-2.0

include $(TOPDIR)/rules.mk

LUCI_TITLE:=LuCI companion for Fibocom modems managed by ModemManager
LUCI_DESCRIPTION:=Read-only Fibocom modem overview and status UI backed by ModemManager
LUCI_DEPENDS:= \
	@MODEMMANAGER_WITH_MBIM \
	@MODEMMANAGER_WITH_NETIFD \
	+luci-base \
	+fibocom-mm-bridge \
	+modemmanager \
	+luci-proto-modemmanager \
	+kmod-usb-acm \
	+kmod-usb-net-cdc-mbim \
	+kmod-usb-wdm
LUCI_PKGARCH:=all

PKG_MAINTAINER:=As Tsaqib <0xfiqks@gmail.com>
PKG_LICENSE:=Apache-2.0

include $(TOPDIR)/feeds/luci/luci.mk

# call BuildPackage - OpenWrt buildroot signature
