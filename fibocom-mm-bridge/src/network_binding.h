/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef FIBOCOM_NETWORK_BINDING_H
#define FIBOCOM_NETWORK_BINDING_H

#include <stdbool.h>
#include <stddef.h>

#define FIBOCOM_NETWORK_SECTION_MAX 64U
#define FIBOCOM_NETWORK_MODE_MAX 15U

enum FibocomNetworkBindingResult {
	FIBOCOM_NETWORK_BINDING_ERROR = -1,
	FIBOCOM_NETWORK_BINDING_NONE = 0,
	FIBOCOM_NETWORK_BINDING_UNIQUE = 1,
	FIBOCOM_NETWORK_BINDING_AMBIGUOUS = 2,
};

enum FibocomNetworkModeUpdateResult {
	FIBOCOM_NETWORK_MODE_UPDATE_ERROR = -1,
	FIBOCOM_NETWORK_MODE_UPDATE_NONE = 0,
	FIBOCOM_NETWORK_MODE_UPDATE_OK = 1,
	FIBOCOM_NETWORK_MODE_UPDATE_AMBIGUOUS = 2,
	FIBOCOM_NETWORK_MODE_UPDATE_INVALID = 3,
	FIBOCOM_NETWORK_MODE_UPDATE_VERIFY_FAILED = 4,
};

/*
 * This intentionally contains no modem device path or connection secrets.
 * The mode strings are copied only when they pass the narrow ModemManager
 * mode grammar; a missing or malformed option leaves the corresponding
 * has_* member false.
 */
struct FibocomNetworkBinding {
	char section[FIBOCOM_NETWORK_SECTION_MAX + 1U];
	bool has_allowedmode;
	char allowedmode[FIBOCOM_NETWORK_MODE_MAX + 1U];
	bool has_preferredmode;
	char preferredmode[FIBOCOM_NETWORK_MODE_MAX + 1U];
	bool disable_modem;
	bool disable_modem_configured;
};

/*
 * Find the one named `config interface` whose proto is exactly
 * "modemmanager" and whose device exactly equals the internal Device value
 * returned by mm_modem_get_device(). The default lookup reads the active UCI
 * network package using libuci's production defaults.
 *
 * AMBIGUOUS also covers a matching anonymous or non-link-safe section. ERROR
 * reports invalid input or a UCI load failure. This deliberately makes every
 * result other than UNIQUE unsuitable as an ownership grant.
 */
enum FibocomNetworkBindingResult
fibocom_network_binding_lookup(const char *device,
			       struct FibocomNetworkBinding *binding);

/*
 * Test/fixture variant. confdir must be an absolute directory path. It loads
 * <confdir>/network without production UCI overlays; NULL selects the normal
 * production lookup behavior.
 */
enum FibocomNetworkBindingResult
fibocom_network_binding_lookup_at(const char *confdir, const char *device,
				  struct FibocomNetworkBinding *binding);

/*
 * Persist the schema-4 radio-mode intent on the uniquely bound netifd
 * interface. Only the exact allowedmode/preferredmode options are changed.
 * The section is resolved again in the same libuci context, committed once,
 * and read back before OK is returned. No section or device identifier needs
 * to cross the browser boundary.
 */
bool fibocom_network_modes_are_valid(const char *allowedmode,
				     const char *preferredmode);
enum FibocomNetworkModeUpdateResult fibocom_network_modes_update(
	const char *device, const char *allowedmode, const char *preferredmode,
	struct FibocomNetworkBinding *binding);
enum FibocomNetworkModeUpdateResult fibocom_network_modes_update_at(
	const char *confdir, const char *device, const char *allowedmode,
	const char *preferredmode, struct FibocomNetworkBinding *binding);

#ifdef FIBOCOM_NETWORK_BINDING_TESTING
struct FibocomNetworkBindingTestSection {
	const char *name;
	const char *type;
	bool anonymous;
	const char *proto;
	const char *device;
	const char *allowedmode;
	const char *preferredmode;
	const char *disable_modem;
};

enum FibocomNetworkBindingResult
fibocom_network_binding_lookup_test_sections(
	const struct FibocomNetworkBindingTestSection *sections,
	size_t section_count, const char *device,
	struct FibocomNetworkBinding *binding);
#endif

#endif /* FIBOCOM_NETWORK_BINDING_H */
