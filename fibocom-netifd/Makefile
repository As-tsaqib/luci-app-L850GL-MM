# SPDX-FileCopyrightText: 2026 As Tsaqib
# SPDX-License-Identifier: GPL-2.0-or-later

include $(TOPDIR)/rules.mk

PKG_NAME:=fibocom-netifd
PKG_VERSION:=0.1.0
PKG_RELEASE:=1

PKG_MAINTAINER:=As Tsaqib <0xfiqks@gmail.com>
PKG_LICENSE:=GPL-2.0-or-later

include $(INCLUDE_DIR)/package.mk

define Package/fibocom-netifd
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=WWAN
  TITLE:=netifd protocol support for fibocomd
  URL:=https://github.com/As-tsaqib/luci-app-fibocom
  DEPENDS:=+fibocomd +jshn +ubus
  PKGARCH:=all
endef

define Package/fibocom-netifd/description
 Thin netifd protocol adapter for fibocomd. The initial shadow-mode adapter
 fails closed and never starts a modem bearer.
endef

define Build/Compile
endef

define Package/fibocom-netifd/install
	$(INSTALL_DIR) $(1)/lib/netifd/proto
	$(INSTALL_BIN) ./files/lib/netifd/proto/fibocom.sh \
		$(1)/lib/netifd/proto/fibocom.sh
endef

$(eval $(call BuildPackage,fibocom-netifd))
