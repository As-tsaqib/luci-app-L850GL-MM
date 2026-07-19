/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "discovery.h"
#include "profile.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const gchar *production_profile_path;

typedef struct {
	GMainLoop *loop;
	guint callback_count;
	gboolean timed_out;
} ScanWait;

static gchar *
join2(const gchar *a, const gchar *b)
{
	return g_build_filename(a, b, NULL);
}

static gchar *
join3(const gchar *a, const gchar *b, const gchar *c)
{
	return g_build_filename(a, b, c, NULL);
}

static gchar *
join4(const gchar *a, const gchar *b, const gchar *c, const gchar *d)
{
	return g_build_filename(a, b, c, d, NULL);
}

static void
make_directory(const gchar *path)
{
	if (g_mkdir_with_parents(path, 0700) != 0)
		g_error("cannot create fixture directory %s: %s", path,
			g_strerror(errno));
	if (!g_file_test(path, G_FILE_TEST_IS_DIR))
		g_error("fixture path is not a directory: %s", path);
}

static void
write_text(const gchar *path, const gchar *contents)
{
	GError *error = NULL;

	if (!g_file_set_contents(path, contents, -1, &error))
		g_error("cannot write fixture %s: %s", path, error->message);
	g_clear_error(&error);
}

static void
make_link(const gchar *target, const gchar *link_path)
{
	if (symlink(target, link_path) != 0)
		g_error("cannot create fixture symlink %s -> %s: %s", link_path,
			target, g_strerror(errno));
}

static void
remove_tree(const gchar *path)
{
	struct stat status;

	if (lstat(path, &status) != 0) {
		if (errno == ENOENT)
			return;
		g_error("cannot inspect fixture path %s: %s", path,
			g_strerror(errno));
	}
	if (S_ISDIR(status.st_mode)) {
		GDir *directory = g_dir_open(path, 0, NULL);
		const gchar *name;

		if (directory == NULL)
			g_error("cannot open fixture directory %s", path);
		while ((name = g_dir_read_name(directory)) != NULL) {
			gchar *child = join2(path, name);

			remove_tree(child);
			g_free(child);
		}
		g_dir_close(directory);
		if (g_rmdir(path) != 0)
			g_error("cannot remove fixture directory %s: %s", path,
				g_strerror(errno));
	} else if (g_unlink(path) != 0) {
		g_error("cannot remove fixture path %s: %s", path,
			g_strerror(errno));
	}
}

static gchar *
usb_devices_path(const gchar *sysfs_root)
{
	return join4(sysfs_root, "bus", "usb", "devices");
}

static void
add_node(const gchar *interface_path, const gchar *kind, const gchar *name)
{
	gchar *container = join2(interface_path, kind);
	gchar *node = join2(container, name);

	make_directory(container);
	make_directory(node);
	g_free(node);
	g_free(container);
}

static void
add_interface(const gchar *sysfs_root, const gchar *slot,
	      const gchar *suffix, const gchar *number, const gchar *driver,
	      const gchar *tty, const gchar *wdm, const gchar *netdev)
{
	gchar *devices = usb_devices_path(sysfs_root);
	gchar *physical = join2(devices, slot);
	gchar *entry = g_strdup_printf("%s:%s", slot, suffix);
	gchar *interface_path = join2(physical, entry);
	gchar *attribute = join2(interface_path, "bInterfaceNumber");
	gchar *driver_link = join2(interface_path, "driver");
	gchar *global_link = join2(devices, entry);
	gchar *global_target = g_build_filename(slot, entry, NULL);

	make_directory(interface_path);
	write_text(attribute, number);
	make_link(driver, driver_link);
	make_link(global_target, global_link);
	if (tty != NULL)
		add_node(interface_path, "tty", tty);
	if (wdm != NULL)
		add_node(interface_path, "usbmisc", wdm);
	if (netdev != NULL)
		add_node(interface_path, "net", netdev);

	g_free(global_target);
	g_free(global_link);
	g_free(driver_link);
	g_free(attribute);
	g_free(interface_path);
	g_free(entry);
	g_free(physical);
	g_free(devices);
}

static void
add_physical_device(const gchar *sysfs_root, const gchar *slot,
		    const gchar *vid, const gchar *pid, const gchar *serial)
{
	gchar *devices = usb_devices_path(sysfs_root);
	gchar *physical = join2(devices, slot);
	gchar *vendor = join2(physical, "idVendor");
	gchar *product = join2(physical, "idProduct");
	gchar *serial_path = join2(physical, "serial");

	make_directory(physical);
	write_text(vendor, vid);
	write_text(product, pid);
	write_text(serial_path, serial);

	g_free(serial_path);
	g_free(product);
	g_free(vendor);
	g_free(physical);
	g_free(devices);
}

static void
add_prefix_collision_trap(const gchar *sysfs_root)
{
	gchar *devices = usb_devices_path(sysfs_root);
	gchar *trap_parent = join3(devices, "1-1", "traps");
	gchar *trap = join2(trap_parent, "prefix-collision-interface");
	gchar *number = join2(trap, "bInterfaceNumber");
	gchar *driver_link = join2(trap, "driver");
	gchar *global_link = join2(devices, "1-10:9.9");

	make_directory(trap);
	write_text(number, "0c");
	make_link("trap_driver", driver_link);
	make_link("1-1/traps/prefix-collision-interface", global_link);

	g_free(global_link);
	g_free(driver_link);
	g_free(number);
	g_free(trap);
	g_free(trap_parent);
	g_free(devices);
}

static gchar *
build_sysfs_fixture(void)
{
	GError *error = NULL;
	gchar *root = g_dir_make_tmp("fibocom-shadow-sysfs-XXXXXX", &error);
	gchar *devices;
	gchar *unsupported;
	gchar *unsupported_vendor;
	gchar *unsupported_product;

	if (root == NULL)
		g_error("cannot create temporary sysfs fixture: %s", error->message);
	g_clear_error(&error);
	devices = usb_devices_path(root);
	make_directory(devices);

	add_physical_device(root, "1-1", "2CB7\n", "0007\n",
			    "L850-MBIM-UNIT-0001\n");
	add_interface(root, "1-1", "1.0", "00\n", "cdc_mbim",
		      NULL, "cdc-wdm0", "wwan0");
	add_interface(root, "1-1", "1.2", "02\n", "cdc_acm",
		      "ttyACM0", NULL, NULL);
	add_interface(root, "1-1", "1.4", "04\n", "cdc_acm",
		      "ttyACM1", NULL, NULL);
	add_interface(root, "1-1", "1.6", "06\n", "cdc_acm",
		      "ttyACM2", NULL, NULL);

	add_physical_device(root, "1-10", "8087\n", "095A\n",
			    "L850-NCM-UNIT-0002\n");
	add_interface(root, "1-10", "1.0", "00\n", "cdc_acm",
		      "ttyACM3", NULL, NULL);
	add_interface(root, "1-10", "1.2", "02\n", "cdc_acm",
		      "ttyACM4", NULL, NULL);
	add_interface(root, "1-10", "1.4", "04\n", "cdc_acm",
		      "ttyACM5", NULL, NULL);
	add_interface(root, "1-10", "1.6", "06\n", "cdc_ncm",
		      NULL, NULL, "wwan1");
	add_interface(root, "1-10", "1.8", "08\n", "cdc_ncm",
		      NULL, NULL, "wwan2");
	add_interface(root, "1-10", "1.10", "0a\n", "cdc_ncm",
		      NULL, NULL, "wwan3");

	/*
	 * This entry starts with "1-1" but not with the required "1-1:".
	 * Its target is deliberately below 1-1, so path containment alone would
	 * not catch a broken textual-prefix check.
	 */
	add_prefix_collision_trap(root);

	unsupported = join2(devices, "2-1");
	unsupported_vendor = join2(unsupported, "idVendor");
	unsupported_product = join2(unsupported, "idProduct");
	make_directory(unsupported);
	write_text(unsupported_vendor, "ffff\n");
	write_text(unsupported_product, "ffff\n");

	g_free(unsupported_product);
	g_free(unsupported_vendor);
	g_free(unsupported);
	g_free(devices);
	return root;
}

static const FibocomDevice *
find_device_by_path(const FibocomDiscovery *discovery, const gchar *physical_path)
{
	const GPtrArray *devices = fibocom_discovery_devices(discovery);
	guint i;

	for (i = 0; i < devices->len; i++) {
		const FibocomDevice *device = g_ptr_array_index(devices, i);

		if (g_str_equal(device->physical_path, physical_path))
			return device;
	}
	return NULL;
}

static const FibocomInterface *
find_interface(const FibocomDevice *device, const gchar *number)
{
	guint i;

	for (i = 0; i < device->interfaces->len; i++) {
		const FibocomInterface *interface =
			g_ptr_array_index(device->interfaces, i);

		if (g_str_equal(interface->number, number))
			return interface;
	}
	return NULL;
}

static guint
count_interfaces(const FibocomDevice *device, guint interface_index)
{
	guint count = 0;
	guint i;

	for (i = 0; i < device->interfaces->len; i++) {
		const FibocomInterface *interface =
			g_ptr_array_index(device->interfaces, i);

		if (interface->index == interface_index)
			count++;
	}
	return count;
}

static gboolean
ports_have_name(const GPtrArray *ports, const gchar *name)
{
	guint i;

	for (i = 0; i < ports->len; i++) {
		const FibocomPort *port = g_ptr_array_index((GPtrArray *)ports, i);

		if (g_str_equal(port->name, name))
			return TRUE;
	}
	return FALSE;
}

static gboolean
device_has_port(const FibocomDevice *device, const gchar *name)
{
	guint i;

	for (i = 0; i < device->interfaces->len; i++) {
		const FibocomInterface *interface =
			g_ptr_array_index(device->interfaces, i);

		if (ports_have_name(interface->ttys, name) ||
		    ports_have_name(interface->wdms, name) ||
		    ports_have_name(interface->netdevs, name))
			return TRUE;
	}
	return FALSE;
}

static void
scan_changed(FibocomDiscovery *discovery, const FibocomReconcile *reconcile,
	     gpointer user_data)
{
	ScanWait *wait = user_data;

	(void)discovery;
	(void)reconcile;
	wait->callback_count++;
	g_main_loop_quit(wait->loop);
}

static gboolean
scan_timeout(gpointer user_data)
{
	ScanWait *wait = user_data;

	wait->timed_out = TRUE;
	g_main_loop_quit(wait->loop);
	return G_SOURCE_REMOVE;
}

static guint64
run_scan(FibocomDiscovery *discovery, ScanWait *wait, const gchar *reason)
{
	guint timeout_id;
	guint64 request_id;
	guint callbacks_before = wait->callback_count;

	wait->timed_out = FALSE;
	request_id = fibocom_discovery_request_scan(discovery, reason);
	g_assert_cmpuint(request_id, >, 0);
	timeout_id = g_timeout_add_seconds(5, scan_timeout, wait);
	g_main_loop_run(wait->loop);
	if (!wait->timed_out)
		g_source_remove(timeout_id);
	g_assert_false(wait->timed_out);
	g_assert_cmpuint(wait->callback_count, ==, callbacks_before + 1);
	g_assert_false(fibocom_discovery_scan_in_progress(discovery));
	g_assert_false(fibocom_discovery_scan_pending(discovery));
	g_assert_cmpuint(fibocom_discovery_reconcile(discovery)->scan_id, ==,
			 request_id);
	return request_id;
}

static gchar *
expected_serial_device_id(const gchar *serial)
{
	gchar *material = g_strdup_printf("fibocom-l850:v1:serial:%s", serial);
	gchar *hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, material, -1);
	gchar *device_id = g_strdup_printf("l850-%s", hash);

	g_free(hash);
	g_free(material);
	return device_id;
}

static void
assert_port(const FibocomInterface *interface, const GPtrArray *ports,
	    const gchar *name)
{
	const FibocomPort *port;

	g_assert_nonnull(interface);
	g_assert_cmpuint(ports->len, ==, 1);
	port = g_ptr_array_index((GPtrArray *)ports, 0);
	g_assert_cmpstr(port->name, ==, name);
	g_assert_cmpstr(port->interface_number, ==, interface->number);
	g_assert_cmpuint(port->interface_index, ==, interface->index);
	g_assert_cmpstr(port->driver, ==, interface->driver);
}

static void
test_profile_accepts_only_reviewed_contract(void)
{
	FibocomProfile *profile;
	const FibocomUsbMatch *match;
	const FibocomPortMap *map;
	GError *error = NULL;
	gchar *contents = NULL;
	gsize contents_length = 0;
	gchar *temporary;
	gchar *bad_path;
	gchar *bad_contents;
	gchar *needle;
	gchar *link_path;
	gchar *nul_path;
	gchar *nul_contents;
	gsize prefix_length;

	profile = fibocom_profile_load(production_profile_path, &error);
	g_assert_no_error(error);
	g_assert_nonnull(profile);
	g_assert_cmpstr(fibocom_profile_id(profile), ==, "fibocom-l850-gl");
	g_assert_cmpstr(fibocom_profile_display_name(profile), ==,
			"Fibocom L850-GL");
	match = fibocom_profile_match_usb(profile, "2cb7", "0007");
	g_assert_nonnull(match);
	g_assert_cmpint(match->composition, ==, FIBOCOM_COMPOSITION_MBIM);
	match = fibocom_profile_match_usb(profile, "8087", "095a");
	g_assert_nonnull(match);
	g_assert_cmpint(match->composition, ==, FIBOCOM_COMPOSITION_NCM);
	g_assert_null(fibocom_profile_match_usb(profile, "ffff", "ffff"));
	map = fibocom_profile_port_map(profile, FIBOCOM_COMPOSITION_NCM);
	g_assert_cmpuint(map->at_primary, ==, 0x00);
	g_assert_cmpuint(map->at_secondary, ==, 0x04);
	g_assert_cmpuint(map->data_candidates->len, ==, 3);
	g_assert_cmpuint(g_array_index(map->data_candidates, guint, 0), ==, 0x06);
	g_assert_cmpuint(g_array_index(map->data_candidates, guint, 1), ==, 0x08);
	g_assert_cmpuint(g_array_index(map->data_candidates, guint, 2), ==, 0x0a);
	fibocom_profile_unref(profile);

	g_assert_true(g_file_get_contents(production_profile_path, &contents,
					&contents_length, &error));
	g_assert_no_error(error);
	temporary = g_dir_make_tmp("fibocom-profile-test-XXXXXX", &error);
	g_assert_no_error(error);
	g_assert_nonnull(temporary);
	bad_path = join2(temporary, "uppercase-interface.json");
	bad_contents = g_memdup2(contents, contents_length + 1);
	needle = g_strstr_len(bad_contents, (gssize)contents_length, "\"0a\"");
	g_assert_nonnull(needle);
	needle[2] = 'A';
	write_text(bad_path, bad_contents);
	profile = fibocom_profile_load(bad_path, &error);
	g_assert_null(profile);
	g_assert_error(error, g_quark_from_static_string("fibocom-profile-error"), 1);
	g_clear_error(&error);

	needle = g_strstr_len(contents, (gssize)contents_length,
			      "Fibocom L850-GL");
	g_assert_nonnull(needle);
	prefix_length = (gsize)(needle - contents);
	nul_contents = g_strdup_printf("%.*sFibocom L850-GL\\u0000suffix%s",
				       (int)prefix_length, contents,
				       needle + strlen("Fibocom L850-GL"));
	nul_path = join2(temporary, "embedded-nul.json");
	write_text(nul_path, nul_contents);
	profile = fibocom_profile_load(nul_path, &error);
	g_assert_null(profile);
	g_assert_error(error, g_quark_from_static_string("fibocom-profile-error"), 1);
	g_clear_error(&error);

	link_path = join2(temporary, "profile-symlink.json");
	make_link(production_profile_path, link_path);
	profile = fibocom_profile_load(link_path, &error);
	g_assert_null(profile);
	g_assert_nonnull(error);
	g_clear_error(&error);

	g_free(link_path);
	g_free(nul_path);
	g_free(nul_contents);
	g_free(bad_contents);
	g_free(bad_path);
	g_free(contents);
	remove_tree(temporary);
	g_free(temporary);
}

static void
test_async_mbim_ncm_discovery_and_generation(void)
{
	GError *error = NULL;
	FibocomProfile *profile = fibocom_profile_load(production_profile_path,
						       &error);
	gchar *sysfs_root;
	FibocomDiscovery *discovery;
	ScanWait wait = { 0 };
	const FibocomReconcile *reconcile;
	const FibocomDevice *mbim;
	const FibocomDevice *ncm;
	const FibocomInterface *interface;
	gchar *expected_mbim_id;
	gchar *expected_ncm_id;
	gchar *stable_mbim_id;
	guint64 initial_mbim_generation;
	guint64 changed_mbim_generation;
	guint64 initial_ncm_generation;
	gchar *devices;
	gchar *mbim_physical;
	gchar *detached;
	gchar *old_netdev;
	gchar *new_netdev;

	g_assert_no_error(error);
	g_assert_nonnull(profile);
	sysfs_root = build_sysfs_fixture();
	discovery = fibocom_discovery_new(sysfs_root, profile);
	g_assert_nonnull(discovery);
	fibocom_profile_unref(profile);
	wait.loop = g_main_loop_new(NULL, FALSE);
	fibocom_discovery_set_changed_callback(discovery, scan_changed, &wait);

	run_scan(discovery, &wait, "fixture-initial");
	reconcile = fibocom_discovery_reconcile(discovery);
	g_assert_null(reconcile->last_error);
	g_assert_cmpuint(reconcile->device_count, ==, 2);
	g_assert_cmpuint(reconcile->added, ==, 2);
	g_assert_cmpuint(reconcile->removed, ==, 0);
	g_assert_cmpuint(reconcile->changed, ==, 0);
	g_assert_cmpuint(reconcile->unchanged, ==, 0);

	mbim = find_device_by_path(discovery, "1-1");
	ncm = find_device_by_path(discovery, "1-10");
	g_assert_nonnull(mbim);
	g_assert_nonnull(ncm);
	g_assert_cmpstr(mbim->composition, ==, "mbim");
	g_assert_cmpstr(ncm->composition, ==, "ncm");
	g_assert_cmpstr(mbim->vid, ==, "2cb7");
	g_assert_cmpstr(mbim->pid, ==, "0007");
	g_assert_cmpstr(ncm->vid, ==, "8087");
	g_assert_cmpstr(ncm->pid, ==, "095a");
	g_assert_cmpstr(mbim->identity_scope, ==, "usb-serial-hash");
	g_assert_cmpstr(ncm->identity_scope, ==, "usb-serial-hash");
	g_assert_true(mbim->present);
	g_assert_true(ncm->present);
	expected_mbim_id = expected_serial_device_id("L850-MBIM-UNIT-0001");
	expected_ncm_id = expected_serial_device_id("L850-NCM-UNIT-0002");
	g_assert_cmpstr(mbim->device_id, ==, expected_mbim_id);
	g_assert_cmpstr(ncm->device_id, ==, expected_ncm_id);
	g_assert_true(fibocom_discovery_find(discovery, expected_mbim_id) == mbim);
	g_assert_true(fibocom_discovery_find(discovery, expected_ncm_id) == ncm);
	g_assert_cmpuint(mbim->generation, >, 0);
	g_assert_cmpuint(ncm->generation, >, 0);
	g_assert_cmpuint(mbim->generation, !=, ncm->generation);
	initial_mbim_generation = mbim->generation;
	initial_ncm_generation = ncm->generation;
	stable_mbim_id = g_strdup(mbim->device_id);

	/* Exact grouping and complete MBIM topology. */
	g_assert_cmpuint(mbim->interfaces->len, ==, 4);
	g_assert_cmpint(mbim->topology_status, ==, FIBOCOM_TOPOLOGY_COMPLETE);
	g_assert_cmpstr(mbim->topology_reason, ==, "exact-profile-match");
	g_assert_null(find_interface(mbim, "0c"));
	interface = find_interface(mbim, "00");
	g_assert_nonnull(interface);
	assert_port(interface, interface->wdms, "cdc-wdm0");
	assert_port(interface, interface->netdevs, "wwan0");
	interface = find_interface(mbim, "02");
	g_assert_cmpstr(interface->role, ==, "at-primary");
	assert_port(interface, interface->ttys, "ttyACM0");
	interface = find_interface(mbim, "04");
	g_assert_cmpstr(interface->role, ==, "ignored");
	assert_port(interface, interface->ttys, "ttyACM1");
	interface = find_interface(mbim, "06");
	g_assert_cmpstr(interface->role, ==, "at-secondary");
	assert_port(interface, interface->ttys, "ttyACM2");
	g_assert_false(device_has_port(mbim, "ttyACM3"));
	g_assert_false(device_has_port(mbim, "wwan1"));

	/* Exact lowercase-hex NCM candidates 06/08/0a and their ownership. */
	g_assert_cmpuint(ncm->interfaces->len, ==, 6);
	g_assert_cmpint(ncm->topology_status, ==, FIBOCOM_TOPOLOGY_COMPLETE);
	g_assert_cmpstr(ncm->topology_reason, ==, "exact-profile-match");
	interface = find_interface(ncm, "00");
	g_assert_cmpstr(interface->role, ==, "at-primary");
	assert_port(interface, interface->ttys, "ttyACM3");
	interface = find_interface(ncm, "02");
	g_assert_cmpstr(interface->role, ==, "ignored");
	assert_port(interface, interface->ttys, "ttyACM4");
	interface = find_interface(ncm, "04");
	g_assert_cmpstr(interface->role, ==, "at-secondary");
	assert_port(interface, interface->ttys, "ttyACM5");
	interface = find_interface(ncm, "06");
	g_assert_cmpstr(interface->role, ==, "ncm-data-candidate");
	assert_port(interface, interface->netdevs, "wwan1");
	interface = find_interface(ncm, "08");
	g_assert_cmpstr(interface->role, ==, "ncm-data-candidate");
	assert_port(interface, interface->netdevs, "wwan2");
	interface = find_interface(ncm, "0a");
	g_assert_cmpstr(interface->role, ==, "ncm-data-candidate");
	assert_port(interface, interface->netdevs, "wwan3");
	g_assert_false(device_has_port(ncm, "ttyACM0"));
	g_assert_false(device_has_port(ncm, "cdc-wdm0"));

	/* An identical scan retains each generation and reports unchanged. */
	run_scan(discovery, &wait, "fixture-unchanged");
	reconcile = fibocom_discovery_reconcile(discovery);
	g_assert_cmpuint(reconcile->added, ==, 0);
	g_assert_cmpuint(reconcile->removed, ==, 0);
	g_assert_cmpuint(reconcile->changed, ==, 0);
	g_assert_cmpuint(reconcile->unchanged, ==, 2);
	mbim = find_device_by_path(discovery, "1-1");
	ncm = find_device_by_path(discovery, "1-10");
	g_assert_cmpuint(mbim->generation, ==, initial_mbim_generation);
	g_assert_cmpuint(ncm->generation, ==, initial_ncm_generation);

	/* A topology fingerprint change advances only the affected generation. */
	devices = usb_devices_path(sysfs_root);
	old_netdev = join4(devices, "1-1", "1-1:1.0", "net/wwan0");
	new_netdev = join4(devices, "1-1", "1-1:1.0", "net/wwan9");
	g_assert_cmpint(g_rename(old_netdev, new_netdev), ==, 0);
	run_scan(discovery, &wait, "fixture-port-change");
	reconcile = fibocom_discovery_reconcile(discovery);
	g_assert_cmpuint(reconcile->added, ==, 0);
	g_assert_cmpuint(reconcile->removed, ==, 0);
	g_assert_cmpuint(reconcile->changed, ==, 1);
	g_assert_cmpuint(reconcile->unchanged, ==, 1);
	mbim = find_device_by_path(discovery, "1-1");
	ncm = find_device_by_path(discovery, "1-10");
	g_assert_cmpstr(mbim->device_id, ==, stable_mbim_id);
	g_assert_cmpuint(mbim->generation, >, initial_mbim_generation);
	g_assert_cmpuint(ncm->generation, ==, initial_ncm_generation);
	changed_mbim_generation = mbim->generation;
	interface = find_interface(mbim, "00");
	assert_port(interface, interface->netdevs, "wwan9");
	g_assert_cmpint(mbim->topology_status, ==, FIBOCOM_TOPOLOGY_COMPLETE);

	/* Unplug/replug is reconciled without changing the serial-derived ID. */
	mbim_physical = join2(devices, "1-1");
	detached = join2(sysfs_root, "detached-1-1");
	g_assert_cmpint(g_rename(mbim_physical, detached), ==, 0);
	run_scan(discovery, &wait, "fixture-unplug");
	reconcile = fibocom_discovery_reconcile(discovery);
	g_assert_cmpuint(reconcile->device_count, ==, 1);
	g_assert_cmpuint(reconcile->removed, ==, 1);
	g_assert_null(find_device_by_path(discovery, "1-1"));
	g_assert_nonnull(find_device_by_path(discovery, "1-10"));

	g_assert_cmpint(g_rename(detached, mbim_physical), ==, 0);
	run_scan(discovery, &wait, "fixture-replug");
	reconcile = fibocom_discovery_reconcile(discovery);
	g_assert_cmpuint(reconcile->device_count, ==, 2);
	g_assert_cmpuint(reconcile->added, ==, 1);
	g_assert_cmpuint(reconcile->removed, ==, 0);
	mbim = find_device_by_path(discovery, "1-1");
	g_assert_nonnull(mbim);
	g_assert_cmpstr(mbim->device_id, ==, stable_mbim_id);
	g_assert_cmpuint(mbim->generation, >, changed_mbim_generation);
	g_assert_cmpint(mbim->topology_status, ==, FIBOCOM_TOPOLOGY_COMPLETE);

	fibocom_discovery_stop(discovery);
	fibocom_discovery_unref(discovery);
	g_main_loop_unref(wait.loop);
	g_free(detached);
	g_free(mbim_physical);
	g_free(new_netdev);
	g_free(old_netdev);
	g_free(devices);
	g_free(stable_mbim_id);
	g_free(expected_ncm_id);
	g_free(expected_mbim_id);
	remove_tree(sysfs_root);
	g_free(sysfs_root);
}

static void
test_mbim_split_parent_is_ambiguous(void)
{
	GError *error = NULL;
	FibocomProfile *profile = fibocom_profile_load(production_profile_path,
						       &error);
	gchar *sysfs_root;
	gchar *devices;
	gchar *original_net;
	FibocomDiscovery *discovery;
	ScanWait wait = { 0 };
	const FibocomDevice *mbim;
	const FibocomInterface *wdm_parent = NULL;
	const FibocomInterface *net_parent = NULL;
	guint i;

	g_assert_no_error(error);
	g_assert_nonnull(profile);
	sysfs_root = build_sysfs_fixture();
	devices = usb_devices_path(sysfs_root);
	original_net = join4(devices, "1-1", "1-1:1.0", "net");
	remove_tree(original_net);
	add_interface(sysfs_root, "1-1", "1.1", "00\n", "cdc_mbim",
		      NULL, NULL, "wwan-split");

	discovery = fibocom_discovery_new(sysfs_root, profile);
	g_assert_nonnull(discovery);
	fibocom_profile_unref(profile);
	wait.loop = g_main_loop_new(NULL, FALSE);
	fibocom_discovery_set_changed_callback(discovery, scan_changed, &wait);
	run_scan(discovery, &wait, "fixture-mbim-split-parent");

	mbim = find_device_by_path(discovery, "1-1");
	g_assert_nonnull(mbim);
	g_assert_cmpuint(count_interfaces(mbim, 0x00), ==, 2);
	for (i = 0; i < mbim->interfaces->len; i++) {
		const FibocomInterface *interface =
			g_ptr_array_index(mbim->interfaces, i);

		if (interface->index != 0x00)
			continue;
		if (interface->wdms->len == 1)
			wdm_parent = interface;
		if (interface->netdevs->len == 1)
			net_parent = interface;
	}
	g_assert_nonnull(wdm_parent);
	g_assert_nonnull(net_parent);
	g_assert_true(wdm_parent != net_parent);
	g_assert_cmpstr(wdm_parent->sysfs_path, !=, net_parent->sysfs_path);
	assert_port(wdm_parent, wdm_parent->wdms, "cdc-wdm0");
	assert_port(net_parent, net_parent->netdevs, "wwan-split");
	g_assert_cmpint(mbim->topology_status, ==, FIBOCOM_TOPOLOGY_AMBIGUOUS);
	g_assert_cmpstr(mbim->topology_reason, ==, "mbim-parent-mismatch");

	fibocom_discovery_stop(discovery);
	fibocom_discovery_unref(discovery);
	g_main_loop_unref(wait.loop);
	g_free(original_net);
	g_free(devices);
	remove_tree(sysfs_root);
	g_free(sysfs_root);
}

static void
test_mbim_wrong_data_interface_is_partial(void)
{
	GError *error = NULL;
	FibocomProfile *profile = fibocom_profile_load(production_profile_path,
						       &error);
	gchar *sysfs_root;
	gchar *devices;
	gchar *interface_number;
	FibocomDiscovery *discovery;
	ScanWait wait = { 0 };
	const FibocomDevice *mbim;
	const FibocomInterface *data_interface;

	g_assert_no_error(error);
	g_assert_nonnull(profile);
	sysfs_root = build_sysfs_fixture();
	devices = usb_devices_path(sysfs_root);
	interface_number = join4(devices, "1-1", "1-1:1.0",
				 "bInterfaceNumber");
	write_text(interface_number, "01\n");

	discovery = fibocom_discovery_new(sysfs_root, profile);
	g_assert_nonnull(discovery);
	fibocom_profile_unref(profile);
	wait.loop = g_main_loop_new(NULL, FALSE);
	fibocom_discovery_set_changed_callback(discovery, scan_changed, &wait);
	run_scan(discovery, &wait, "fixture-mbim-wrong-data-interface");

	mbim = find_device_by_path(discovery, "1-1");
	g_assert_nonnull(mbim);
	g_assert_cmpuint(count_interfaces(mbim, 0x00), ==, 0);
	g_assert_cmpuint(count_interfaces(mbim, 0x01), ==, 1);
	data_interface = find_interface(mbim, "01");
	g_assert_nonnull(data_interface);
	g_assert_cmpstr(data_interface->driver, ==, "cdc_mbim");
	assert_port(data_interface, data_interface->wdms, "cdc-wdm0");
	assert_port(data_interface, data_interface->netdevs, "wwan0");
	g_assert_cmpint(mbim->topology_status, ==, FIBOCOM_TOPOLOGY_PARTIAL);
	g_assert_cmpstr(mbim->topology_reason, ==,
			"wrong-mbim-data-interface");

	fibocom_discovery_stop(discovery);
	fibocom_discovery_unref(discovery);
	g_main_loop_unref(wait.loop);
	g_free(interface_number);
	g_free(devices);
	remove_tree(sysfs_root);
	g_free(sysfs_root);
}

static void
test_wrong_driver_never_reports_complete(void)
{
	GError *error = NULL;
	FibocomProfile *profile = fibocom_profile_load(production_profile_path,
						       &error);
	gchar *sysfs_root;
	gchar *devices;
	gchar *mbim_driver;
	gchar *ncm_driver;
	FibocomDiscovery *discovery;
	ScanWait wait = { 0 };
	const FibocomDevice *mbim;
	const FibocomDevice *ncm;

	g_assert_no_error(error);
	g_assert_nonnull(profile);
	sysfs_root = build_sysfs_fixture();
	devices = usb_devices_path(sysfs_root);
	mbim_driver = join4(devices, "1-1", "1-1:1.0", "driver");
	ncm_driver = join4(devices, "1-10", "1-10:1.8", "driver");
	g_assert_cmpint(g_unlink(mbim_driver), ==, 0);
	make_link("qmi_wwan", mbim_driver);
	g_assert_cmpint(g_unlink(ncm_driver), ==, 0);
	make_link("rndis_host", ncm_driver);

	discovery = fibocom_discovery_new(sysfs_root, profile);
	g_assert_nonnull(discovery);
	fibocom_profile_unref(profile);
	wait.loop = g_main_loop_new(NULL, FALSE);
	fibocom_discovery_set_changed_callback(discovery, scan_changed, &wait);
	run_scan(discovery, &wait, "fixture-wrong-driver");

	mbim = find_device_by_path(discovery, "1-1");
	ncm = find_device_by_path(discovery, "1-10");
	g_assert_nonnull(mbim);
	g_assert_nonnull(ncm);
	g_assert_cmpint(mbim->topology_status, ==, FIBOCOM_TOPOLOGY_PARTIAL);
	g_assert_cmpstr(mbim->topology_reason, ==, "driver-mismatch");
	g_assert_cmpint(ncm->topology_status, ==, FIBOCOM_TOPOLOGY_PARTIAL);
	g_assert_cmpstr(ncm->topology_reason, ==, "driver-mismatch");

	fibocom_discovery_stop(discovery);
	fibocom_discovery_unref(discovery);
	g_main_loop_unref(wait.loop);
	g_free(ncm_driver);
	g_free(mbim_driver);
	g_free(devices);
	remove_tree(sysfs_root);
	g_free(sysfs_root);
}

int
main(int argc, char **argv)
{
	if (argc != 2) {
		g_printerr("usage: %s /path/to/l850-gl.json\n", argv[0]);
		return 2;
	}
	production_profile_path = argv[1];
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fibocom/profile/reviewed-contract",
			test_profile_accepts_only_reviewed_contract);
	g_test_add_func("/fibocom/discovery/mbim-ncm-generation",
			test_async_mbim_ncm_discovery_and_generation);
	g_test_add_func("/fibocom/discovery/mbim-split-parent-ambiguous",
			test_mbim_split_parent_is_ambiguous);
	g_test_add_func("/fibocom/discovery/mbim-wrong-data-interface",
			test_mbim_wrong_data_interface_is_partial);
	g_test_add_func("/fibocom/discovery/wrong-driver-fails-closed",
			test_wrong_driver_never_reports_complete);
	return g_test_run();
}
