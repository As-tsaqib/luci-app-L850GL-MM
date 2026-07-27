# SPDX-FileCopyrightText: 2026 As Tsaqib
# SPDX-License-Identifier: GPL-2.0-or-later

include $(TOPDIR)/rules.mk

PKG_NAME:=fibocom-mm-bridge
PKG_VERSION:=0.4.0
PKG_RELEASE:=1

PKG_MAINTAINER:=As Tsaqib <0xfiqks@gmail.com>
PKG_LICENSE:=GPL-2.0-or-later
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
PKG_BUILD_DEPENDS:=modemmanager
PKG_BUILD_FLAGS:=gc-sections
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

define Package/fibocom-mm-bridge
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=WWAN
  TITLE:=Fibocom ModemManager companion bridge
  URL:=https://github.com/As-tsaqib/luci-app-fibocom
  DEPENDS:= \
	+modemmanager \
	+glib2 \
	+libubox \
	+libubus \
	+libuci
endef

define Package/fibocom-mm-bridge/config
	config FIBOCOM_MM_BRIDGE_L850_EXPERT
		bool "Build the reviewed L850 PCI/EARFCN expert object"
		depends on PACKAGE_fibocom-mm-bridge
		depends on MODEMMANAGER_WITH_AT_COMMAND_VIA_DBUS
		default n
		help
		  Publish fibocom.mm.l850 with fixed, typed commands for the exact
		  live-validated L850-GL firmware allowlist. This requires a matching
		  ModemManager build that enables reviewed AT command transport.
endef

define Package/fibocom-mm-bridge/description
 A typed ubus facade over ModemManager for Fibocom modems. The initial
 supported target is the L850-GL in MBIM composition. It observes modem state
 and implements SMS and capability-gated standard radio controls through the
 native ModemManager API. It never dials or creates or deletes bearers. Its
 schema-3 mode-policy endpoint changes only allowedmode and preferredmode on
 the uniquely bound netifd interface. The base build never executes AT commands; the explicit expert
 build can dispatch only its compiled-in, firmware-gated L850 grammar through
 ModemManager's asynchronous command queue.
endef

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) ./src/. $(PKG_BUILD_DIR)/
endef

define Build/Compile
	+$(MAKE) $(PKG_JOBS) -C $(PKG_BUILD_DIR) \
		$(TARGET_CONFIGURE_OPTS) \
		PKG_CONFIG="$(PKG_CONFIG)" \
		FIBOCOM_MM_L850_EXPERT="$(if $(CONFIG_FIBOCOM_MM_BRIDGE_L850_EXPERT),1,)" \
		CPPFLAGS="$(TARGET_CPPFLAGS)" \
		CFLAGS="$(TARGET_CFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)"
endef

define Package/fibocom-mm-bridge/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/fibocom-mm-bridge \
		$(1)/usr/sbin/fibocom-mm-bridge

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/fibocom-mm-bridge \
		$(1)/etc/init.d/fibocom-mm-bridge
endef

$(eval $(call BuildPackage,fibocom-mm-bridge))
