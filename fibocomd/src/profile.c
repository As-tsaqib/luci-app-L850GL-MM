/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "profile.h"

#include <errno.h>
#include <fcntl.h>
#include <json-c/json.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PROFILE_MAX_BYTES (64U * 1024U)

struct _FibocomProfile {
	gint ref_count;
	gchar *id;
	gchar *display_name;
	GPtrArray *usb_matches; /* FibocomUsbMatch */
	FibocomPortMap mbim_ports;
	FibocomPortMap ncm_ports;
};

static GQuark
profile_error_quark(void)
{
	return g_quark_from_static_string("fibocom-profile-error");
}

static void
set_invalid(GError **error, const gchar *detail)
{
	g_set_error(error, profile_error_quark(), 1,
		    "invalid L850 profile: %s", detail);
}

static gboolean
object_has_only(json_object *object, const gchar *const *allowed)
{
	json_object_object_foreach(object, key, value) {
		gboolean found = FALSE;
		guint i;

		(void)value;
		for (i = 0; allowed[i] != NULL; i++) {
			if (g_str_equal(key, allowed[i])) {
				found = TRUE;
				break;
			}
		}
		if (!found)
			return FALSE;
	}

	return TRUE;
}

static gboolean
get_object(json_object *parent, const gchar *key, json_object **out)
{
	return json_object_object_get_ex(parent, key, out) &&
	       json_object_is_type(*out, json_type_object);
}

static gboolean
get_array(json_object *parent, const gchar *key, json_object **out)
{
	return json_object_object_get_ex(parent, key, out) &&
	       json_object_is_type(*out, json_type_array);
}

static gboolean
get_string(json_object *parent, const gchar *key, const gchar **out,
	   gsize max_length)
{
	json_object *value;
	const gchar *string;
	int json_length;

	if (!json_object_object_get_ex(parent, key, &value) ||
	    !json_object_is_type(value, json_type_string))
		return FALSE;
	string = json_object_get_string(value);
	json_length = json_object_get_string_len(value);
	if (string == NULL || json_length <= 0 || (gsize)json_length > max_length ||
	    strlen(string) != (gsize)json_length)
		return FALSE;
	*out = string;
	return TRUE;
}

static gboolean
is_lower_hex4(const gchar *value)
{
	guint i;

	if (strlen(value) != 4)
		return FALSE;
	for (i = 0; i < 4; i++) {
		if (!g_ascii_isdigit(value[i]) &&
		    !(value[i] >= 'a' && value[i] <= 'f'))
			return FALSE;
	}
	return TRUE;
}

static gboolean
parse_interface_number(const gchar *value, guint *number)
{
	gchar *end = NULL;
	gulong parsed;

	if (value == NULL || strlen(value) != 2 ||
	    !g_ascii_isxdigit(value[0]) || !g_ascii_isxdigit(value[1]) ||
	    g_ascii_isupper(value[0]) || g_ascii_isupper(value[1]))
		return FALSE;
	errno = 0;
	parsed = strtoul(value, &end, 16);
	if (errno != 0 || end == NULL || *end != '\0' || parsed > 255)
		return FALSE;
	*number = (guint)parsed;
	return TRUE;
}

static gboolean
array_contains_uint(const GArray *array, guint needle)
{
	guint i;

	for (i = 0; i < array->len; i++) {
		if (g_array_index(array, guint, i) == needle)
			return TRUE;
	}
	return FALSE;
}

static gboolean
parse_interface_array(json_object *object, const gchar *key, GArray *output,
		      gboolean required)
{
	json_object *array;
	size_t length;
	size_t i;

	if (!json_object_object_get_ex(object, key, &array))
		return !required;
	if (!json_object_is_type(array, json_type_array))
		return FALSE;
	length = json_object_array_length(array);
	if (required && length == 0)
		return FALSE;
	for (i = 0; i < length; i++) {
		json_object *item = json_object_array_get_idx(array, i);
		const gchar *value;
		guint number;

		if (!json_object_is_type(item, json_type_string))
			return FALSE;
		value = json_object_get_string(item);
		if (!parse_interface_number(value, &number) ||
		    array_contains_uint(output, number))
			return FALSE;
		g_array_append_val(output, number);
	}
	return TRUE;
}

static gboolean
parse_port_map(json_object *ports, const gchar *key, FibocomPortMap *map,
	       gboolean ncm)
{
	static const gchar *const allowed[] = {
		"at_primary", "at_secondary", "ignored", "data_candidates",
		"data_selector", NULL
	};
	json_object *object;
	json_object *secondary;
	json_object *selector_object;
	const gchar *value;

	if (!get_object(ports, key, &object) || !object_has_only(object, allowed))
		return FALSE;
	if (!get_string(object, "at_primary", &value, 2) ||
	    !parse_interface_number(value, &map->at_primary))
		return FALSE;
	if (json_object_object_get_ex(object, "at_secondary", &secondary)) {
		if (!get_string(object, "at_secondary", &value, 2) ||
		    !parse_interface_number(value, &map->at_secondary))
			return FALSE;
		map->has_at_secondary = TRUE;
	}
	map->ignored = g_array_new(FALSE, FALSE, sizeof(guint));
	map->data_candidates = g_array_new(FALSE, FALSE, sizeof(guint));
	if (!parse_interface_array(object, "ignored", map->ignored, TRUE) ||
	    !parse_interface_array(object, "data_candidates", map->data_candidates, ncm))
		return FALSE;
	if (json_object_object_get_ex(object, "data_selector", &selector_object)) {
		if (!get_string(object, "data_selector", &value, 32) ||
		    !g_str_equal(value, ncm ? "hardware-required" :
					 "exact-interface-parent"))
			return FALSE;
	} else if (ncm) {
		return FALSE;
	}

	/* P0 accepts only the reviewed L850 role map, while still loading it as data. */
	if (!ncm) {
		return map->at_primary == 0x02 && map->has_at_secondary &&
		       map->at_secondary == 0x06 && map->ignored->len == 1 &&
		       g_array_index(map->ignored, guint, 0) == 0x04 &&
		       map->data_candidates->len == 0;
	}
	return map->at_primary == 0x00 && map->has_at_secondary &&
	       map->at_secondary == 0x04 && map->ignored->len == 1 &&
	       g_array_index(map->ignored, guint, 0) == 0x02 &&
	       map->data_candidates->len == 3 &&
	       array_contains_uint(map->data_candidates, 0x06) &&
	       array_contains_uint(map->data_candidates, 0x08) &&
	       array_contains_uint(map->data_candidates, 0x0a);
}

static gboolean
validate_backends(json_object *root)
{
	static const gchar *const allowed[] = { "mbim", "ncm", NULL };
	json_object *backends;
	const gchar *management;
	const gchar *mbim;
	const gchar *ncm;

	return get_string(root, "management_backend", &management, 64) &&
	       g_str_equal(management, "intel-xmm") &&
	       get_object(root, "bearer_backends", &backends) &&
	       object_has_only(backends, allowed) &&
	       get_string(backends, "mbim", &mbim, 64) &&
	       g_str_equal(mbim, "mbim-basic-connect") &&
	       get_string(backends, "ncm", &ncm, 64) &&
	       g_str_equal(ncm, "intel-xmm-ncm");
}

static gboolean
validate_ncm(json_object *root)
{
	static const gchar *const allowed[] = { "session_cid", NULL };
	json_object *ncm;
	json_object *cid;

	return get_object(root, "ncm", &ncm) && object_has_only(ncm, allowed) &&
	       json_object_object_get_ex(ncm, "session_cid", &cid) &&
	       json_object_is_type(cid, json_type_int) &&
	       json_object_get_int64(cid) == 0;
}

static gboolean
validate_string_array(json_object *parent, const gchar *key,
		      const gchar *required_value, gsize max_length,
		      gsize exact_length)
{
	json_object *array;
	GHashTable *seen;
	size_t length;
	size_t i;
	gboolean found = required_value == NULL;
	gboolean valid = FALSE;

	if (!get_array(parent, key, &array))
		return FALSE;
	length = json_object_array_length(array);
	if (length == 0 || (exact_length != 0 && length != exact_length))
		return FALSE;
	seen = g_hash_table_new(g_str_hash, g_str_equal);
	for (i = 0; i < length; i++) {
		json_object *item = json_object_array_get_idx(array, i);
		const gchar *value;

		if (!json_object_is_type(item, json_type_string))
			goto out;
		value = json_object_get_string(item);
		if (value == NULL || value[0] == '\0' || strlen(value) > max_length ||
		    g_hash_table_contains(seen, value))
			goto out;
		g_hash_table_add(seen, (gpointer)value);
		if (required_value != NULL && g_str_equal(value, required_value))
			found = TRUE;
	}
	valid = found;

out:
	g_hash_table_unref(seen);
	return valid;
}

static gboolean
capability_equals(json_object *capabilities, const gchar *key,
		  const gchar *expected)
{
	const gchar *actual;

	return get_string(capabilities, key, &actual, 64) &&
	       g_str_equal(actual, expected);
}

static gboolean
validate_capabilities(json_object *root)
{
	static const gchar *const keys[] = {
		"shadow_inventory", "mbim", "ncm", "esim", "band_lock",
		"cell_lock", "sim_switch", NULL
	};
	static const gchar *const states[] = {
		"supported", "planned", "mbim-only-planned",
		"hardware-validation-required", "probe-required",
		"experimental-disabled", "unavailable", NULL
	};
	json_object *capabilities;
	guint count = 0;

	if (!get_object(root, "capabilities", &capabilities))
		return FALSE;
	if (!object_has_only(capabilities, keys))
		return FALSE;
	json_object_object_foreach(capabilities, key, value) {
		const gchar *state;
		gboolean allowed = FALSE;
		guint i;

		if (key[0] == '\0' || strlen(key) > 64 ||
		    !json_object_is_type(value, json_type_string))
			return FALSE;
		state = json_object_get_string(value);
		for (i = 0; states[i] != NULL; i++) {
			if (g_str_equal(state, states[i])) {
				allowed = TRUE;
				break;
			}
		}
		if (!allowed)
			return FALSE;
		count++;
	}
	return count == 7 &&
	       capability_equals(capabilities, "shadow_inventory", "supported") &&
	       capability_equals(capabilities, "mbim", "planned") &&
	       capability_equals(capabilities, "ncm", "hardware-validation-required") &&
	       capability_equals(capabilities, "esim", "mbim-only-planned") &&
	       capability_equals(capabilities, "band_lock", "planned") &&
	       capability_equals(capabilities, "cell_lock", "experimental-disabled") &&
	       capability_equals(capabilities, "sim_switch", "probe-required");
}

static gboolean
parse_usb_matches(FibocomProfile *profile, json_object *match)
{
	static const gchar *const match_allowed[] = {
		"manufacturer", "models_exact", "usb", NULL
	};
	static const gchar *const usb_allowed[] = {
		"vid", "pid", "composition", NULL
	};
	json_object *array;
	json_object *manufacturer;
	size_t length;
	size_t i;
	gboolean have_mbim = FALSE;
	gboolean have_ncm = FALSE;

	if (!object_has_only(match, match_allowed) ||
	    !validate_string_array(match, "models_exact", "L850-GL", 128, 1) ||
	    !get_array(match, "usb", &array))
		return FALSE;
	if (json_object_object_get_ex(match, "manufacturer", &manufacturer) &&
	    !validate_string_array(match, "manufacturer", "Fibocom", 64, 1))
		return FALSE;
	length = json_object_array_length(array);
	if (length != 2)
		return FALSE;
	for (i = 0; i < length; i++) {
		json_object *item = json_object_array_get_idx(array, i);
		const gchar *vid;
		const gchar *pid;
		const gchar *composition;
		FibocomUsbMatch *usb_match;

		if (!json_object_is_type(item, json_type_object) ||
		    !object_has_only(item, usb_allowed) ||
		    !get_string(item, "vid", &vid, 4) ||
		    !get_string(item, "pid", &pid, 4) ||
		    !get_string(item, "composition", &composition, 8) ||
		    !is_lower_hex4(vid) || !is_lower_hex4(pid))
			return FALSE;
		usb_match = g_new0(FibocomUsbMatch, 1);
		g_strlcpy(usb_match->vid, vid, sizeof(usb_match->vid));
		g_strlcpy(usb_match->pid, pid, sizeof(usb_match->pid));
		if (g_str_equal(vid, "2cb7") && g_str_equal(pid, "0007") &&
		    g_str_equal(composition, "mbim") && !have_mbim) {
			usb_match->composition = FIBOCOM_COMPOSITION_MBIM;
			have_mbim = TRUE;
		} else if (g_str_equal(vid, "8087") && g_str_equal(pid, "095a") &&
			   g_str_equal(composition, "ncm") && !have_ncm) {
			usb_match->composition = FIBOCOM_COMPOSITION_NCM;
			have_ncm = TRUE;
		} else {
			g_free(usb_match);
			return FALSE;
		}
		g_ptr_array_add(profile->usb_matches, usb_match);
	}
	return have_mbim && have_ncm;
}

static gchar *
read_bounded_file(const gchar *path, gsize *length, GError **error)
{
	gchar *buffer;
	gsize total = 0;
	struct stat status;
	int fd;

	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0) {
		g_set_error(error, profile_error_quark(), errno,
			    "cannot open profile: %s", g_strerror(errno));
		return NULL;
	}
	if (fstat(fd, &status) < 0) {
		g_set_error(error, profile_error_quark(), errno,
			    "cannot stat profile: %s", g_strerror(errno));
		close(fd);
		return NULL;
	}
	if (!S_ISREG(status.st_mode)) {
		g_set_error_literal(error, profile_error_quark(), EINVAL,
				    "profile is not a regular file");
		close(fd);
		return NULL;
	}
	if (status.st_size < 0 || (guint64)status.st_size > PROFILE_MAX_BYTES) {
		g_set_error_literal(error, profile_error_quark(), EFBIG,
				    "profile exceeds 64 KiB limit");
		close(fd);
		return NULL;
	}
	buffer = g_malloc(PROFILE_MAX_BYTES + 2U);
	while (total <= PROFILE_MAX_BYTES) {
		ssize_t count = read(fd, buffer + total,
				     PROFILE_MAX_BYTES + 1U - total);

		if (count > 0) {
			total += (gsize)count;
			continue;
		}
		if (count == 0)
			break;
		if (errno == EINTR)
			continue;
		g_set_error(error, profile_error_quark(), errno,
			    "cannot read profile: %s", g_strerror(errno));
		close(fd);
		g_free(buffer);
		return NULL;
	}
	close(fd);
	if (total > PROFILE_MAX_BYTES) {
		g_set_error_literal(error, profile_error_quark(), EFBIG,
				    "profile exceeds 64 KiB limit");
		g_free(buffer);
		return NULL;
	}
	buffer[total] = '\0';
	*length = total;
	return buffer;
}

FibocomProfile *
fibocom_profile_load(const gchar *path, GError **error)
{
	static const gchar *const top_allowed[] = {
		"schema", "id", "display_name", "match", "management_backend",
		"bearer_backends", "ports", "ncm", "capabilities", NULL
	};
	FibocomProfile *profile = NULL;
	json_tokener *tokener = NULL;
	json_object *root = NULL;
	json_object *value;
	json_object *match;
	json_object *ports;
	const gchar *id;
	const gchar *display_name;
	gchar *contents = NULL;
	gsize length = 0;
	size_t parsed;

	g_return_val_if_fail(path != NULL, NULL);
	contents = read_bounded_file(path, &length, error);
	if (contents == NULL)
		return NULL;
	tokener = json_tokener_new_ex(32);
	if (tokener == NULL) {
		set_invalid(error, "cannot allocate JSON parser");
		goto out;
	}
	root = json_tokener_parse_ex(tokener, contents, (int)length);
	parsed = json_tokener_get_parse_end(tokener);
	if (json_tokener_get_error(tokener) != json_tokener_success ||
	    root == NULL || !json_object_is_type(root, json_type_object)) {
		set_invalid(error, "JSON parse failed");
		goto out;
	}
	while (parsed < length && g_ascii_isspace(contents[parsed]))
		parsed++;
	if (parsed != length || !object_has_only(root, top_allowed)) {
		set_invalid(error, "unexpected JSON content or top-level field");
		goto out;
	}
	if (!json_object_object_get_ex(root, "schema", &value) ||
	    !json_object_is_type(value, json_type_int) || json_object_get_int(value) != 1 ||
	    !get_string(root, "id", &id, 64) || !g_str_equal(id, FIBOCOM_PROFILE_ID) ||
	    !get_string(root, "display_name", &display_name, 128) ||
	    !get_object(root, "match", &match) || !get_object(root, "ports", &ports) ||
	    !validate_backends(root) || !validate_ncm(root) ||
	    !validate_capabilities(root)) {
		set_invalid(error, "required field or schema mismatch");
		goto out;
	}

	profile = g_new0(FibocomProfile, 1);
	profile->ref_count = 1;
	profile->id = g_strdup(id);
	profile->display_name = g_strdup(display_name);
	profile->usb_matches = g_ptr_array_new_with_free_func(g_free);
	{
		static const gchar *const ports_allowed[] = { "mbim", "ncm", NULL };

		if (!object_has_only(ports, ports_allowed) ||
		    !parse_usb_matches(profile, match) ||
	    !parse_port_map(ports, "mbim", &profile->mbim_ports, FALSE) ||
		    !parse_port_map(ports, "ncm", &profile->ncm_ports, TRUE)) {
			set_invalid(error, "USB match or reviewed port map mismatch");
			fibocom_profile_unref(profile);
			profile = NULL;
			goto out;
		}
	}

out:
	if (root != NULL)
		json_object_put(root);
	if (tokener != NULL)
		json_tokener_free(tokener);
	g_free(contents);
	return profile;
}

FibocomProfile *
fibocom_profile_ref(FibocomProfile *profile)
{
	g_return_val_if_fail(profile != NULL, NULL);
	g_atomic_int_inc(&profile->ref_count);
	return profile;
}

void
fibocom_profile_unref(FibocomProfile *profile)
{
	if (profile == NULL || !g_atomic_int_dec_and_test(&profile->ref_count))
		return;
	g_free(profile->id);
	g_free(profile->display_name);
	g_ptr_array_unref(profile->usb_matches);
	if (profile->mbim_ports.ignored != NULL)
		g_array_unref(profile->mbim_ports.ignored);
	if (profile->mbim_ports.data_candidates != NULL)
		g_array_unref(profile->mbim_ports.data_candidates);
	if (profile->ncm_ports.ignored != NULL)
		g_array_unref(profile->ncm_ports.ignored);
	if (profile->ncm_ports.data_candidates != NULL)
		g_array_unref(profile->ncm_ports.data_candidates);
	g_free(profile);
}

const gchar *
fibocom_profile_id(const FibocomProfile *profile)
{
	return profile->id;
}

const gchar *
fibocom_profile_display_name(const FibocomProfile *profile)
{
	return profile->display_name;
}

const FibocomUsbMatch *
fibocom_profile_match_usb(const FibocomProfile *profile, const gchar *vid,
			  const gchar *pid)
{
	guint i;

	for (i = 0; i < profile->usb_matches->len; i++) {
		const FibocomUsbMatch *match = g_ptr_array_index(profile->usb_matches, i);

		if (g_str_equal(match->vid, vid) && g_str_equal(match->pid, pid))
			return match;
	}
	return NULL;
}

const FibocomPortMap *
fibocom_profile_port_map(const FibocomProfile *profile,
			 FibocomComposition composition)
{
	return composition == FIBOCOM_COMPOSITION_MBIM ?
	       &profile->mbim_ports : &profile->ncm_ports;
}
