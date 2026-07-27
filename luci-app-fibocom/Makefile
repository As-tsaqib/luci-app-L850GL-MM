# SPDX-FileCopyrightText: 2026 As Tsaqib
# SPDX-License-Identifier: Apache-2.0

include $(TOPDIR)/rules.mk

PKG_VERSION:=0.3.0
PKG_RELEASE:=1

LUCI_TITLE:=LuCI companion for Fibocom modems managed by ModemManager
LUCI_DESCRIPTION:=Minimal Fibocom Overview, Lock, and SMS UI backed by ModemManager
LUCI_URL:=https://github.com/As-tsaqib/luci-app-fibocom
LUCI_MAINTAINER:=As Tsaqib <0xfiqks@gmail.com>
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

PKG_LICENSE:=Apache-2.0

include $(TOPDIR)/feeds/luci/luci.mk

# call BuildPackage - OpenWrt buildroot signature
