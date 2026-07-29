# SPDX-FileCopyrightText: 2026 As Tsaqib
# SPDX-License-Identifier: GPL-2.0-or-later

CC ?= cc
PKG_CONFIG ?= pkg-config

PKG_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags glib-2.0 gio-2.0 mm-glib)
PKG_LIBS ?= $(shell $(PKG_CONFIG) --libs glib-2.0 gio-2.0 mm-glib)

override CPPFLAGS += $(PKG_CFLAGS)
ifneq ($(L850GL_MM_EXPERT),)
override CPPFLAGS += -DL850GL_MM_EXPERT=1
endif
override CFLAGS += -std=gnu11 -Wall -Wextra -Wformat=2 -Wshadow \
	-Wstrict-prototypes -Wmissing-prototypes
override LDLIBS += $(PKG_LIBS) -lubus -luci -lubox

TARGET := l850gl-mm-bridge
SOURCES := main.c identity.c hardware.c network_binding.c radio_policy.c \
	sms_policy.c sms_dedupe_policy.c ubus_request.c l850_cell.c bridge.c \
	ubus_glib.c
ifneq ($(L850GL_MM_EXPERT),)
SOURCES += l850_ca.c l850_voltage.c l850_mutation_policy.c
endif
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
