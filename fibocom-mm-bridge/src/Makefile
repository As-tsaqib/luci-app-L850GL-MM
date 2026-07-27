# SPDX-FileCopyrightText: 2026 As Tsaqib
# SPDX-License-Identifier: GPL-2.0-or-later

CC ?= cc
PKG_CONFIG ?= pkg-config

PKG_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags glib-2.0 gio-2.0 mm-glib)
PKG_LIBS ?= $(shell $(PKG_CONFIG) --libs glib-2.0 gio-2.0 mm-glib)

override CPPFLAGS += $(PKG_CFLAGS)
ifneq ($(FIBOCOM_MM_L850_EXPERT),)
override CPPFLAGS += -DFIBOCOM_MM_L850_EXPERT=1
endif
override CFLAGS += -std=gnu11 -Wall -Wextra -Wformat=2 -Wshadow \
	-Wstrict-prototypes -Wmissing-prototypes
override LDLIBS += $(PKG_LIBS) -lubus -luci -lubox

TARGET := fibocom-mm-bridge
SOURCES := main.c identity.c hardware.c network_binding.c radio_policy.c \
	sms_policy.c sms_dedupe_policy.c ubus_request.c l850_cell.c bridge.c \
	ubus_glib.c
OBJECTS := $(SOURCES:.c=.o)
DEPS := $(OBJECTS:.o=.d)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJECTS) $(DEPS)

-include $(DEPS)
