# SPDX-FileCopyrightText: 2026 As Tsaqib
# SPDX-License-Identifier: GPL-2.0-or-later

include $(TOPDIR)/rules.mk

PKG_NAME:=l850gl-mm-bridge
PKG_VERSION:=0.6.0
PKG_RELEASE:=1

PKG_MAINTAINER:=As Tsaqib <0xfiqks@gmail.com>
PKG_LICENSE:=GPL-2.0-or-later
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
PKG_BUILD_DEPENDS:=modemmanager
PKG_BUILD_FLAGS:=gc-sections
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

define Package/l850gl-mm-bridge
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=WWAN
  TITLE:=L850-GL ModemManager companion bridge
  URL:=https://github.com/As-tsaqib/luci-app-L850GL-MM
  DEPENDS:= \
	+modemmanager \
	+glib2 \
	+libubox \
	+libubus \
	+libuci
  CONFLICTS:=fibocom-mm-bridge
endef

define Package/l850gl-mm-bridge/config
	config L850GL_MM_BRIDGE_EXPERT
		bool "Build the reviewed L850 PCI/EARFCN expert object"
		depends on PACKAGE_l850gl-mm-bridge
		depends on MODEMMANAGER_WITH_AT_COMMAND_VIA_DBUS
		default n
		help
		  Publish l850gl.mm.l850 with fixed, typed commands for the exact
		  live-validated L850-GL firmware allowlist. This requires a matching
		  ModemManager build that enables reviewed AT command transport.
endef

define Package/l850gl-mm-bridge/description
 A typed ubus facade over ModemManager for the L850-GL in MBIM composition.
 It observes modem state
 and implements SMS and capability-gated standard radio controls through the
 native ModemManager API. It never dials or creates or deletes bearers. Its
 schema-4 mode-policy endpoint changes only allowedmode and preferredmode on
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
		L850GL_MM_EXPERT="$(if $(CONFIG_L850GL_MM_BRIDGE_EXPERT),1,)" \
		CPPFLAGS="$(TARGET_CPPFLAGS)" \
		CFLAGS="$(TARGET_CFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)"
endef

define Package/l850gl-mm-bridge/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/l850gl-mm-bridge \
		$(1)/usr/sbin/l850gl-mm-bridge

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/l850gl-mm-bridge \
		$(1)/etc/init.d/l850gl-mm-bridge
endef

$(eval $(call BuildPackage,l850gl-mm-bridge))
