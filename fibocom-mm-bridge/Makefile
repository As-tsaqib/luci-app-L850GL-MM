# SPDX-FileCopyrightText: 2026 As Tsaqib
# SPDX-License-Identifier: GPL-2.0-or-later

include $(TOPDIR)/rules.mk

PKG_NAME:=fibocom-mm-bridge
PKG_VERSION:=0.1.0
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
  TITLE:=Read-only Fibocom ModemManager bridge
  URL:=https://github.com/As-tsaqib/luci-app-fibocom
  DEPENDS:= \
	+modemmanager \
	+glib2 \
	+libubox \
	+libubus
endef

define Package/fibocom-mm-bridge/description
 A read-only, typed ubus facade over ModemManager for Fibocom modems. The
 initial supported target is the L850-GL in MBIM composition. This package
 never dials, creates or deletes bearers, sends SMS, executes AT commands, or
 changes UCI.
endef

define Build/Prepare
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) ./src/. $(PKG_BUILD_DIR)/
endef

define Build/Compile
	+$(MAKE) $(PKG_JOBS) -C $(PKG_BUILD_DIR) \
		$(TARGET_CONFIGURE_OPTS) \
		PKG_CONFIG="$(PKG_CONFIG)" \
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
