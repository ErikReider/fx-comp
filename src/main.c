#define _POSIX_C_SOURCE 200809L

#include <getopt.h>
#include <pthread.h>
#include <scenefx/render/fx_renderer/fx_renderer.h>
#include <scenefx/types/wlr_scene.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/backend/multi.h>
#include <wlr/backend/wayland.h>
#include <wlr/backend/x11.h>
#include <wlr/config.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "comp/animation_mgr.h"
#include "comp/lock.h"
#include "comp/output.h"
#include "comp/server.h"
#include "constants.h"
#include "desktop/layer_shell.h"
#include "desktop/xdg.h"
#include "desktop/xdg_decoration.h"
#include "seat/cursor.h"
#include "seat/seat.h"
#include "util.h"

struct comp_server server = {0};

static void print_help(void) {
	printf("Usage:\n");
	printf("\t-s <cmd>\tStartup command\n");
	printf("\t-l <DEBUG|INFO>\tLog level\n");
	printf("\t-D <log-txn-timings>\tLog level\n");
	printf("\t-o <int>\tNumber of additional testing outputs\n");
}

static void create_output(struct wlr_backend *backend, void *data) {
	bool *done = data;
	if (*done) {
		return;
	}

	if (wlr_backend_is_wl(backend)) {
		wlr_wl_output_create(backend);
		*done = true;
	} else if (wlr_backend_is_headless(backend)) {
		wlr_headless_add_output(backend, 1920, 1080);
		*done = true;
	}
#if WLR_HAS_X11_BACKEND
	else if (wlr_backend_is_x11(backend)) {
		wlr_x11_output_create(backend);
		*done = true;
	}
#endif
}

void comp_create_extra_output(void) {
	bool done = false;
	wlr_multi_for_each_backend(server.backend, create_output, &done);
	if (!done) {
		wlr_log(WLR_ERROR, "Could not create virtual output for backend!");
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.wl_display);
		exit(1);
	}
}

/** Initialize GTK */
static void *init_gtk(void *attr) {
	wlr_log(WLR_INFO, "Initializing GTK");
	if (!gtk_init_check(NULL, NULL)) {
		wlr_log(WLR_ERROR, "Failed to initialize GTK");
	}

	return NULL;
}

int main(int argc, char *argv[]) {
	char *startup_cmd = NULL;
	enum wlr_log_importance log_importance = WLR_ERROR;
	int num_test_outputs = 1;

	int c;
	while ((c = getopt(argc, argv, "s:o:l:D:h")) != -1) {
		switch (c) {
		case 's':
			startup_cmd = optarg;
			break;
		case 'l':
			if (strcmp(optarg, "DEBUG") == 0) {
				log_importance = WLR_DEBUG;
			} else if (strcmp(optarg, "INFO") == 0) {
				log_importance = WLR_INFO;
			}
			break;
		case 'D':
			if (strcmp(optarg, "log-txn-timings") == 0) {
				server.debug.log_txn_timings = true;
			}
			break;
		case 'o':;
			char *endptr;
			long extra_outputs = strtol(optarg, &endptr, 10);
			if (endptr == optarg) {
				fprintf(stderr, "Could not parse number\n");
				return 1;
			}
			if (extra_outputs < 1) {
				fprintf(stderr, "Additional outputs has to be larger than 0\n");
				return 1;
			}
			num_test_outputs += extra_outputs;
			break;
		default:
			print_help();
			return 0;
		}
	}
	if (optind < argc) {
		print_help();
		return 0;
	}

	wlr_log_init(log_importance, NULL);

	/* The Wayland display is managed by libwayland. It handles accepting
	 * clients from the Unix socket, manging Wayland globals, and so on. */
	server.wl_display = wl_display_create();

	server.wl_event_loop = wl_display_get_event_loop(server.wl_display);
	// Initialize animation manager
	server.animation_mgr = comp_animation_mgr_init();

	// Transactions
	wl_list_init(&server.dirty_objects);

	/* The backend is a wlroots feature which abstracts the underlying input and
	 * output hardware. The autocreate option will choose the most suitable
	 * backend based on the current environment, such as opening an X11 window
	 * if an X11 server is running. */
	// TODO: Use wlr_session?
	server.backend = wlr_backend_autocreate(server.wl_display, NULL);
	if (server.backend == NULL) {
		wlr_log(WLR_ERROR, "failed to create wlr_backend");
		return 1;
	}

	// Create headless backend
	server.headless_backend = wlr_headless_backend_create(server.wl_display);
	if (server.headless_backend == NULL) {
		wlr_log(WLR_ERROR, "Failed to create headless backend");
		wlr_backend_destroy(server.backend);
		return 1;
	} else {
		wlr_multi_backend_add(server.backend, server.headless_backend);
	}

	/* Autocreates a renderer, either Pixman, GLES2 or Vulkan for us. The user
	 * can also specify a renderer using the WLR_RENDERER env var.
	 * The renderer is responsible for defining the various pixel formats it
	 * supports for shared memory, this configures that for clients. */
	server.renderer = fx_renderer_create(server.backend);
	if (server.renderer == NULL) {
		wlr_log(WLR_ERROR, "failed to create fx_renderer");
		return 1;
	}

	wlr_renderer_init_wl_display(server.renderer, server.wl_display);

	/* Autocreates an allocator for us.
	 * The allocator is the bridge between the renderer and the backend. It
	 * handles the buffer creation, allowing wlroots to render onto the
	 * screen */
	server.allocator =
		wlr_allocator_autocreate(server.backend, server.renderer);
	if (server.allocator == NULL) {
		wlr_log(WLR_ERROR, "failed to create wlr_allocator");
		return 1;
	}

	/* This creates some hands-off wlroots interfaces. The compositor is
	 * necessary for clients to allocate surfaces, the subcompositor allows to
	 * assign the role of subsurfaces to surfaces and the data device manager
	 * handles the clipboard. Each of these wlroots interfaces has room for you
	 * to dig your fingers in and play with their behavior if you want. Note
	 * that the clients cannot set the selection directly without compositor
	 * approval, see the handling of the request_set_selection event below.*/
	server.compositor =
		wlr_compositor_create(server.wl_display, 5, server.renderer);
	wlr_subcompositor_create(server.wl_display);
	wlr_data_device_manager_create(server.wl_display);

	/*
	 * Output
	 */

	/* Creates an output layout, which a wlroots utility for working with an
	 * arrangement of screens in a physical layout. */
	server.output_layout = wlr_output_layout_create();
	if (server.output_layout == NULL) {
		wlr_log(WLR_ERROR, "failed to create wlr_output_layout");
		return 1;
	}

	/* Configure a listener to be notified when new outputs are available on the
	 * backend. */
	wl_list_init(&server.outputs);
	server.layout_change.notify = comp_server_layout_change;
	wl_signal_add(&server.output_layout->events.change, &server.layout_change);

	wlr_xdg_output_manager_v1_create(server.wl_display, server.output_layout);
	server.output_manager = wlr_output_manager_v1_create(server.wl_display);
	if (server.output_manager == NULL) {
		wlr_log(WLR_ERROR, "failed to create wlr_output_manager");
		return 1;
	}
	server.output_manager_apply.notify = comp_server_output_manager_apply;
	wl_signal_add(&server.output_manager->events.apply,
				  &server.output_manager_apply);
	server.output_manager_test.notify = comp_server_output_manager_test;
	wl_signal_add(&server.output_manager->events.test,
				  &server.output_manager_test);

	server.new_output.notify = comp_new_output;
	wl_signal_add(&server.backend->events.new_output, &server.new_output);

	/*
	 * Scene
	 */

	/* Create a scene graph. This is a wlroots abstraction that handles all
	 * rendering and damage tracking. All the compositor author needs to do
	 * is add things that should be rendered to the scene graph at the proper
	 * positions and then call wlr_scene_output_commit() to render a frame if
	 * necessary.
	 */
	server.root_scene = wlr_scene_create();

	server.trees.outputs_tree = wlr_scene_tree_create(&server.root_scene->tree);
	server.trees.dnd_tree = wlr_scene_tree_create(&server.root_scene->tree);

	server.scene_layout =
		wlr_scene_attach_output_layout(server.root_scene, server.output_layout);
	if (server.scene_layout == NULL) {
		wlr_log(WLR_ERROR, "failed to attach output_layout to wlr_scene");
		return 1;
	}

	/* Set scene presentation */
	struct wlr_presentation *presentation =
		wlr_presentation_create(server.wl_display, server.backend);
	if (presentation == NULL) {
		wlr_log(WLR_ERROR, "failed to create wlr_presentation");
		return 1;
	}
	wlr_scene_set_presentation(server.root_scene, presentation);

	// Create a fallback headless output
	struct wlr_output *wlr_output = wlr_headless_add_output(
		server.headless_backend, HEADLESS_FALLBACK_OUTPUT_WIDTH,
		HEADLESS_FALLBACK_OUTPUT_HEIGHT);
	wlr_output_set_name(wlr_output, "FALLBACK");
	server.fallback_output = comp_output_create(&server, wlr_output);

	/*
	 * XDG Toplevels
	 */

	/* Set up xdg-shell version 3. The xdg-shell is a Wayland protocol which is
	 * used for application windows. For more detail on shells, refer to
	 * https://drewdevault.com/2018/07/29/Wayland-shells.html.
	 */
	server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 5);
	server.new_xdg_surface.notify = xdg_new_xdg_surface;
	wl_signal_add(&server.xdg_shell->events.new_surface,
				  &server.new_xdg_surface);

	/*
	 * Layer shell
	 */

	server.layer_shell = wlr_layer_shell_v1_create(server.wl_display, 4);
	server.new_layer_surface.notify = layer_shell_new_surface;
	wl_signal_add(&server.layer_shell->events.new_surface,
				  &server.new_layer_surface);

	/*
	 * XWayland
	 */

	server.xwayland_mgr.wlr_xwayland =
		wlr_xwayland_create(server.wl_display, server.compositor, false);
	if (!server.xwayland_mgr.wlr_xwayland) {
		wlr_log(WLR_ERROR, "Failed to start Xwayland");
		unsetenv("DISPLAY");
	} else {
		listener_init(&server.new_xwayland_surface);
		listener_connect(&server.xwayland_mgr.wlr_xwayland->events.new_surface,
						 &server.new_xwayland_surface, xwayland_new_surface);

		listener_init(&server.xwayland_ready);
		listener_connect(&server.xwayland_mgr.wlr_xwayland->events.ready,
						 &server.xwayland_ready, xwayland_ready_cb);

		setenv("DISPLAY", server.xwayland_mgr.wlr_xwayland->display_name, true);
	}

	server.relative_pointer_manager =
		wlr_relative_pointer_manager_v1_create(server.wl_display);

	server.pointer_constraints =
		wlr_pointer_constraints_v1_create(server.wl_display);
	// TODO: Pointer constraint
	// server.pointer_constraint.notify = handle_pointer_constraint;
	// wl_signal_add(&server.pointer_constraints->events.new_constraint,
	// 			  &server.pointer_constraint);

	/*
	 * Seat
	 */

	server.seat = comp_seat_create(&server);

	/*
	 * Init protocols
	 */

	wlr_viewporter_create(server.wl_display);
	wlr_single_pixel_buffer_manager_v1_create(server.wl_display);
	wlr_gamma_control_manager_v1_create(server.wl_display);
	wlr_screencopy_manager_v1_create(server.wl_display);
	wlr_export_dmabuf_manager_v1_create(server.wl_display);
	wlr_fractional_scale_manager_v1_create(server.wl_display, 1);
	wlr_data_control_manager_v1_create(server.wl_display);

	/*
	 * Server side decorations
	 */

	struct wlr_server_decoration_manager *server_decoration_manager =
		wlr_server_decoration_manager_create(server.wl_display);
	if (server_decoration_manager == NULL) {
		wlr_log(WLR_ERROR, "failed to create wlr_server_decoration_manager");
		return 1;
	}
	// Use server-side decoration by default by default
	wlr_server_decoration_manager_set_default_mode(
		server_decoration_manager, WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);

	struct wlr_xdg_decoration_manager_v1 *xdg_decoration_manager =
		wlr_xdg_decoration_manager_v1_create(server.wl_display);
	if (xdg_decoration_manager == NULL) {
		wlr_log(WLR_ERROR, "failed to create wlr_xdg_decoration_manager_v1");
		return 1;
	}
	wl_list_init(&server.xdg_decorations);
	server.new_xdg_decoration.notify = handle_xdg_decoration;
	wl_signal_add(&xdg_decoration_manager->events.new_toplevel_decoration,
				  &server.new_xdg_decoration);

	comp_session_lock_create();

	/*
	 * Wayland socket
	 */

	/* Add a Unix socket to the Wayland display. */
	const char *socket = wl_display_add_socket_auto(server.wl_display);
	if (!socket) {
		wlr_backend_destroy(server.backend);
		return 1;
	}

	/* Start the backend. This will enumerate outputs and inputs, become the DRM
	 * master, etc */
	if (!wlr_backend_start(server.backend)) {
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.wl_display);
		return 1;
	}

	/* Set the WAYLAND_DISPLAY environment variable to our socket and run the
	 * startup command if requested. */
	setenv("WAYLAND_DISPLAY", socket, true);
	if (startup_cmd) {
		if (fork() == 0) {
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, (void *)NULL);
		}
	}

	// Create additional outputs
	if (num_test_outputs > 1) {
		for (int i = 0; i < num_test_outputs - 1; i++) {
			comp_create_extra_output();
		}
	}

	pthread_t init_gtk_thread;
	pthread_create(&init_gtk_thread, NULL, init_gtk, NULL);

	/* Run the Wayland event loop. This does not return until you exit the
	 * compositor. Starting the backend rigged up all of the necessary event
	 * loop configuration to listen to libinput events, DRM events, generate
	 * frame events at the refresh rate, and so on. */
	wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s",
			socket);
	wl_display_run(server.wl_display);
	// Once wl_display_run returns, we destroy all clients then shut down the
	// server.

	pthread_cancel(init_gtk_thread);
	wlr_xwayland_destroy(server.xwayland_mgr.wlr_xwayland);
	wl_display_destroy_clients(server.wl_display);
	comp_cursor_destroy(server.seat->cursor);
	wlr_output_layout_destroy(server.output_layout);
	comp_animation_mgr_destroy(server.animation_mgr);
	wl_display_destroy(server.wl_display);
	wlr_scene_node_destroy(&server.root_scene->tree.node);

	return 0;
}
