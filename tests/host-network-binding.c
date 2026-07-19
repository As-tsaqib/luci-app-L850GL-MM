/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "network_binding.h"

#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static const char fixture[] =
	"config interface 'lan'\n"
	"\toption proto 'static'\n"
	"\toption device '/sys/devices/l850-a'\n"
	"\toption apn 'must-not-leak'\n"
	"\n"
	"config route 'not_an_interface'\n"
	"\toption proto 'modemmanager'\n"
	"\toption device '/sys/devices/l850-a'\n"
	"\n"
	"config interface 'wrong_proto'\n"
	"\toption proto 'ModemManager'\n"
	"\toption device '/sys/devices/l850-a'\n"
	"\n"
	"config interface 'mobile_a'\n"
	"\toption proto 'modemmanager'\n"
	"\toption device '/sys/devices/l850-a'\n"
	"\toption allowedmode '4g|3g'\n"
	"\toption preferredmode '4g'\n"
	"\toption disable_modem '0'\n"
	"\toption apn 'secret-apn'\n"
	"\toption pincode '1234'\n"
	"\toption username 'secret-user'\n"
	"\toption password 'secret-password'\n"
	"\n"
	"config interface 'mobile_b1'\n"
	"\toption proto 'modemmanager'\n"
	"\toption device '/sys/devices/l850-b'\n"
	"\n"
	"config interface 'mobile_b2'\n"
	"\toption proto 'modemmanager'\n"
	"\toption device '/sys/devices/l850-b'\n"
	"\n"
	"config interface\n"
	"\toption proto 'modemmanager'\n"
	"\toption device '/sys/devices/l850-c'\n"
	"\n"
	"config interface 'mobile_d'\n"
	"\toption proto 'modemmanager'\n"
	"\toption device '/sys/devices/l850-d'\n"
	"\toption allowedmode '4g||3g'\n"
	"\toption preferredmode 'lte'\n";

static void
write_all(int fd, const char *value, size_t length)
{
	size_t offset = 0U;

	while (offset < length) {
		ssize_t written = write(fd, value + offset, length - offset);

		assert(written > 0);
		offset += (size_t)written;
	}
}

static bool
memory_contains(const void *memory, size_t memory_length, const char *needle)
{
	const unsigned char *bytes = memory;
	size_t needle_length = strlen(needle);
	size_t i;

	if (needle_length > memory_length)
		return false;
	for (i = 0; i <= memory_length - needle_length; i++) {
		if (memcmp(bytes + i, needle, needle_length) == 0)
			return true;
	}
	return false;
}

static void
assert_cleared(const struct FibocomNetworkBinding *binding)
{
	assert(binding->section[0] == '\0');
	assert(!binding->has_allowedmode);
	assert(!binding->has_preferredmode);
	assert(binding->disable_modem);
	assert(!binding->disable_modem_configured);
}

static void
test_real_libuci_fixture(const char *confdir)
{
	struct FibocomNetworkBinding binding;
	enum FibocomNetworkBindingResult result;
	char overlong_device[4098];

	memset(&binding, 0xa5, sizeof(binding));
	result = fibocom_network_binding_lookup_at(
		confdir, "/sys/devices/l850-a", &binding);
	assert(result == FIBOCOM_NETWORK_BINDING_UNIQUE);
	assert(strcmp(binding.section, "mobile_a") == 0);
	assert(binding.has_allowedmode);
	assert(strcmp(binding.allowedmode, "4g|3g") == 0);
	assert(binding.has_preferredmode);
	assert(strcmp(binding.preferredmode, "4g") == 0);
	assert(binding.disable_modem_configured);
	assert(!binding.disable_modem);
	assert(!memory_contains(&binding, sizeof(binding), "secret-apn"));
	assert(!memory_contains(&binding, sizeof(binding), "1234"));
	assert(!memory_contains(&binding, sizeof(binding), "secret-user"));
	assert(!memory_contains(&binding, sizeof(binding), "secret-password"));
	assert(!memory_contains(&binding, sizeof(binding),
			       "/sys/devices/l850-a"));

	memset(&binding, 0xa5, sizeof(binding));
	result = fibocom_network_binding_lookup_at(
		confdir, "/sys/devices/l850-b", &binding);
	assert(result == FIBOCOM_NETWORK_BINDING_AMBIGUOUS);
	assert_cleared(&binding);

	result = fibocom_network_binding_lookup_at(
		confdir, "/sys/devices/l850-c", &binding);
	assert(result == FIBOCOM_NETWORK_BINDING_AMBIGUOUS);
	assert_cleared(&binding);

	result = fibocom_network_binding_lookup_at(
		confdir, "/sys/devices/l850-d", &binding);
	assert(result == FIBOCOM_NETWORK_BINDING_UNIQUE);
	assert(strcmp(binding.section, "mobile_d") == 0);
	assert(!binding.has_allowedmode);
	assert(!binding.has_preferredmode);
	assert(binding.disable_modem);
	assert(!binding.disable_modem_configured);

	result = fibocom_network_binding_lookup_at(
		confdir, "/sys/devices/l850-a/near-match", &binding);
	assert(result == FIBOCOM_NETWORK_BINDING_NONE);
	assert_cleared(&binding);

	memset(overlong_device, 'x', sizeof(overlong_device));
	overlong_device[sizeof(overlong_device) - 1U] = '\0';
	result = fibocom_network_binding_lookup_at(confdir, overlong_device,
					   &binding);
	assert(result == FIBOCOM_NETWORK_BINDING_ERROR);
	assert_cleared(&binding);

	assert(fibocom_network_binding_lookup_at(
		       "relative/config", "/sys/devices/l850-a", &binding) ==
	       FIBOCOM_NETWORK_BINDING_ERROR);
	assert_cleared(&binding);
	assert(fibocom_network_binding_lookup_at(confdir, NULL, &binding) ==
	       FIBOCOM_NETWORK_BINDING_ERROR);
	assert_cleared(&binding);
	assert(fibocom_network_binding_lookup_at(
		       confdir, "/sys/devices/l850-a", NULL) ==
	       FIBOCOM_NETWORK_BINDING_ERROR);
}

static void
test_scan_safety_rules(void)
{
	static const struct FibocomNetworkBindingTestSection unsafe[] = {
		{
			.name = "mobile-bad",
			.type = "interface",
			.proto = "modemmanager",
			.device = "/sys/devices/l850",
		},
	};
	static const struct FibocomNetworkBindingTestSection safe_empty_modes[] = {
		{
			.name = "mobile_2",
			.type = "interface",
			.proto = "modemmanager",
			.device = "/sys/devices/l850",
			.allowedmode = "",
			.preferredmode = "",
			.disable_modem = "false",
		},
	};
	static const struct FibocomNetworkBindingTestSection unordered_modes[] = {
		{
			.name = "mobile_3",
			.type = "interface",
			.proto = "modemmanager",
			.device = "/sys/devices/l850",
			.allowedmode = "2g|5g|4g",
			.preferredmode = "none",
			.disable_modem = "1",
		},
	};
	static const struct FibocomNetworkBindingTestSection duplicate_mode[] = {
		{
			.name = "mobile_4",
			.type = "interface",
			.proto = "modemmanager",
			.device = "/sys/devices/l850",
			.allowedmode = "4g|4g",
		},
	};
	struct FibocomNetworkBinding binding;
	enum FibocomNetworkBindingResult result;

	result = fibocom_network_binding_lookup_test_sections(
		unsafe, sizeof(unsafe) / sizeof(unsafe[0]),
		"/sys/devices/l850", &binding);
	assert(result == FIBOCOM_NETWORK_BINDING_AMBIGUOUS);
	assert_cleared(&binding);

	result = fibocom_network_binding_lookup_test_sections(
		safe_empty_modes,
		sizeof(safe_empty_modes) / sizeof(safe_empty_modes[0]),
		"/sys/devices/l850", &binding);
	assert(result == FIBOCOM_NETWORK_BINDING_UNIQUE);
	assert(binding.has_allowedmode && binding.allowedmode[0] == '\0');
	assert(binding.has_preferredmode && binding.preferredmode[0] == '\0');
	assert(binding.disable_modem_configured);
	assert(binding.disable_modem);

	result = fibocom_network_binding_lookup_test_sections(
		unordered_modes,
		sizeof(unordered_modes) / sizeof(unordered_modes[0]),
		"/sys/devices/l850", &binding);
	assert(result == FIBOCOM_NETWORK_BINDING_UNIQUE);
	assert(strcmp(binding.allowedmode, "2g|5g|4g") == 0);
	assert(strcmp(binding.preferredmode, "none") == 0);
	assert(binding.disable_modem);

	result = fibocom_network_binding_lookup_test_sections(
		duplicate_mode,
		sizeof(duplicate_mode) / sizeof(duplicate_mode[0]),
		"/sys/devices/l850", &binding);
	assert(result == FIBOCOM_NETWORK_BINDING_UNIQUE);
	assert(!binding.has_allowedmode);
	assert(binding.disable_modem);
}

int
main(void)
{
	char template[PATH_MAX];
	char network_path[PATH_MAX];
	const char *temporary_directory;
	char *confdir;
	int fd;

	temporary_directory = getenv("TMPDIR");
	if (temporary_directory == NULL || temporary_directory[0] == '\0')
		temporary_directory = ".";
	assert(snprintf(template, sizeof(template),
			"%s/fibocom-network-binding-test.XXXXXX",
			temporary_directory) > 0);
	confdir = mkdtemp(template);
	assert(confdir != NULL);
	assert(confdir[0] == '/');
	assert(snprintf(network_path, sizeof(network_path), "%s/network",
			confdir) > 0);
	fd = open(network_path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	assert(fd >= 0);
	write_all(fd, fixture, sizeof(fixture) - 1U);
	assert(close(fd) == 0);

	test_real_libuci_fixture(confdir);
	test_scan_safety_rules();

	assert(unlink(network_path) == 0);
	assert(rmdir(confdir) == 0);
	puts("network binding tests passed");
	return 0;
}
