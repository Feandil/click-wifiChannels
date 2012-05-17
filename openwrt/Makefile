#
# Copyright (C) 2006-2010 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=udp-test
PKG_VERSION:=20120515
PKG_RELEASE:=1
PKG_REV:=HEAD

PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.gz
PKG_SOURCE_URL:=git://github.com/Feandil/click-wifiChannels.git
PKG_SOURCE_PROTO:=git
PKG_SOURCE_VERSION:=$(PKG_REV)
PKG_SOURCE_SUBDIR:=$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

define Package/udp-test
	SECTION:=net
	CATEGORY:=Network
	DEPENDS:=+zlib +librt +libevent2
	TITLE:=UDP tools for link probing
	URL:=https://github.com/Feandil/click-wifiChannels
endef

define Package/udp-test/Description
	Tools that generate paquets, receive and gzip it on the fly.
endef

TARGET_CFLAGS += "-static -O2 -MD"
TARGET_CXXFLAGS += "-static -O2 -MD"

define Build/Configure
endef

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR)/udp-test CC='$(TARGET_CC)' CFLAGS='$(TARGET_CFLAGS)' LDFLAGS='$(TARGET_LDFLAGS)'
endef

define Package/udp-test/install
	$(INSTALL_DIR) $(1)/usr
	$(INSTALL_DIR) $(1)/usr/bin
	$(CP) $(PKG_BUILD_DIR)/udp-test/server $(1)/usr/bin/udp-test-server
	$(CP) $(PKG_BUILD_DIR)/udp-test/client $(1)/usr/bin/udp-test-client
endef

$(eval $(call BuildPackage,udp-test))