# SPDX-FileCopyrightText: 2026 As Tsaqib
# SPDX-License-Identifier: GPL-2.0-or-later

include $(TOPDIR)/rules.mk

PKG_NAME:=fibocomd
PKG_VERSION:=0.1.0
PKG_RELEASE:=1

PKG_MAINTAINER:=As Tsaqib <0xfiqks@gmail.com>
PKG_LICENSE:=GPL-2.0-or-later
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
PKG_BUILD_FLAGS:=gc-sections
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

define Package/fibocomd
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=WWAN
  TITLE:=Fibocom modem manager daemon
  URL:=https://github.com/As-tsaqib/luci-app-fibocom
  DEPENDS:= \
	+glib2 \
	+libjson-c \
	+libubox \
	+libubus \
	+jshn \
	+ubus \
	+kmod-usb-acm \
	+kmod-usb-net-cdc-mbim \
	+kmod-usb-net-cdc-ncm \
	+kmod-usb-wdm
endef

define Package/fibocomd/description
 fibocomd owns Fibocom modem discovery and lifecycle. The initial package is
 deliberately restricted to read-only shadow inventory and cannot dial or
 mutate a modem.
endef

define Package/fibocomd/conffiles
/etc/config/fibocom
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

define Package/fibocomd/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/fibocomd $(1)/usr/sbin/fibocomd

	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/etc/config/fibocom $(1)/etc/config/fibocom

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/fibocomd $(1)/etc/init.d/fibocomd

	$(INSTALL_DIR) $(1)/etc/hotplug.d/usb
	$(INSTALL_BIN) ./files/etc/hotplug.d/usb/25-fibocom $(1)/etc/hotplug.d/usb/25-fibocom

	$(INSTALL_DIR) $(1)/etc/hotplug.d/tty
	$(INSTALL_BIN) ./files/etc/hotplug.d/tty/25-fibocom $(1)/etc/hotplug.d/tty/25-fibocom

	$(INSTALL_DIR) $(1)/etc/hotplug.d/net
	$(INSTALL_BIN) ./files/etc/hotplug.d/net/25-fibocom $(1)/etc/hotplug.d/net/25-fibocom

	$(INSTALL_DIR) $(1)/usr/share/fibocom/profiles
	$(INSTALL_DATA) ./files/usr/share/fibocom/profiles/l850-gl.json \
		$(1)/usr/share/fibocom/profiles/l850-gl.json
endef

$(eval $(call BuildPackage,fibocomd))
