#include <assert.h>
#include <scenefx/types/wlr_scene.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <wayland-util.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/log.h>

#include "comp/border/titlebar.h"
#include "comp/output.h"
#include "comp/server.h"
#include "comp/workspace.h"
#include "constants.h"
#include "desktop/toplevel.h"
#include "util.h"

static void output_get_identifier(char *identifier, size_t len,
								  struct comp_output *output) {
	struct wlr_output *wlr_output = output->wlr_output;
	snprintf(identifier, len, "%s %s %s",
			 wlr_output->make ? wlr_output->make : "Unknown",
			 wlr_output->model ? wlr_output->model : "Unknown",
			 wlr_output->serial ? wlr_output->serial : "Unknown");
}

static bool output_match_name_or_id(struct comp_output *output,
									const char *name_or_id) {
	if (strcmp(name_or_id, "*") == 0) {
		return true;
	}

	char identifier[128];
	output_get_identifier(identifier, sizeof(identifier), output);
	return strcasecmp(identifier, name_or_id) == 0 ||
		   strcasecmp(output->wlr_output->name, name_or_id) == 0;
}

struct comp_output *comp_output_by_name_or_id(const char *name_or_id) {
	struct comp_output *output;
	wl_list_for_each(output, &server.outputs, link) {
		if (output_match_name_or_id(output, name_or_id)) {
			return output;
		}
	}
	return NULL;
}

struct comp_workspace *comp_workspace_from_index(struct comp_output *output,
												 size_t index) {
	size_t pos = 0;
	struct comp_workspace *pos_ws;
	wl_list_for_each_reverse(pos_ws, &output->workspaces, output_link) {
		if (pos == index) {
			return pos_ws;
		}
		pos++;
	}

	return NULL;
}

struct comp_workspace *comp_output_get_active_ws(struct comp_output *output,
												 bool fullscreen) {
	enum comp_workspace_type new_ws_type;
	struct comp_workspace *active_ws = output->active_workspace;
	if (active_ws == NULL) {
		new_ws_type = fullscreen ? COMP_WORKSPACE_TYPE_FULLSCREEN
								 : COMP_WORKSPACE_TYPE_REGULAR;
	} else {
		switch (new_ws_type = active_ws->type) {
		case COMP_WORKSPACE_TYPE_FULLSCREEN:
			if (fullscreen) {
				return active_ws;
			}
			// TODO: Try previous ws, else create new ws
			break;
		case COMP_WORKSPACE_TYPE_REGULAR:
			if (!fullscreen) {
				return active_ws;
			}
			// TODO: Create new fullscreen ws
			break;
		}
	}

	return active_ws;
}

static void output_configure_scene(struct comp_output *output,
								   struct wlr_scene_node *node, void *data) {
	if (!node->enabled) {
		return;
	}

	switch (node->type) {
	case WLR_SCENE_NODE_BUFFER:;
		struct wlr_scene_buffer *buffer = wlr_scene_buffer_from_node(node);

		struct wlr_scene_surface *scene_surface =
			wlr_scene_surface_try_from_buffer(buffer);
		if (!scene_surface) {
			return;
		}

		struct wlr_xdg_surface *xdg_surface =
			wlr_xdg_surface_try_from_wlr_surface(scene_surface->surface);

		if (data && xdg_surface &&
			xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {

			struct comp_object *object = data;
			struct comp_toplevel *toplevel = object->data;
			struct comp_titlebar *titlebar = toplevel->titlebar;

			float opacity = 1;
			// Change the opacity of the grabbed toplevel if it's not displayed
			// on it's active monitor
			if (output != toplevel->workspace->output) {
				opacity = TOPLEVEL_NON_MAIN_OUTPUT_OPACITY;
			}
			wlr_scene_buffer_set_opacity(buffer, opacity);
			wlr_scene_buffer_set_opacity(titlebar->widget.scene_buffer,
										 opacity);
		}
		break;
	case WLR_SCENE_NODE_TREE:;
		struct wlr_scene_tree *tree = wl_container_of(node, tree, node);
		if (node->data) {
			data = node->data;
		}
		struct wlr_scene_node *node;
		wl_list_for_each(node, &tree->children, link) {
			output_configure_scene(output, node, data);
		}
	default:
		break;
	}
}

static void output_frame(struct wl_listener *listener, void *data) {
	/* This function is called every time an output is ready to display a frame,
	 * generally at the output's refresh rate (e.g. 60Hz). */
	struct comp_output *output = wl_container_of(listener, output, frame);
	struct wlr_scene *scene = output->server->root_scene;

	struct wlr_scene_output *scene_output =
		wlr_scene_get_scene_output(scene, output->wlr_output);

	output_configure_scene(output, &server.root_scene->tree.node, NULL);

	/* Render the scene if needed and commit the output */
	wlr_scene_output_commit(scene_output, NULL);

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_request_state(struct wl_listener *listener, void *data) {
	/* This function is called when the backend requests a new state for
	 * the output. For example, Wayland and X11 backends request a new mode
	 * when the output window is resized. */
	struct comp_output *output =
		wl_container_of(listener, output, request_state);

	const struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(output->wlr_output, event->state);
}

static void evacuate_workspaces(struct comp_output *output) {
	if (wl_list_empty(&output->workspaces)) {
		return;
	}

	// Get the new output
	struct comp_output *dest_output = NULL;
	wl_list_for_each(dest_output, &server.outputs, link) {
		if (dest_output != output) {
			break;
		}
	}
	if (dest_output == NULL) {
		dest_output = server.fallback_output;
	}

	wlr_log(WLR_DEBUG, "Evacuating workspace to output '%s'",
			dest_output->wlr_output->name);

	// TODO: Test this with multiple monitors
	struct comp_workspace *workspace, *tmp_ws;
	wl_list_for_each_reverse_safe(workspace, tmp_ws, &output->workspaces,
								  output_link) {
		comp_output_move_workspace_to(dest_output, workspace);
	}

	// Focus the last workspace on the destination output
	struct comp_workspace *last_ws =
		wl_container_of(dest_output->workspaces.prev, last_ws, output_link);
	comp_output_focus_workspace(dest_output, last_ws);
}

static void output_destroy(struct wl_listener *listener, void *data) {
	struct comp_output *output = wl_container_of(listener, output, destroy);

	evacuate_workspaces(output);

	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);

	wlr_scene_node_destroy(&output->output_tree->node);

	free(output);
}

struct comp_output *comp_output_create(struct comp_server *server,
									   struct wlr_output *wlr_output) {
	struct comp_output *output = calloc(1, sizeof(*output));
	output->wlr_output = wlr_output;
	wlr_output->data = output;
	output->server = server;

	// Initialize layers
	output->output_tree = wlr_scene_tree_create(&server->root_scene->tree);

	output->layers.shell_background =
		wlr_scene_tree_create(output->output_tree);
	output->layers.shell_bottom = wlr_scene_tree_create(output->output_tree);
	output->layers.optimized_blur_node = wlr_scene_blur_create(
		output->output_tree, wlr_output->width, wlr_output->height);
	output->layers.workspaces = wlr_scene_tree_create(output->output_tree);
	output->layers.shell_top = wlr_scene_tree_create(output->output_tree);
	output->layers.fullscreen = wlr_scene_tree_create(output->output_tree);
	output->layers.shell_overlay = wlr_scene_tree_create(output->output_tree);
	output->layers.seat = wlr_scene_tree_create(output->output_tree);
	output->layers.session_lock = wlr_scene_tree_create(output->output_tree);

	// Initially disable due to this potentially being a fallback wlr_output
	wlr_scene_node_set_enabled(&output->layers.optimized_blur_node->node,
							   false);

	wl_list_init(&output->workspaces);


	wl_list_insert(&server->outputs, &output->link);

	return output;
}

void comp_new_output(struct wl_listener *listener, void *data) {
	/* This event is raised by the backend when a new output (aka a display or
	 * monitor) becomes available. */
	struct comp_server *server = wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	if (wlr_output == server->fallback_output->wlr_output) {
		return;
	}

	/* Configures the output created by the backend to use our allocator
	 * and our renderer. Must be done once, before commiting the output */
	wlr_output_init_render(wlr_output, server->allocator, server->renderer);

	/* The output may be disabled, switch it on. */
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);

	/* Some backends don't have modes. DRM+KMS does, and we need to set a mode
	 * before we can use the output. The mode is a tuple of (width, height,
	 * refresh rate), and each monitor supports only a specific set of modes. We
	 * just pick the monitor's preferred mode, a more sophisticated compositor
	 * would let the user configure it. */
	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (mode != NULL) {
		wlr_output_state_set_mode(&state, mode);
	}

	/* Atomically applies the new output state. */
	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);

	/* Allocates and configures our state for this output */
	struct comp_output *output = comp_output_create(server, wlr_output);

	if (server->active_output == NULL) {
		server->active_output = output;
	}

	/*
	 * Workspaces
	 */

	// Create the initial workspace
	if (comp_workspace_new(output, COMP_WORKSPACE_TYPE_REGULAR) == NULL) {
		wlr_log(WLR_ERROR, "Could not create initial workspaces for output: %s",
				output->wlr_output->name);
		abort();
	}

	/*
	 * Signals
	 */

	/* Sets up a listener for the frame event. */
	output->frame.notify = output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);

	/* Sets up a listener for the state request event. */
	output->request_state.notify = output_request_state;
	wl_signal_add(&wlr_output->events.request_state, &output->request_state);

	/* Sets up a listener for the destroy event. */
	output->destroy.notify = output_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);

	/* Adds this to the output layout. The add_auto function arranges outputs
	 * from left-to-right in the order they appear. A more sophisticated
	 * compositor would let the user configure the arrangement of outputs in the
	 * layout.
	 *
	 * The output layout utility automatically adds a wl_output global to the
	 * display, which Wayland clients can see to find out information about the
	 * output (such as DPI, scale factor, manufacturer, etc).
	 */
	struct wlr_scene_output *scene_output =
		wlr_scene_output_create(server->root_scene, wlr_output);
	output->scene_output = scene_output;
	struct wlr_output_layout_output *l_output =
		wlr_output_layout_add_auto(server->output_layout, wlr_output);
	wlr_scene_output_layout_add_output(server->scene_layout, l_output,
									   scene_output);
}

void comp_output_disable(struct comp_output *output) {
	struct comp_server *server = output->server;

	// Disable output and set a new active output as active
	if (output == server->active_output) {
		server->active_output = NULL;
		struct comp_output *iter;
		wl_list_for_each(iter, &server->outputs, link) {
			if (iter->wlr_output->enabled) {
				server->active_output = iter;
				break;
			}
		}
	}

	if (server->active_output) {
		// Move all workspaces to a new monitor
		evacuate_workspaces(output);
	}
}

void comp_output_update_sizes(struct comp_output *output) {
	// Update the monitors geometry wlr_box
	wlr_output_layout_get_box(server.output_layout, output->wlr_output,
							  &output->geometry);

	const int output_x = output->geometry.x, output_y = output->geometry.y;

	// Update the scene_output position
	wlr_scene_output_set_position(output->scene_output, output_x, output_y);

	// Update the output tree position to match the scene_output
	wlr_scene_node_set_position(&output->output_tree->node, output_x, output_y);

	// Update optimized blur node position and size
	wlr_scene_node_set_enabled(&output->layers.optimized_blur_node->node, true);
	wlr_scene_node_set_position(&output->layers.optimized_blur_node->node,
								output->geometry.x, output->geometry.y);
	// Also marks the blur as dirty
	wlr_scene_blur_set_size(output->layers.optimized_blur_node,
							output->geometry.width, output->geometry.height);
}

void comp_output_move_workspace_to(struct comp_output *dest_output,
								   struct comp_workspace *ws) {
	if (ws->output == dest_output || !ws) {
		return;
	}

	// Remove from previous output
	if (ws->output) {
		wl_list_remove(&ws->output_link);
		ws->output = NULL;
	}

	// Add to the new output
	wlr_scene_node_reparent(&ws->workspace_tree->node,
							dest_output->layers.workspaces);
	wlr_scene_node_set_enabled(&ws->workspace_tree->node, false);

	ws->output = dest_output;

	wl_list_insert(&dest_output->workspaces, &ws->output_link);
}

void comp_output_focus_workspace(struct comp_output *output,
								 struct comp_workspace *ws) {
	assert(ws);

	// Disable the previous workspace
	if (output->active_workspace) {
		wlr_scene_node_set_enabled(
			&output->active_workspace->workspace_tree->node, false);
	}
	output->prev_workspace = output->active_workspace;

	// Enable the active workspace
	output->active_workspace = ws;

	// Make sure that all other workspaces are disabled
	struct comp_workspace *workspace;
	wl_list_for_each(workspace, &output->workspaces, output_link) {
		wlr_scene_node_set_enabled(&workspace->workspace_tree->node,
								   workspace == output->active_workspace);
	}
}

static struct comp_workspace *
comp_output_dir_workspace(struct comp_output *output, bool should_wrap,
						  int dir) {
	if (!output) {
		wlr_log(WLR_ERROR, "Could not switch workspace on NULL output");
		return NULL;
	}

	int index = comp_workspace_find_index(&output->workspaces,
										  output->active_workspace);

	size_t new_index;
	if (!should_wrap) {
		new_index = index += dir;
		if (index < 0 || index >= wl_list_length(&output->workspaces)) {
			return NULL;
		}
	} else {
		new_index = wrap(index + dir, wl_list_length(&output->workspaces));
	}

	struct comp_workspace *ws = comp_workspace_from_index(output, new_index);
	wlr_log(WLR_DEBUG, "Switched to workspace %zu on output %s", new_index,
			output->wlr_output->name);
	return ws;
}

struct comp_workspace *comp_output_prev_workspace(struct comp_output *output,
												  bool should_wrap) {
	return comp_output_dir_workspace(output, should_wrap, -1);
}

struct comp_workspace *comp_output_next_workspace(struct comp_output *output,
												  bool should_wrap) {
	return comp_output_dir_workspace(output, should_wrap, 1);
}