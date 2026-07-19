/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "bridge.h"
#include "ubus_glib.h"

#include <glib-unix.h>
#include <signal.h>
#include <stdlib.h>

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

int
main(int argc, char **argv)
{
	gboolean foreground = FALSE;
	gboolean show_version = FALSE;
	gchar *ubus_socket = NULL;
	GOptionEntry options[] = {
		{ "foreground", 'f', 0, G_OPTION_ARG_NONE, &foreground,
		  "Stay in the foreground (required by procd)", NULL },
		{ "ubus-socket", 0, 0, G_OPTION_ARG_FILENAME, &ubus_socket,
		  "Alternate ubus Unix socket", "PATH" },
		{ "version", 'V', 0, G_OPTION_ARG_NONE, &show_version,
		  "Print version and exit", NULL },
		{ .long_name = NULL }
	};
	GOptionContext *option_context;
	FibocomBridge *bridge = NULL;
	FibocomUbus *ubus = NULL;
	SignalState signal_state = {};
	guint sigterm_source = 0;
	guint sigint_source = 0;
	g_autoptr(GError) error = NULL;
	int exit_status = EXIT_FAILURE;

	option_context = g_option_context_new("- read-only Fibocom ModemManager bridge");
	g_option_context_add_main_entries(option_context, options, NULL);
	if (!g_option_context_parse(option_context, &argc, &argv, &error)) {
		g_printerr("fibocom-mm-bridge: invalid command line\n");
		goto out;
	}
	if (argc != 1) {
		g_printerr("fibocom-mm-bridge: unexpected positional argument\n");
		goto out;
	}
	if (show_version) {
		g_print("fibocom-mm-bridge %s\n", FIBOCOM_MM_BRIDGE_VERSION);
		exit_status = EXIT_SUCCESS;
		goto out;
	}
	if (ubus_socket != NULL && !g_path_is_absolute(ubus_socket)) {
		g_printerr("fibocom-mm-bridge: ubus socket path must be absolute\n");
		goto out;
	}
	bridge = fibocom_bridge_new();
	ubus = fibocom_ubus_new(bridge, ubus_socket);
	if (bridge == NULL || ubus == NULL) {
		g_printerr("fibocom-mm-bridge: initialization failed\n");
		goto out;
	}
	signal(SIGPIPE, SIG_IGN);
	signal_state.loop = g_main_loop_new(NULL, FALSE);
	sigterm_source = g_unix_signal_add(SIGTERM, quit_signal_cb, &signal_state);
	sigint_source = g_unix_signal_add(SIGINT, quit_signal_cb, &signal_state);
	fibocom_bridge_start(bridge);
	fibocom_ubus_start(ubus);
	g_message("fibocom-mm-bridge %s started read-only%s",
		FIBOCOM_MM_BRIDGE_VERSION, foreground ? "" :
		" (foreground by design)");
	g_main_loop_run(signal_state.loop);
	exit_status = EXIT_SUCCESS;

out:
	if (sigterm_source != 0)
		g_source_remove(sigterm_source);
	if (sigint_source != 0)
		g_source_remove(sigint_source);
	fibocom_ubus_stop(ubus);
	fibocom_bridge_stop(bridge);
	if (signal_state.loop != NULL)
		g_main_loop_unref(signal_state.loop);
	fibocom_ubus_free(ubus);
	fibocom_bridge_free(bridge);
	g_option_context_free(option_context);
	g_free(ubus_socket);
	return exit_status;
}
