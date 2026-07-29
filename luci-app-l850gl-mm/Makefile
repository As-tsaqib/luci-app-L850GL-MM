# SPDX-FileCopyrightText: 2026 As Tsaqib
# SPDX-License-Identifier: Apache-2.0

include $(TOPDIR)/rules.mk

PKG_VERSION:=0.6.0
PKG_RELEASE:=1

LUCI_TITLE:=LuCI companion for the L850-GL modem managed by ModemManager
LUCI_DESCRIPTION:=Minimal L850GL MM Overview, Lock, and SMS UI backed by ModemManager
LUCI_URL:=https://github.com/As-tsaqib/luci-app-L850GL-MM
LUCI_MAINTAINER:=As Tsaqib <0xfiqks@gmail.com>
# The SDK/image configuration selects ModemManager MBIM and netifd features.
# Do not add @MODEMMANAGER_WITH_* here: selecting +modemmanager while depending
# on an option that itself depends on PACKAGE_modemmanager is a Kconfig cycle.
LUCI_DEPENDS:= \
	+luci-base \
	+l850gl-mm-bridge \
	+modemmanager \
	+luci-proto-modemmanager \
	+kmod-usb-acm \
	+kmod-usb-net-cdc-mbim \
	+kmod-usb-wdm
LUCI_PKGARCH:=all

PKG_LICENSE:=Apache-2.0

include $(TOPDIR)/feeds/luci/luci.mk

# call BuildPackage - OpenWrt buildroot signature
