/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "discovery.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SCAN_DEBOUNCE_MS 200U
#define RECONCILE_INTERVAL_SECONDS 30U
#define MAX_USB_ENTRIES 1024U
#define MAX_SUPPORTED_DEVICES 32U
#define MAX_INTERFACES_PER_DEVICE 64U
#define MAX_CLASS_ENTRIES 4096U
#define SYSFS_VALUE_MAX 255U
#define COMPONENT_MAX 96U

typedef enum {
	NODE_TTY,
	NODE_WDM,
	NODE_NETDEV,
} NodeKind;

typedef struct {
	GPtrArray *devices; /* FibocomDevice */
} ScanResult;

typedef struct {
	gchar *sysfs_root;
	FibocomProfile *profile;
	guint64 scan_id;
} ScanTaskData;

struct _FibocomDiscovery {
	gint ref_count;
	gchar *sysfs_root;
	FibocomProfile *profile;
	GPtrArray *devices; /* FibocomDevice */
	FibocomReconcile reconcile;
	GCancellable *cancellable;
	guint debounce_source;
	guint periodic_source;
	gboolean scan_running;
	gboolean scan_pending;
	gboolean stopping;
	guint64 next_scan_id;
	guint64 queued_scan_id;
	guint64 next_generation;
	FibocomDiscoveryChangedFunc changed_callback;
	gpointer changed_data;
};

static gboolean start_scan_cb(gpointer user_data);
static void schedule_debounced_scan(FibocomDiscovery *discovery, guint delay_ms);

static gboolean
safe_component(const gchar *value)
{
	gsize length;
	gsize i;

	if (value == NULL)
		return FALSE;
	length = strlen(value);
	if (length == 0 || length > COMPONENT_MAX ||
	    g_str_equal(value, ".") || g_str_equal(value, ".."))
		return FALSE;
	for (i = 0; i < length; i++) {
		if (!g_ascii_isalnum(value[i]) && value[i] != '-' &&
		    value[i] != '_' && value[i] != '.' && value[i] != ':')
			return FALSE;
	}
	return TRUE;
}

static gboolean
path_is_within(const gchar *path, const gchar *parent)
{
	gsize length;

	if (path == NULL || parent == NULL)
		return FALSE;
	if (g_str_equal(parent, G_DIR_SEPARATOR_S))
		return path[0] == G_DIR_SEPARATOR;
	length = strlen(parent);
	return strncmp(path, parent, length) == 0 &&
	       (path[length] == '\0' || path[length] == G_DIR_SEPARATOR);
}

static gchar *
realpath_dup(const gchar *path)
{
	char *resolved = realpath(path, NULL);
	gchar *result;

	if (resolved == NULL)
		return NULL;
	result = g_strdup(resolved);
	free(resolved);
	return result;
}

static gchar *
read_attribute(const gchar *directory, const gchar *name)
{
	gchar buffer[SYSFS_VALUE_MAX + 1U];
	gchar *path;
	ssize_t count;
	int fd;

	path = g_build_filename(directory, name, NULL);
	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	g_free(path);
	if (fd < 0)
		return NULL;
	count = read(fd, buffer, SYSFS_VALUE_MAX);
	close(fd);
	if (count <= 0 || count == SYSFS_VALUE_MAX)
		return NULL;
	buffer[count] = '\0';
	return g_strdup(g_strstrip(buffer));
}

static gboolean
valid_hex4(gchar *value)
{
	guint i;

	if (value == NULL || strlen(value) != 4)
		return FALSE;
	for (i = 0; i < 4; i++) {
		value[i] = g_ascii_tolower(value[i]);
		if (!g_ascii_isxdigit(value[i]))
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
uint_array_contains(const GArray *array, guint needle)
{
	guint i;

	for (i = 0; i < array->len; i++) {
		if (g_array_index(array, guint, i) == needle)
			return TRUE;
	}
	return FALSE;
}

static gchar *
driver_basename(const gchar *interface_path)
{
	gchar *path = g_build_filename(interface_path, "driver", NULL);
	gchar *target = g_file_read_link(path, NULL);
	gchar *basename = NULL;

	if (target != NULL)
		basename = g_path_get_basename(target);
	g_free(target);
	g_free(path);
	if (basename != NULL && !safe_component(basename))
		g_clear_pointer(&basename, g_free);
	return basename != NULL ? basename : g_strdup("");
}

static FibocomInterface *
interface_new(guint number, const gchar *sysfs_path, const gchar *driver,
	      const FibocomPortMap *map, FibocomComposition composition)
{
	FibocomInterface *interface = g_new0(FibocomInterface, 1);

	interface->number = g_strdup_printf("%02x", number);
	interface->index = number;
	interface->sysfs_path = g_strdup(sysfs_path);
	interface->driver = g_strdup(driver != NULL ? driver : "");
	interface->ttys = g_ptr_array_new_with_free_func((GDestroyNotify)fibocom_port_free);
	interface->wdms = g_ptr_array_new_with_free_func((GDestroyNotify)fibocom_port_free);
	interface->netdevs = g_ptr_array_new_with_free_func((GDestroyNotify)fibocom_port_free);
	if (number == map->at_primary)
		interface->role = g_strdup("at-primary");
	else if (map->has_at_secondary && number == map->at_secondary)
		interface->role = g_strdup("at-secondary");
	else if (uint_array_contains(map->ignored, number))
		interface->role = g_strdup("ignored");
	else if (composition == FIBOCOM_COMPOSITION_NCM &&
		 uint_array_contains(map->data_candidates, number))
		interface->role = g_strdup("ncm-data-candidate");
	else
		interface->role = g_strdup("unknown");
	return interface;
}

static gint
interface_compare(gconstpointer left, gconstpointer right)
{
	const FibocomInterface *a = *(FibocomInterface *const *)left;
	const FibocomInterface *b = *(FibocomInterface *const *)right;

	if (a->index != b->index)
		return a->index < b->index ? -1 : 1;
	return g_strcmp0(a->sysfs_path, b->sysfs_path);
}

static gint
port_compare(gconstpointer left, gconstpointer right)
{
	const FibocomPort *a = *(FibocomPort *const *)left;
	const FibocomPort *b = *(FibocomPort *const *)right;

	return g_strcmp0(a->name, b->name);
}

static gboolean
interface_path_exists(const FibocomDevice *device, const gchar *path)
{
	guint i;

	for (i = 0; i < device->interfaces->len; i++) {
		const FibocomInterface *interface = g_ptr_array_index(device->interfaces, i);

		if (g_str_equal(interface->sysfs_path, path))
			return TRUE;
	}
	return FALSE;
}

static void
try_add_interface(FibocomDevice *device, const gchar *candidate,
		  const gchar *physical_real, const FibocomPortMap *map,
		  FibocomComposition composition)
{
	FibocomInterface *interface;
	gchar *real;
	gchar *number_value;
	gchar *driver;
	guint number;

	if (device->interfaces->len >= MAX_INTERFACES_PER_DEVICE)
		return;
	real = realpath_dup(candidate);
	if (real == NULL || !path_is_within(real, physical_real) ||
	    interface_path_exists(device, real)) {
		g_free(real);
		return;
	}
	number_value = read_attribute(candidate, "bInterfaceNumber");
	if (!parse_interface_number(number_value, &number)) {
		g_free(number_value);
		g_free(real);
		return;
	}
	driver = driver_basename(candidate);
	interface = interface_new(number, real, driver, map, composition);
	g_ptr_array_add(device->interfaces, interface);
	g_free(driver);
	g_free(number_value);
	g_free(real);
}

static void
scan_interface_directory(FibocomDevice *device, const gchar *directory,
			 const gchar *slot, const gchar *physical_real,
			 const FibocomPortMap *map,
			 FibocomComposition composition, gboolean require_prefix)
{
	GDir *dir;
	const gchar *entry;
	guint count = 0;
	gchar *prefix = g_strdup_printf("%s:", slot);

	dir = g_dir_open(directory, 0, NULL);
	if (dir == NULL) {
		g_free(prefix);
		return;
	}
	while ((entry = g_dir_read_name(dir)) != NULL && count++ < MAX_USB_ENTRIES) {
		gchar *path;

		if (!safe_component(entry) ||
		    (require_prefix && !g_str_has_prefix(entry, prefix)))
			continue;
		path = g_build_filename(directory, entry, NULL);
		try_add_interface(device, path, physical_real, map, composition);
		g_free(path);
	}
	g_dir_close(dir);
	g_free(prefix);
}

static FibocomInterface *
find_interface_for_path(FibocomDevice *device, const gchar *path)
{
	FibocomInterface *best = NULL;
	gsize best_length = 0;
	guint i;

	for (i = 0; i < device->interfaces->len; i++) {
		FibocomInterface *interface = g_ptr_array_index(device->interfaces, i);
		gsize length = strlen(interface->sysfs_path);

		if (length > best_length && path_is_within(path, interface->sysfs_path)) {
			best = interface;
			best_length = length;
		}
	}
	return best;
}

static GPtrArray *
port_array_for_kind(FibocomInterface *interface, NodeKind kind)
{
	switch (kind) {
	case NODE_TTY:
		return interface->ttys;
	case NODE_WDM:
		return interface->wdms;
	case NODE_NETDEV:
	default:
		return interface->netdevs;
	}
}

static const GPtrArray *
const_port_array_for_kind(const FibocomInterface *interface, NodeKind kind)
{
	switch (kind) {
	case NODE_TTY:
		return interface->ttys;
	case NODE_WDM:
		return interface->wdms;
	case NODE_NETDEV:
	default:
		return interface->netdevs;
	}
}

static void
add_port(FibocomInterface *interface, NodeKind kind, const gchar *name)
{
	GPtrArray *ports = port_array_for_kind(interface, kind);
	FibocomPort *port;
	guint i;

	if (!safe_component(name))
		return;
	for (i = 0; i < ports->len; i++) {
		const FibocomPort *existing = g_ptr_array_index(ports, i);

		if (g_str_equal(existing->name, name))
			return;
	}
	port = g_new0(FibocomPort, 1);
	port->name = g_strdup(name);
	port->interface_number = g_strdup(interface->number);
	port->interface_index = interface->index;
	port->driver = g_strdup(interface->driver);
	g_ptr_array_add(ports, port);
	if ((kind == NODE_WDM || kind == NODE_NETDEV) &&
	    g_str_equal(interface->role, "unknown")) {
		g_free(interface->role);
		interface->role = g_strdup(kind == NODE_WDM ? "mbim-control" : "mbim-data");
	}
}

static void
scan_class(FibocomDevice *device, const gchar *sysfs_root,
	   const gchar *class_name, NodeKind kind)
{
	gchar *class_path = g_build_filename(sysfs_root, "class", class_name, NULL);
	GDir *dir = g_dir_open(class_path, 0, NULL);
	const gchar *entry;
	guint count = 0;

	if (dir == NULL) {
		g_free(class_path);
		return;
	}
	while ((entry = g_dir_read_name(dir)) != NULL && count++ < MAX_CLASS_ENTRIES) {
		FibocomInterface *interface;
		gchar *device_link;
		gchar *real;

		if (!safe_component(entry) ||
		    (kind == NODE_TTY && !g_str_has_prefix(entry, "ttyACM")) ||
		    (kind == NODE_WDM && !g_str_has_prefix(entry, "cdc-wdm")))
			continue;
		device_link = g_build_filename(class_path, entry, "device", NULL);
		real = realpath_dup(device_link);
		g_free(device_link);
		if (real == NULL)
			continue;
		interface = find_interface_for_path(device, real);
		if (interface != NULL)
			add_port(interface, kind, entry);
		g_free(real);
	}
	g_dir_close(dir);
	g_free(class_path);
}

static void
scan_direct_subdirectory(FibocomInterface *interface, const gchar *name,
			 NodeKind kind, const gchar *required_prefix)
{
	gchar *path = g_build_filename(interface->sysfs_path, name, NULL);
	GDir *dir = g_dir_open(path, 0, NULL);
	const gchar *entry;
	guint count = 0;

	if (dir == NULL) {
		g_free(path);
		return;
	}
	while ((entry = g_dir_read_name(dir)) != NULL && count++ < 128U) {
		if (required_prefix == NULL || g_str_has_prefix(entry, required_prefix))
			add_port(interface, kind, entry);
	}
	g_dir_close(dir);
	g_free(path);
}

static void
scan_direct_nodes(FibocomDevice *device)
{
	guint i;

	for (i = 0; i < device->interfaces->len; i++) {
		FibocomInterface *interface = g_ptr_array_index(device->interfaces, i);

		scan_direct_subdirectory(interface, "tty", NODE_TTY, "ttyACM");
		scan_direct_subdirectory(interface, "usbmisc", NODE_WDM, "cdc-wdm");
		scan_direct_subdirectory(interface, "net", NODE_NETDEV, NULL);
	}
}

static guint
count_ports_for_role(const FibocomDevice *device, const gchar *role, NodeKind kind)
{
	guint count = 0;
	guint i;

	for (i = 0; i < device->interfaces->len; i++) {
		const FibocomInterface *interface = g_ptr_array_index(device->interfaces, i);

		if (g_str_equal(interface->role, role))
			count += const_port_array_for_kind(interface, kind)->len;
	}
	return count;
}

static guint
count_all_ports(const FibocomDevice *device, NodeKind kind)
{
	guint count = 0;
	guint i;

	for (i = 0; i < device->interfaces->len; i++) {
		FibocomInterface *interface = g_ptr_array_index(device->interfaces, i);

		count += port_array_for_kind(interface, kind)->len;
	}
	return count;
}

static guint
count_netdevs_on_interface(const FibocomDevice *device, guint interface_index)
{
	guint count = 0;
	guint i;

	for (i = 0; i < device->interfaces->len; i++) {
		const FibocomInterface *interface = g_ptr_array_index(device->interfaces, i);

		if (interface->index == interface_index)
			count += interface->netdevs->len;
	}
	return count;
}

static guint
count_interfaces_with_index(const FibocomDevice *device, guint interface_index)
{
	guint count = 0;
	guint i;

	for (i = 0; i < device->interfaces->len; i++) {
		const FibocomInterface *interface = g_ptr_array_index(device->interfaces, i);

		if (interface->index == interface_index)
			count++;
	}
	return count;
}

static gboolean
driver_matches_if_present(const FibocomDevice *device, guint interface_index,
			  const gchar *expected_driver)
{
	guint i;

	for (i = 0; i < device->interfaces->len; i++) {
		const FibocomInterface *interface = g_ptr_array_index(device->interfaces, i);

		if (interface->index == interface_index &&
		    !g_str_equal(interface->driver, expected_driver))
			return FALSE;
	}
	return TRUE;
}

static const FibocomInterface *
mbim_shared_interface(const FibocomDevice *device)
{
	guint i;

	for (i = 0; i < device->interfaces->len; i++) {
		const FibocomInterface *interface = g_ptr_array_index(device->interfaces, i);

		if (interface->wdms->len == 1 && interface->netdevs->len == 1)
			return interface;
	}
	return NULL;
}

static void
set_topology(FibocomDevice *device, FibocomComposition composition,
	     const FibocomPortMap *map)
{
	guint primary = count_ports_for_role(device, "at-primary", NODE_TTY);
	guint secondary = count_ports_for_role(device, "at-secondary", NODE_TTY);
	guint ignored = count_ports_for_role(device, "ignored", NODE_TTY);
	guint wdms = count_all_ports(device, NODE_WDM);
	guint netdevs = count_all_ports(device, NODE_NETDEV);

	device->topology_status = FIBOCOM_TOPOLOGY_PARTIAL;
	g_free(device->topology_reason);
	device->topology_reason = NULL;
	if (primary > 1 || secondary > 1 || ignored > 1 || wdms > 1) {
		device->topology_status = FIBOCOM_TOPOLOGY_AMBIGUOUS;
		device->topology_reason = g_strdup("duplicate-role-port");
		return;
	}
	if (count_interfaces_with_index(device, map->at_primary) > 1 ||
	    (map->has_at_secondary &&
	     count_interfaces_with_index(device, map->at_secondary) > 1)) {
		device->topology_status = FIBOCOM_TOPOLOGY_AMBIGUOUS;
		device->topology_reason = g_strdup("duplicate-role-interface");
		return;
	}
	for (guint i = 0; i < map->ignored->len; i++) {
		guint ignored_index = g_array_index(map->ignored, guint, i);

		if (count_interfaces_with_index(device, ignored_index) > 1) {
			device->topology_status = FIBOCOM_TOPOLOGY_AMBIGUOUS;
			device->topology_reason = g_strdup("duplicate-ignored-interface");
			return;
		}
	}
	/* Reviewed L850 P1 driver contract; move to typed profile data for model 2. */
	if (!driver_matches_if_present(device, map->at_primary, "cdc_acm") ||
	    (map->has_at_secondary &&
	     !driver_matches_if_present(device, map->at_secondary, "cdc_acm"))) {
		device->topology_reason = g_strdup("driver-mismatch");
		return;
	}
	for (guint i = 0; i < map->ignored->len; i++) {
		guint ignored_index = g_array_index(map->ignored, guint, i);

		if (!driver_matches_if_present(device, ignored_index, "cdc_acm")) {
			device->topology_reason = g_strdup("driver-mismatch");
			return;
		}
	}
	if (primary != 1) {
		device->topology_reason = g_strdup("missing-at-primary");
		return;
	}
	if (secondary != 1) {
		device->topology_reason = g_strdup("missing-at-secondary");
		return;
	}
	if (ignored != 1) {
		device->topology_reason = g_strdup("missing-ignored-port");
		return;
	}
	if (composition == FIBOCOM_COMPOSITION_MBIM) {
		const FibocomInterface *data_interface;

		if (netdevs > 1) {
			device->topology_status = FIBOCOM_TOPOLOGY_AMBIGUOUS;
			device->topology_reason = g_strdup("duplicate-mbim-netdev");
			return;
		}
		if (wdms != 1 || netdevs != 1) {
			device->topology_reason = g_strdup("missing-mbim-control-or-netdev");
			return;
		}
		data_interface = mbim_shared_interface(device);
		if (data_interface == NULL) {
			device->topology_status = FIBOCOM_TOPOLOGY_AMBIGUOUS;
			device->topology_reason = g_strdup("mbim-parent-mismatch");
			return;
		}
		if (count_interfaces_with_index(device, data_interface->index) > 1) {
			device->topology_status = FIBOCOM_TOPOLOGY_AMBIGUOUS;
			device->topology_reason =
				g_strdup("duplicate-mbim-data-interface");
			return;
		}
		/* Reviewed L850 MBIM control/data interface is USB interface #00. */
		if (data_interface->index != 0x00) {
			device->topology_reason = g_strdup("wrong-mbim-data-interface");
			return;
		}
		if (!g_str_equal(data_interface->driver, "cdc_mbim")) {
			device->topology_reason = g_strdup("driver-mismatch");
			return;
		}
	} else {
		guint candidate_netdevs = 0;
		guint i;

		if (wdms != 0) {
			device->topology_status = FIBOCOM_TOPOLOGY_AMBIGUOUS;
			device->topology_reason = g_strdup("unexpected-ncm-control-port");
			return;
		}
		for (i = 0; i < map->data_candidates->len; i++) {
			guint candidate = g_array_index(map->data_candidates, guint, i);
			guint count = count_netdevs_on_interface(device, candidate);
			guint interface_count = count_interfaces_with_index(device, candidate);

			if (!driver_matches_if_present(device, candidate, "cdc_ncm")) {
				device->topology_reason = g_strdup("driver-mismatch");
				return;
			}
			if (interface_count > 1) {
				device->topology_status = FIBOCOM_TOPOLOGY_AMBIGUOUS;
				device->topology_reason = g_strdup("duplicate-ncm-interface");
				return;
			}
			if (interface_count == 0) {
				device->topology_reason = g_strdup("ncm-candidates-incomplete");
				return;
			}
			if (count > 1) {
				device->topology_status = FIBOCOM_TOPOLOGY_AMBIGUOUS;
				device->topology_reason = g_strdup("duplicate-ncm-candidate");
				return;
			}
			if (count == 0) {
				device->topology_reason = g_strdup("ncm-candidates-incomplete");
				return;
			}
			candidate_netdevs += count;
		}
		if (netdevs != candidate_netdevs) {
			device->topology_status = FIBOCOM_TOPOLOGY_AMBIGUOUS;
			device->topology_reason = g_strdup("unexpected-ncm-netdev");
			return;
		}
	}
	device->topology_status = FIBOCOM_TOPOLOGY_COMPLETE;
	device->topology_reason = g_strdup("exact-profile-match");
}

static gchar *
device_fingerprint(const FibocomDevice *device)
{
	GChecksum *checksum = g_checksum_new(G_CHECKSUM_SHA256);
	guint i;
	static const gchar tty_marker[] = "tty";
	static const gchar wdm_marker[] = "wdm";
	static const gchar net_marker[] = "net";
	const gchar *markers[] = { tty_marker, wdm_marker, net_marker };

	g_checksum_update(checksum, (const guchar *)device->vid, strlen(device->vid) + 1U);
	g_checksum_update(checksum, (const guchar *)device->pid, strlen(device->pid) + 1U);
	for (i = 0; i < device->interfaces->len; i++) {
		const FibocomInterface *interface = g_ptr_array_index(device->interfaces, i);
		GPtrArray *arrays[] = { interface->ttys, interface->wdms, interface->netdevs };
		guint a;

		g_checksum_update(checksum, (const guchar *)interface->number,
				  strlen(interface->number) + 1U);
		g_checksum_update(checksum, (const guchar *)interface->driver,
				  strlen(interface->driver) + 1U);
		for (a = 0; a < G_N_ELEMENTS(arrays); a++) {
			guint p;

			g_checksum_update(checksum, (const guchar *)markers[a],
					  strlen(markers[a]) + 1U);
			for (p = 0; p < arrays[a]->len; p++) {
				const FibocomPort *port = g_ptr_array_index(arrays[a], p);

				g_checksum_update(checksum, (const guchar *)port->name,
						  strlen(port->name) + 1U);
			}
		}
	}
	{
		gchar *result = g_strdup(g_checksum_get_string(checksum));
		g_checksum_free(checksum);
		return result;
	}
}

static gboolean
valid_serial(const gchar *serial)
{
	gsize length;
	gsize i;
	gboolean any_nonzero = FALSE;

	if (serial == NULL)
		return FALSE;
	length = strlen(serial);
	if (length < 4 || length > 128 ||
	    g_str_equal(serial, "0123456789ABCDEF") ||
	    g_str_equal(serial, "1234567890"))
		return FALSE;
	for (i = 0; i < length; i++) {
		if (!g_ascii_isprint(serial[i]))
			return FALSE;
		if (serial[i] != '0' && !g_ascii_isspace(serial[i]))
			any_nonzero = TRUE;
	}
	return any_nonzero;
}

static gchar *
make_device_id(const gchar *physical_directory, const gchar *slot,
	       gchar **identity_scope)
{
	gchar *serial = read_attribute(physical_directory, "serial");
	gchar *material;
	gchar *hash;
	gchar *device_id;

	if (valid_serial(serial)) {
		material = g_strdup_printf("fibocom-l850:v1:serial:%s", serial);
		*identity_scope = g_strdup("usb-serial-hash");
	} else {
		material = g_strdup_printf("fibocom-l850:v1:path:%s", slot);
		*identity_scope = g_strdup("path-scoped");
	}
	hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, material, -1);
	device_id = g_strdup_printf("l850-%s", hash);
	g_free(hash);
	g_free(material);
	g_free(serial);
	return device_id;
}

static FibocomDevice *
scan_device(const gchar *sysfs_root, const gchar *usb_directory,
	    const gchar *slot, const gchar *physical_directory,
	    const gchar *physical_real, const gchar *vid, const gchar *pid,
	    const FibocomUsbMatch *match, const FibocomProfile *profile)
{
	const FibocomPortMap *map = fibocom_profile_port_map(profile, match->composition);
	FibocomDevice *device = g_new0(FibocomDevice, 1);
	guint i;

	device->physical_path = g_strdup(slot);
	device->vid = g_strdup(vid);
	device->pid = g_strdup(pid);
	device->composition = g_strdup(match->composition == FIBOCOM_COMPOSITION_MBIM ?
				       "mbim" : "ncm");
	device->present = TRUE;
	device->interfaces = g_ptr_array_new_with_free_func(
		(GDestroyNotify)fibocom_interface_free);
	device->device_id = make_device_id(physical_directory, slot,
					   &device->identity_scope);
	scan_interface_directory(device, usb_directory, slot, physical_real, map,
				 match->composition, TRUE);
	scan_interface_directory(device, physical_real, slot, physical_real, map,
				 match->composition, FALSE);
	g_ptr_array_sort(device->interfaces, interface_compare);
	scan_class(device, sysfs_root, "tty", NODE_TTY);
	scan_class(device, sysfs_root, "usbmisc", NODE_WDM);
	scan_class(device, sysfs_root, "net", NODE_NETDEV);
	scan_direct_nodes(device);
	for (i = 0; i < device->interfaces->len; i++) {
		FibocomInterface *interface = g_ptr_array_index(device->interfaces, i);

		g_ptr_array_sort(interface->ttys, port_compare);
		g_ptr_array_sort(interface->wdms, port_compare);
		g_ptr_array_sort(interface->netdevs, port_compare);
	}
	set_topology(device, match->composition, map);
	device->fingerprint = device_fingerprint(device);
	return device;
}

static void
mark_duplicate_identities(GPtrArray *devices)
{
	GHashTable *groups = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
						 (GDestroyNotify)g_ptr_array_unref);
	GHashTableIter iter;
	gpointer value;
	guint i;

	for (i = 0; i < devices->len; i++) {
		FibocomDevice *device = g_ptr_array_index(devices, i);
		GPtrArray *group = g_hash_table_lookup(groups, device->device_id);

		if (group == NULL) {
			group = g_ptr_array_new();
			g_hash_table_insert(groups, g_strdup(device->device_id), group);
		}
		g_ptr_array_add(group, device);
	}
	g_hash_table_iter_init(&iter, groups);
	while (g_hash_table_iter_next(&iter, NULL, &value)) {
		GPtrArray *group = value;

		if (group->len < 2)
			continue;
		for (i = 0; i < group->len; i++) {
			FibocomDevice *device = g_ptr_array_index(group, i);
			gchar *slot_hash = g_compute_checksum_for_string(
				G_CHECKSUM_SHA256, device->physical_path, -1);
			gchar *unique = g_strdup_printf("%s-slot-%.12s",
				device->device_id, slot_hash);

			g_free(slot_hash);
			g_free(device->device_id);
			device->device_id = unique;
			device->topology_status = FIBOCOM_TOPOLOGY_AMBIGUOUS;
			g_free(device->topology_reason);
			device->topology_reason = g_strdup("duplicate-usb-identity");
		}
	}
	g_hash_table_unref(groups);
}

static gint
device_compare(gconstpointer left, gconstpointer right)
{
	const FibocomDevice *a = *(FibocomDevice *const *)left;
	const FibocomDevice *b = *(FibocomDevice *const *)right;

	return g_strcmp0(a->physical_path, b->physical_path);
}

static ScanResult *
scan_sysfs(const ScanTaskData *task, GCancellable *cancellable, GError **error)
{
	ScanResult *result = g_new0(ScanResult, 1);
	gchar *root_real = realpath_dup(task->sysfs_root);
	gchar *usb_directory;
	GDir *dir;
	const gchar *entry;
	guint count = 0;

	result->devices = g_ptr_array_new_with_free_func((GDestroyNotify)fibocom_device_free);
	if (root_real == NULL) {
		g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
			    "sysfs root unavailable");
		goto fail;
	}
	usb_directory = g_build_filename(task->sysfs_root, "bus", "usb", "devices", NULL);
	dir = g_dir_open(usb_directory, 0, error);
	if (dir == NULL) {
		g_free(usb_directory);
		g_free(root_real);
		goto fail;
	}
	while ((entry = g_dir_read_name(dir)) != NULL && count++ < MAX_USB_ENTRIES &&
	       result->devices->len < MAX_SUPPORTED_DEVICES) {
		const FibocomUsbMatch *match;
		FibocomDevice *device;
		gchar *physical_directory;
		gchar *physical_real;
		gchar *vid;
		gchar *pid;

		if (g_cancellable_is_cancelled(cancellable)) {
			g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_CANCELLED,
					    "sysfs scan cancelled");
			g_dir_close(dir);
			g_free(usb_directory);
			g_free(root_real);
			goto fail;
		}
		if (!safe_component(entry) || strchr(entry, ':') != NULL)
			continue;
		physical_directory = g_build_filename(usb_directory, entry, NULL);
		vid = read_attribute(physical_directory, "idVendor");
		pid = read_attribute(physical_directory, "idProduct");
		if (!valid_hex4(vid) || !valid_hex4(pid) ||
		    (match = fibocom_profile_match_usb(task->profile, vid, pid)) == NULL) {
			g_free(vid);
			g_free(pid);
			g_free(physical_directory);
			continue;
		}
		physical_real = realpath_dup(physical_directory);
		if (physical_real == NULL || !path_is_within(physical_real, root_real)) {
			g_free(physical_real);
			g_free(vid);
			g_free(pid);
			g_free(physical_directory);
			continue;
		}
		device = scan_device(task->sysfs_root, usb_directory, entry,
				     physical_directory, physical_real, vid, pid,
				     match, task->profile);
		g_ptr_array_add(result->devices, device);
		g_free(physical_real);
		g_free(vid);
		g_free(pid);
		g_free(physical_directory);
	}
	g_dir_close(dir);
	g_free(usb_directory);
	g_free(root_real);
	mark_duplicate_identities(result->devices);
	g_ptr_array_sort(result->devices, device_compare);
	return result;

fail:
	g_ptr_array_unref(result->devices);
	g_free(result);
	return NULL;
}

static void
scan_result_free(ScanResult *result)
{
	if (result == NULL)
		return;
	g_ptr_array_unref(result->devices);
	g_free(result);
}

static void
scan_task_data_free(ScanTaskData *task)
{
	if (task == NULL)
		return;
	g_free(task->sysfs_root);
	fibocom_profile_unref(task->profile);
	g_free(task);
}

static void
scan_worker(GTask *task, gpointer source_object, gpointer task_data,
	    GCancellable *cancellable)
{
	ScanTaskData *data = task_data;
	ScanResult *result;
	GError *error = NULL;

	(void)source_object;
	result = scan_sysfs(data, cancellable, &error);
	if (result == NULL)
		g_task_return_error(task, error);
	else
		g_task_return_pointer(task, result, (GDestroyNotify)scan_result_free);
}

static void
clear_reconcile_error(FibocomReconcile *reconcile)
{
	g_clear_pointer(&reconcile->last_error, g_free);
}

static void
apply_scan(FibocomDiscovery *discovery, ScanTaskData *task, ScanResult *result)
{
	GHashTable *old_by_slot = g_hash_table_new(g_str_hash, g_str_equal);
	GHashTable *seen_slots = g_hash_table_new(g_str_hash, g_str_equal);
	GPtrArray *old_devices = discovery->devices;
	guint i;

	clear_reconcile_error(&discovery->reconcile);
	discovery->reconcile.scan_id = task->scan_id;
	discovery->reconcile.completed_at = g_get_real_time() / G_USEC_PER_SEC;
	discovery->reconcile.device_count = result->devices->len;
	discovery->reconcile.added = 0;
	discovery->reconcile.removed = 0;
	discovery->reconcile.changed = 0;
	discovery->reconcile.unchanged = 0;
	for (i = 0; i < old_devices->len; i++) {
		FibocomDevice *old = g_ptr_array_index(old_devices, i);

		g_hash_table_insert(old_by_slot, old->physical_path, old);
	}
	for (i = 0; i < result->devices->len; i++) {
		FibocomDevice *device = g_ptr_array_index(result->devices, i);
		FibocomDevice *old = g_hash_table_lookup(old_by_slot, device->physical_path);

		g_hash_table_add(seen_slots, device->physical_path);
		if (old == NULL) {
			device->generation = ++discovery->next_generation;
			discovery->reconcile.added++;
		} else if (g_str_equal(old->fingerprint, device->fingerprint) &&
			   g_str_equal(old->device_id, device->device_id)) {
			device->generation = old->generation;
			discovery->reconcile.unchanged++;
		} else {
			device->generation = ++discovery->next_generation;
			discovery->reconcile.changed++;
		}
	}
	for (i = 0; i < old_devices->len; i++) {
		FibocomDevice *old = g_ptr_array_index(old_devices, i);

		if (!g_hash_table_contains(seen_slots, old->physical_path)) {
			discovery->next_generation++;
			discovery->reconcile.removed++;
		}
	}
	discovery->devices = g_ptr_array_ref(result->devices);
	g_ptr_array_unref(old_devices);
	g_hash_table_unref(old_by_slot);
	g_hash_table_unref(seen_slots);
}

static void
scan_complete(GObject *source_object, GAsyncResult *async_result, gpointer user_data)
{
	FibocomDiscovery *discovery = user_data;
	GTask *task = G_TASK(async_result);
	ScanTaskData *task_data = g_task_get_task_data(task);
	GError *error = NULL;
	ScanResult *result;
	gboolean notify = FALSE;
	gboolean superseded;

	(void)source_object;
	discovery->scan_running = FALSE;
	result = g_task_propagate_pointer(task, &error);
	superseded = task_data->scan_id < discovery->queued_scan_id;
	if (!discovery->stopping) {
		if (superseded) {
			g_debug("discarding superseded shadow scan=%" G_GUINT64_FORMAT,
				task_data->scan_id);
		} else if (result != NULL) {
			apply_scan(discovery, task_data, result);
			notify = TRUE;
		} else if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			clear_reconcile_error(&discovery->reconcile);
			discovery->reconcile.scan_id = task_data->scan_id;
			discovery->reconcile.completed_at =
				g_get_real_time() / G_USEC_PER_SEC;
			discovery->reconcile.device_count = discovery->devices->len;
			discovery->reconcile.added = 0;
			discovery->reconcile.removed = 0;
			discovery->reconcile.changed = 0;
			discovery->reconcile.unchanged = discovery->devices->len;
			discovery->reconcile.last_error = g_strdup("sysfs-scan-failed");
			g_warning("shadow sysfs reconciliation failed: %s", error->message);
			notify = TRUE;
		}
		if (notify && discovery->changed_callback != NULL)
			discovery->changed_callback(discovery, &discovery->reconcile,
						    discovery->changed_data);
		if (discovery->scan_pending)
			schedule_debounced_scan(discovery, 0);
	}
	g_clear_error(&error);
	if (result != NULL)
		scan_result_free(result);
	fibocom_discovery_unref(discovery);
}

static gboolean
start_scan_cb(gpointer user_data)
{
	FibocomDiscovery *discovery = user_data;
	ScanTaskData *task_data;
	GTask *task;

	discovery->debounce_source = 0;
	if (discovery->stopping)
		return G_SOURCE_REMOVE;
	if (discovery->scan_running) {
		discovery->scan_pending = TRUE;
		return G_SOURCE_REMOVE;
	}
	discovery->scan_pending = FALSE;
	discovery->scan_running = TRUE;
	task_data = g_new0(ScanTaskData, 1);
	task_data->sysfs_root = g_strdup(discovery->sysfs_root);
	task_data->profile = fibocom_profile_ref(discovery->profile);
	task_data->scan_id = discovery->queued_scan_id;
	task = g_task_new(NULL, discovery->cancellable, scan_complete,
			  fibocom_discovery_ref(discovery));
	g_task_set_task_data(task, task_data, (GDestroyNotify)scan_task_data_free);
	g_task_set_return_on_cancel(task, FALSE);
	g_task_run_in_thread(task, scan_worker);
	g_object_unref(task);
	return G_SOURCE_REMOVE;
}

static void
schedule_debounced_scan(FibocomDiscovery *discovery, guint delay_ms)
{
	if (discovery->stopping)
		return;
	if (discovery->scan_running) {
		discovery->scan_pending = TRUE;
		return;
	}
	if (discovery->debounce_source != 0)
		g_source_remove(discovery->debounce_source);
	discovery->scan_pending = TRUE;
	discovery->debounce_source = g_timeout_add_full(
		G_PRIORITY_DEFAULT, delay_ms, start_scan_cb,
		fibocom_discovery_ref(discovery),
		(GDestroyNotify)fibocom_discovery_unref);
}

static gboolean
periodic_scan_cb(gpointer user_data)
{
	FibocomDiscovery *discovery = user_data;

	if (discovery->stopping)
		return G_SOURCE_REMOVE;
	fibocom_discovery_request_scan(discovery, "periodic-reconcile");
	return G_SOURCE_CONTINUE;
}

FibocomDiscovery *
fibocom_discovery_new(const gchar *sysfs_root, FibocomProfile *profile)
{
	FibocomDiscovery *discovery;

	g_return_val_if_fail(sysfs_root != NULL, NULL);
	g_return_val_if_fail(profile != NULL, NULL);
	discovery = g_new0(FibocomDiscovery, 1);
	discovery->ref_count = 1;
	discovery->sysfs_root = g_canonicalize_filename(sysfs_root, NULL);
	discovery->profile = fibocom_profile_ref(profile);
	discovery->devices = g_ptr_array_new_with_free_func(
		(GDestroyNotify)fibocom_device_free);
	discovery->cancellable = g_cancellable_new();
	return discovery;
}

FibocomDiscovery *
fibocom_discovery_ref(FibocomDiscovery *discovery)
{
	g_return_val_if_fail(discovery != NULL, NULL);
	g_atomic_int_inc(&discovery->ref_count);
	return discovery;
}

void
fibocom_discovery_unref(FibocomDiscovery *discovery)
{
	if (discovery == NULL || !g_atomic_int_dec_and_test(&discovery->ref_count))
		return;
	g_free(discovery->sysfs_root);
	fibocom_profile_unref(discovery->profile);
	g_ptr_array_unref(discovery->devices);
	g_free(discovery->reconcile.last_error);
	g_object_unref(discovery->cancellable);
	g_free(discovery);
}

void
fibocom_discovery_set_changed_callback(FibocomDiscovery *discovery,
				       FibocomDiscoveryChangedFunc callback,
				       gpointer user_data)
{
	discovery->changed_callback = callback;
	discovery->changed_data = user_data;
}

void
fibocom_discovery_start(FibocomDiscovery *discovery)
{
	if (discovery->stopping || discovery->periodic_source != 0)
		return;
	fibocom_discovery_request_scan(discovery, "cold-start");
	discovery->periodic_source = g_timeout_add_seconds_full(
		G_PRIORITY_LOW, RECONCILE_INTERVAL_SECONDS, periodic_scan_cb,
		fibocom_discovery_ref(discovery),
		(GDestroyNotify)fibocom_discovery_unref);
}

void
fibocom_discovery_stop(FibocomDiscovery *discovery)
{
	if (discovery->stopping)
		return;
	discovery->stopping = TRUE;
	g_cancellable_cancel(discovery->cancellable);
	if (discovery->debounce_source != 0) {
		g_source_remove(discovery->debounce_source);
		discovery->debounce_source = 0;
	}
	if (discovery->periodic_source != 0) {
		g_source_remove(discovery->periodic_source);
		discovery->periodic_source = 0;
	}
}

guint64
fibocom_discovery_request_scan(FibocomDiscovery *discovery, const gchar *reason)
{
	(void)reason;
	if (discovery->stopping)
		return 0;
	discovery->queued_scan_id = ++discovery->next_scan_id;
	schedule_debounced_scan(discovery, SCAN_DEBOUNCE_MS);
	return discovery->queued_scan_id;
}

const GPtrArray *
fibocom_discovery_devices(const FibocomDiscovery *discovery)
{
	return discovery->devices;
}

const FibocomDevice *
fibocom_discovery_find(const FibocomDiscovery *discovery, const gchar *device_id)
{
	guint i;

	for (i = 0; i < discovery->devices->len; i++) {
		const FibocomDevice *device = g_ptr_array_index(discovery->devices, i);

		if (g_str_equal(device->device_id, device_id))
			return device;
	}
	return NULL;
}

const FibocomReconcile *
fibocom_discovery_reconcile(const FibocomDiscovery *discovery)
{
	return &discovery->reconcile;
}

gboolean
fibocom_discovery_scan_in_progress(const FibocomDiscovery *discovery)
{
	return discovery->scan_running;
}

gboolean
fibocom_discovery_scan_pending(const FibocomDiscovery *discovery)
{
	return discovery->scan_pending || discovery->debounce_source != 0;
}

const FibocomProfile *
fibocom_discovery_profile(const FibocomDiscovery *discovery)
{
	return discovery->profile;
}
