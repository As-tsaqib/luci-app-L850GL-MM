/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "network_binding.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <uci.h>

#define FIBOCOM_NETWORK_DEVICE_MAX 4096U
#define FIBOCOM_NETWORK_CONFDIR_MAX 4096U

struct BindingScan {
	unsigned int matches;
	bool unsafe_match;
	struct FibocomNetworkBinding candidate;
};

static void
binding_clear(struct FibocomNetworkBinding *binding)
{
	if (binding == NULL)
		return;
	memset(binding, 0, sizeof(*binding));
	/* This is also the effective default in proto_modemmanager_teardown(). */
	binding->disable_modem = true;
}

static bool
bounded_length(const char *value, size_t maximum, size_t *length)
{
	size_t i;

	if (value == NULL || length == NULL)
		return false;
	for (i = 0; i <= maximum; i++) {
		if (value[i] == '\0') {
			*length = i;
			return true;
		}
	}
	return false;
}

static bool
bounded_equal(const char *left, const char *right, size_t maximum)
{
	size_t left_length;
	size_t right_length;

	return bounded_length(left, maximum, &left_length) &&
	       bounded_length(right, maximum, &right_length) &&
	       left_length == right_length &&
	       memcmp(left, right, left_length) == 0;
}

static bool
section_name_is_safe(const char *name, size_t *length)
{
	size_t i;
	size_t name_length;

	if (!bounded_length(name, FIBOCOM_NETWORK_SECTION_MAX, &name_length) ||
	    name_length == 0U)
		return false;
	for (i = 0; i < name_length; i++) {
		const unsigned char value = (unsigned char)name[i];

		if (!((value >= (unsigned char)'a' &&
		       value <= (unsigned char)'z') ||
		      (value >= (unsigned char)'A' &&
		       value <= (unsigned char)'Z') ||
		      (value >= (unsigned char)'0' &&
		       value <= (unsigned char)'9') ||
		      value == (unsigned char)'_'))
			return false;
	}
	*length = name_length;
	return true;
}

static uint8_t
mode_token_bit(const char *value)
{
	if (value[1] != 'g')
		return 0U;
	switch (value[0]) {
	case '2':
		return UINT8_C(1) << 0U;
	case '3':
		return UINT8_C(1) << 1U;
	case '4':
		return UINT8_C(1) << 2U;
	case '5':
		return UINT8_C(1) << 3U;
	default:
		return 0U;
	}
}

static bool
allowedmode_is_safe(const char *value, size_t *length)
{
	size_t offset = 0U;
	size_t value_length;
	uint8_t modes = 0U;

	if (!bounded_length(value, FIBOCOM_NETWORK_MODE_MAX, &value_length))
		return false;
	if (value_length == 0U ||
	    (value_length == 3U && memcmp(value, "any", 3U) == 0)) {
		*length = value_length;
		return true;
	}

	while (offset < value_length) {
		uint8_t mode;

		if (value_length - offset < 2U)
			return false;
		mode = mode_token_bit(value + offset);
		if (mode == 0U || (modes & mode) != 0U)
			return false;
		modes |= mode;
		offset += 2U;
		if (offset == value_length)
			break;
		if (value[offset] != '|')
			return false;
		offset++;
	}

	*length = value_length;
	return true;
}

static bool
preferredmode_is_safe(const char *value, size_t *length)
{
	size_t value_length;

	if (!bounded_length(value, FIBOCOM_NETWORK_MODE_MAX, &value_length))
		return false;
	if (value_length == 0U ||
	    (value_length == 4U && memcmp(value, "none", 4U) == 0) ||
	    (value_length == 2U && mode_token_bit(value) != 0U)) {
		*length = value_length;
		return true;
	}
	return false;
}

static void
copy_modes(struct FibocomNetworkBinding *binding, const char *allowedmode,
	   const char *preferredmode)
{
	size_t length;

	if (allowedmode != NULL && allowedmode_is_safe(allowedmode, &length)) {
		memcpy(binding->allowedmode, allowedmode, length);
		binding->allowedmode[length] = '\0';
		binding->has_allowedmode = true;
	}
	if (preferredmode != NULL &&
	    preferredmode_is_safe(preferredmode, &length)) {
		memcpy(binding->preferredmode, preferredmode, length);
		binding->preferredmode[length] = '\0';
		binding->has_preferredmode = true;
	}
}

static void
scan_consider(struct BindingScan *scan, const char *name, const char *type,
	      bool anonymous, const char *proto, const char *section_device,
	      const char *allowedmode, const char *preferredmode,
	      const char *disable_modem, const char *target_device)
{
	struct FibocomNetworkBinding candidate;
	size_t section_length;

	if (!bounded_equal(type, "interface", sizeof("interface") - 1U) ||
	    !bounded_equal(proto, "modemmanager",
			   sizeof("modemmanager") - 1U) ||
	    !bounded_equal(section_device, target_device,
			   FIBOCOM_NETWORK_DEVICE_MAX))
		return;

	if (scan->matches < 2U)
		scan->matches++;
	if (anonymous || !section_name_is_safe(name, &section_length)) {
		scan->unsafe_match = true;
		return;
	}
	if (scan->matches != 1U)
		return;

	binding_clear(&candidate);
	memcpy(candidate.section, name, section_length);
	candidate.section[section_length] = '\0';
	copy_modes(&candidate, allowedmode, preferredmode);
	candidate.disable_modem_configured = disable_modem != NULL;
	/* The upstream teardown skips disable only for the exact value "0". */
	candidate.disable_modem = disable_modem == NULL ||
				    strcmp(disable_modem, "0") != 0;
	scan->candidate = candidate;
}

static enum FibocomNetworkBindingResult
scan_finish(const struct BindingScan *scan,
	    struct FibocomNetworkBinding *binding)
{
	if (scan->matches == 0U)
		return FIBOCOM_NETWORK_BINDING_NONE;
	if (scan->matches != 1U || scan->unsafe_match)
		return FIBOCOM_NETWORK_BINDING_AMBIGUOUS;
	*binding = scan->candidate;
	return FIBOCOM_NETWORK_BINDING_UNIQUE;
}

static bool
lookup_arguments_are_valid(const char *device,
			   struct FibocomNetworkBinding *binding)
{
	size_t device_length;

	if (binding == NULL)
		return false;
	binding_clear(binding);
	return bounded_length(device, FIBOCOM_NETWORK_DEVICE_MAX,
			      &device_length) && device_length != 0U;
}

enum FibocomNetworkBindingResult
fibocom_network_binding_lookup_at(const char *confdir, const char *device,
				  struct FibocomNetworkBinding *binding)
{
	struct BindingScan scan = { 0 };
	struct uci_context *context;
	struct uci_package *package = NULL;
	struct uci_element *element;
	char fixture_path[FIBOCOM_NETWORK_CONFDIR_MAX + sizeof("/network")];
	const char *package_name = "network";
	size_t confdir_length;
	enum FibocomNetworkBindingResult result = FIBOCOM_NETWORK_BINDING_ERROR;

	if (!lookup_arguments_are_valid(device, binding))
		return FIBOCOM_NETWORK_BINDING_ERROR;
	if (confdir != NULL) {
		if (!bounded_length(confdir, FIBOCOM_NETWORK_CONFDIR_MAX,
				    &confdir_length) ||
		    confdir_length == 0U || confdir[0] != '/' ||
		    confdir[confdir_length - 1U] == '/')
			return FIBOCOM_NETWORK_BINDING_ERROR;
		if (snprintf(fixture_path, sizeof(fixture_path), "%s/network",
			     confdir) < 0)
			return FIBOCOM_NETWORK_BINDING_ERROR;
		package_name = fixture_path;
	}

	context = uci_alloc_context();
	if (context == NULL)
		return FIBOCOM_NETWORK_BINDING_ERROR;
	if (confdir != NULL &&
	    (uci_set_confdir(context, confdir) != UCI_OK ||
	     uci_set_conf2dir(context, NULL) != UCI_OK))
		goto out;
	if (uci_load(context, package_name, &package) != UCI_OK ||
	    package == NULL)
		goto out;

	uci_foreach_element(&package->sections, element) {
		struct uci_section *section = uci_to_section(element);

		scan_consider(
			&scan, section->e.name, section->type, section->anonymous,
			uci_lookup_option_string(context, section, "proto"),
			uci_lookup_option_string(context, section, "device"),
			uci_lookup_option_string(context, section, "allowedmode"),
			uci_lookup_option_string(context, section, "preferredmode"),
			uci_lookup_option_string(context, section, "disable_modem"),
			device);
	}
	result = scan_finish(&scan, binding);

out:
	if (result != FIBOCOM_NETWORK_BINDING_UNIQUE)
		binding_clear(binding);
	uci_free_context(context);
	return result;
}

enum FibocomNetworkBindingResult
fibocom_network_binding_lookup(const char *device,
			       struct FibocomNetworkBinding *binding)
{
	return fibocom_network_binding_lookup_at(NULL, device, binding);
}

#ifdef FIBOCOM_NETWORK_BINDING_TESTING
enum FibocomNetworkBindingResult
fibocom_network_binding_lookup_test_sections(
	const struct FibocomNetworkBindingTestSection *sections,
	size_t section_count, const char *device,
	struct FibocomNetworkBinding *binding)
{
	struct BindingScan scan = { 0 };
	size_t i;
	enum FibocomNetworkBindingResult result;

	if (!lookup_arguments_are_valid(device, binding) ||
	    (sections == NULL && section_count != 0U))
		return FIBOCOM_NETWORK_BINDING_ERROR;
	for (i = 0; i < section_count; i++) {
		scan_consider(&scan, sections[i].name, sections[i].type,
			      sections[i].anonymous, sections[i].proto,
			      sections[i].device, sections[i].allowedmode,
			      sections[i].preferredmode,
			      sections[i].disable_modem, device);
	}
	result = scan_finish(&scan, binding);
	if (result != FIBOCOM_NETWORK_BINDING_UNIQUE)
		binding_clear(binding);
	return result;
}
#endif
