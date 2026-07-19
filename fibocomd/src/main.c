/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "discovery.h"
#include "profile.h"
#include "ubus_glib.h"

#include <glib-unix.h>
#include <signal.h>
#include <stdlib.h>

#define FIBOCOMD_VERSION "0.1.0-shadow"

typedef struct {
	GMainLoop *loop;
} SignalState;

static gboolean
quit_signal_cb(gpointer user_data)
{
	SignalState *state = user_data;

	g_main_loop_quit(state->loop);
	return G_SOURCE_CONTINUE;
}

static void
inventory_changed(FibocomDiscovery *discovery,
		  const FibocomReconcile *reconcile, gpointer user_data)
{
	(void)discovery;
	(void)user_data;
	if (reconcile->last_error != NULL) {
		g_warning("shadow reconcile scan=%" G_GUINT64_FORMAT " failed: %s",
			  reconcile->scan_id, reconcile->last_error);
		return;
	}
	g_message("shadow reconcile scan=%" G_GUINT64_FORMAT
		  " devices=%u added=%u removed=%u changed=%u unchanged=%u",
		  reconcile->scan_id, reconcile->device_count, reconcile->added,
		  reconcile->removed, reconcile->changed, reconcile->unchanged);
}

int
main(int argc, char **argv)
{
	gboolean foreground = FALSE;
	gboolean shadow = FALSE;
	gboolean show_version = FALSE;
	gchar *sysfs_root = NULL;
	gchar *profile_path = NULL;
	gchar *ubus_socket = NULL;
	GOptionEntry options[] = {
		{ "foreground", 'f', 0, G_OPTION_ARG_NONE, &foreground,
		  "Stay in the foreground (required by procd)", NULL },
		{ "shadow", 0, 0, G_OPTION_ARG_NONE, &shadow,
		  "Enable strictly read-only shadow mode", NULL },
		{ "sysfs-root", 0, 0, G_OPTION_ARG_FILENAME, &sysfs_root,
		  "Alternate sysfs root for fixture tests", "PATH" },
		{ "profile", 0, 0, G_OPTION_ARG_FILENAME, &profile_path,
		  "Alternate L850 profile for fixture tests", "PATH" },
		{ "ubus-socket", 0, 0, G_OPTION_ARG_FILENAME, &ubus_socket,
		  "Alternate ubus Unix socket", "PATH" },
		{ "version", 'V', 0, G_OPTION_ARG_NONE, &show_version,
		  "Print version and exit", NULL },
		{ .long_name = NULL }
	};
	GOptionContext *option_context;
	FibocomProfile *profile = NULL;
	FibocomDiscovery *discovery = NULL;
	FibocomUbus *ubus = NULL;
	SignalState signal_state = {};
	guint sigterm_source = 0;
	guint sigint_source = 0;
	GError *error = NULL;
	int exit_status = EXIT_FAILURE;

	option_context = g_option_context_new("- Fibocom L850 shadow inventory daemon");
	g_option_context_add_main_entries(option_context, options, NULL);
	if (!g_option_context_parse(option_context, &argc, &argv, &error)) {
		g_printerr("fibocomd: %s\n", error->message);
		g_clear_error(&error);
		goto out;
	}
	if (argc != 1) {
		g_printerr("fibocomd: unexpected positional argument\n");
		goto out;
	}
	if (show_version) {
		g_print("fibocomd %s\n", FIBOCOMD_VERSION);
		exit_status = EXIT_SUCCESS;
		goto out;
	}
	if (sysfs_root == NULL)
		sysfs_root = g_strdup(FIBOCOM_SYSFS_ROOT);
	if (profile_path == NULL)
		profile_path = g_strdup(FIBOCOM_PROFILE_PATH);
	if (!shadow) {
		g_printerr("fibocomd: this build only supports explicit --shadow mode\n");
		goto out;
	}
	if (!g_path_is_absolute(sysfs_root) ||
	    !g_file_test(sysfs_root, G_FILE_TEST_IS_DIR)) {
		g_printerr("fibocomd: sysfs root must be an existing absolute directory\n");
		goto out;
	}
	if (!g_path_is_absolute(profile_path)) {
		g_printerr("fibocomd: profile path must be absolute\n");
		goto out;
	}
	if (ubus_socket != NULL && !g_path_is_absolute(ubus_socket)) {
		g_printerr("fibocomd: ubus socket path must be absolute\n");
		goto out;
	}

	profile = fibocom_profile_load(profile_path, &error);
	if (profile == NULL) {
		g_printerr("fibocomd: %s\n", error->message);
		g_clear_error(&error);
		goto out;
	}
	discovery = fibocom_discovery_new(sysfs_root, profile);
	if (discovery == NULL) {
		g_printerr("fibocomd: cannot initialize discovery\n");
		goto out;
	}
	fibocom_discovery_set_changed_callback(discovery, inventory_changed, NULL);
	ubus = fibocom_ubus_new(discovery, ubus_socket);
	if (ubus == NULL) {
		g_printerr("fibocomd: cannot initialize ubus adapter\n");
		goto out;
	}

	signal(SIGPIPE, SIG_IGN);
	signal_state.loop = g_main_loop_new(NULL, FALSE);
	sigterm_source = g_unix_signal_add(SIGTERM, quit_signal_cb, &signal_state);
	sigint_source = g_unix_signal_add(SIGINT, quit_signal_cb, &signal_state);
	fibocom_discovery_start(discovery);
	fibocom_ubus_start(ubus);
	g_message("fibocomd %s started in read-only shadow mode%s",
		  FIBOCOMD_VERSION, foreground ? "" : " (foreground by design)");
	g_main_loop_run(signal_state.loop);
	exit_status = EXIT_SUCCESS;

out:
	if (sigterm_source != 0)
		g_source_remove(sigterm_source);
	if (sigint_source != 0)
		g_source_remove(sigint_source);
	if (ubus != NULL)
		fibocom_ubus_stop(ubus);
	if (discovery != NULL)
		fibocom_discovery_stop(discovery);
	if (signal_state.loop != NULL)
		g_main_loop_unref(signal_state.loop);
	fibocom_ubus_free(ubus);
	fibocom_discovery_unref(discovery);
	fibocom_profile_unref(profile);
	g_option_context_free(option_context);
	g_free(sysfs_root);
	g_free(profile_path);
	g_free(ubus_socket);
	return exit_status;
}
