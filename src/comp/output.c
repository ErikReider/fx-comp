#include <assert.h>
#include <gtk-3.0/gtk/gtk.h>
#include <scenefx/types/wlr_scene.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <wayland-util.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/log.h>

#include "comp/lock.h"
#include "comp/object.h"
#include "comp/output.h"
#include "comp/server.h"
#include "comp/tiling_node.h"
#include "comp/widget.h"
#include "comp/workspace.h"
#include "constants.h"
#include "desktop/layer_shell.h"
#include "desktop/toplevel.h"
#include "desktop/widgets/workspace_indicator.h"
#include "seat/seat.h"
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

struct comp_workspace *
comp_output_new_workspace(struct comp_output *output,
						  enum comp_workspace_type type) {
	return comp_workspace_new(output, type);
}

void comp_output_remove_workspace(struct comp_output *output,
								  struct comp_workspace *ws) {
	if (!wl_list_empty(&ws->toplevels)) {
		return;
	}

	const int num_ws = wl_list_length(&output->workspaces);
	// Replace the fullscreen workspace with a regular one
	if (num_ws <= 2 && ws->type == COMP_WORKSPACE_TYPE_FULLSCREEN) {
		comp_output_new_workspace(output, COMP_WORKSPACE_TYPE_REGULAR);
	} else if (num_ws <= 2) {
		return;
	}

	bool is_active = ws == output->active_workspace;
	comp_workspace_destroy(ws);

	if (is_active) {
		comp_output_focus_workspace(output, output->prev_workspace);
		// The previous workspace should be set to NULL when there's only one
		// workspace visible
		if (num_ws - 1 == 1) {
			output->prev_workspace = NULL;
			return;
		}

		struct comp_workspace *prev_ws = wl_container_of(
			output->active_workspace->output_link.next, prev_ws, output_link);
		output->prev_workspace = prev_ws;
	}
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
								   struct wlr_scene_node *node) {
	if (!node->enabled) {
		return;
	}

	if (node->type == WLR_SCENE_NODE_BUFFER) {
		struct wlr_scene_buffer *buffer = wlr_scene_buffer_from_node(node);
		// struct wlr_scene_surface *surface =
		// 	wlr_scene_surface_try_from_buffer(buffer);

		struct comp_object *obj = buffer->node.data;
		if (!obj) {
			wlr_log(WLR_DEBUG,
					"Tried to apply effects to buffer with unknown data");
			return;
		}
		if (obj->type == COMP_OBJECT_TYPE_TOPLEVEL) {
			struct comp_toplevel *toplevel = obj->data;
			// Stretch the saved toplevel buffer to fit the toplevel state
			if (!wl_list_empty(&toplevel->saved_scene_tree->children)) {
				int width = toplevel->state.width;
				int height = toplevel->state.height;
				if (buffer->transform & WL_OUTPUT_TRANSFORM_90) {
					wlr_scene_buffer_set_dest_size(buffer, height, width);
				} else {
					wlr_scene_buffer_set_dest_size(buffer, width, height);
				}
			}
		}

	} else if (node->type == WLR_SCENE_NODE_TREE) {
		struct wlr_scene_tree *tree = wlr_scene_tree_from_node(node);
		struct wlr_scene_node *node;
		wl_list_for_each(node, &tree->children, link) {
			output_configure_scene(output, node);
		}
	}
}

static void output_frame(struct wl_listener *listener, void *data) {
	/* This function is called every time an output is ready to display a frame,
	 * generally at the output's refresh rate (e.g. 60Hz). */
	struct comp_output *output = wl_container_of(listener, output, frame);
	if (!output->wlr_output->enabled) {
		return;
	}

	struct wlr_scene *scene = output->server->root_scene;
	struct wlr_scene_output *scene_output =
		wlr_scene_get_scene_output(scene, output->wlr_output);

	output_configure_scene(output, &server.root_scene->tree.node);
	// wlr_scene_optimized_blur_mark_dirty(scene);
	// wlr_output_layout_get_box(server.output_layout, output->wlr_output,
	// 						  &output->geometry);
	// wlr_scene_output_set_position(scene_output, output->geometry.x,
	// 							  output->geometry.y);

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

static void output_present(struct wl_listener *listener, void *data) {
	struct comp_output *output = wl_container_of(listener, output, present);

	struct wlr_output_event_present *output_event = data;

	if (!output->wlr_output->enabled || !output_event->presented) {
		return;
	}

	output->refresh_nsec = output_event->refresh;
	output->refresh_sec = (float)output_event->refresh / NSEC_IN_SECONDS;
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

	bool moved = false;
	struct comp_workspace *workspace, *tmp_ws;
	wl_list_for_each_reverse_safe(workspace, tmp_ws, &output->workspaces,
								  output_link) {
		// Ignore empty workspaces
		if (!wl_list_empty(&workspace->toplevels)) {
			comp_output_move_workspace_to(dest_output, workspace);
			moved = true;
		}
	}

	// Ignore if no workspaces were moved
	if (moved) {
		// Focus the last workspace on the destination output
		struct comp_workspace *last_ws =
			wl_container_of(dest_output->workspaces.next, last_ws, output_link);
		comp_output_focus_workspace(dest_output, last_ws);
		wl_signal_emit_mutable(&output->events.ws_change, output);
	}
}

static void output_destroy(struct wl_listener *listener, void *data) {
	struct comp_output *output = wl_container_of(listener, output, destroy);

	if (output->wlr_output->enabled) {
		comp_output_disable(output);
	}

	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->present.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);

	wlr_scene_output_destroy(output->scene_output);
	output->wlr_output->data = NULL;
	output->wlr_output = NULL;
	output->scene_output = NULL;

	wlr_scene_node_destroy(&output->object.scene_tree->node);

	free(output);
}

struct comp_output *comp_output_create(struct comp_server *server,
									   struct wlr_output *wlr_output) {
	struct comp_output *output = calloc(1, sizeof(*output));
	if (!output) {
		wlr_log(WLR_ERROR, "Could not allocate comp_output");
		return NULL;
	}
	output->wlr_output = wlr_output;
	wlr_output->data = output;
	output->server = server;

	output->object.scene_tree = alloc_tree(server->trees.outputs_tree);
	output->object.content_tree = alloc_tree(output->object.scene_tree);
	output->object.scene_tree->node.data = &output->object;
	output->object.data = output;
	output->object.type = COMP_OBJECT_TYPE_OUTPUT;
	output->object.destroying = false;

	// Initialize layers
	output->layers.shell_background = alloc_tree(output->object.content_tree);
	output->layers.shell_bottom = alloc_tree(output->object.content_tree);
	output->layers.optimized_blur_node = wlr_scene_blur_create(
		output->object.scene_tree, wlr_output->width, wlr_output->height);
	output->layers.workspaces = alloc_tree(output->object.content_tree);
	output->layers.unmanaged = alloc_tree(output->object.content_tree);
	output->layers.shell_top = alloc_tree(output->object.content_tree);
	output->layers.shell_overlay = alloc_tree(output->object.content_tree);
	output->layers.session_lock = alloc_tree(output->object.content_tree);

	// Initially disable due to this potentially being a fallback wlr_output
	wlr_scene_node_set_enabled(&output->layers.optimized_blur_node->node,
							   false);

	wl_list_init(&output->workspaces);

	wl_list_insert(&server->outputs, &output->link);

	wl_signal_init(&output->events.ws_change);

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
	if (!output) {
		wlr_output_destroy(wlr_output);
		return;
	}

	if (server->active_output == NULL) {
		server->active_output = output;
	}

	/*
	 * Workspaces
	 */

	// Create the initial workspaces
	struct comp_workspace *first_ws =
		comp_workspace_new(output, COMP_WORKSPACE_TYPE_REGULAR);
	struct comp_workspace *second_ws =
		comp_workspace_new(output, COMP_WORKSPACE_TYPE_REGULAR);
	if (!first_ws || !second_ws) {
		wlr_log(WLR_ERROR, "Could not create initial workspaces for output: %s",
				output->wlr_output->name);
		abort();
	}
	comp_output_focus_workspace(output, first_ws);

	output->ws_indicator = comp_ws_indicator_init(server, output);

	/*
	 * Signals
	 */

	wl_signal_init(&output->events.disable);

	/* Sets up a listener for the frame event. */
	output->frame.notify = output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);

	/* Sets up a listener for the state request event. */
	output->request_state.notify = output_request_state;
	wl_signal_add(&wlr_output->events.request_state, &output->request_state);

	/* Sets up a listener for the present event. */
	output->present.notify = output_present;
	wl_signal_add(&wlr_output->events.present, &output->present);

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

	if (server->comp_session_lock.locked) {
		comp_session_lock_add_output(wlr_output);
	}
}

void comp_output_disable(struct comp_output *output) {
	wlr_log(WLR_DEBUG, "Disabling output '%s'", output->wlr_output->name);

	struct comp_server *server = output->server;

	wl_signal_emit_mutable(&output->events.disable, output);

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
	wlr_scene_node_set_position(&output->object.scene_tree->node, output_x,
								output_y);

	// Update optimized blur node position and size
	wlr_scene_node_set_enabled(&output->layers.optimized_blur_node->node, true);
	wlr_scene_node_set_position(&output->layers.optimized_blur_node->node,
								output->geometry.x, output->geometry.y);
	// Also marks the blur as dirty
	wlr_scene_blur_set_size(output->layers.optimized_blur_node,
							output->geometry.width, output->geometry.height);

	comp_output_arrange_layers(output);
	comp_output_arrange_output(output);

	// TODO: Update toplevel positions/tiling/sizes Only enable for actual
	// outputs
	// printf("GEO: %i %i %ix%i\n", output->geometry.width,
	// 	   output->geometry.height, output->geometry.x,
	// output->geometry.y);
}

void comp_output_move_workspace_to(struct comp_output *dest_output,
								   struct comp_workspace *ws) {
	if (!ws || ws->output == dest_output) {
		return;
	}

	// Remove from previous output
	if (ws->output) {
		wl_list_remove(&ws->output_link);
		ws->output = NULL;
	}

	// Add to the new output
	wlr_scene_node_reparent(&ws->object.scene_tree->node,
							dest_output->layers.workspaces);
	wlr_scene_node_set_enabled(&ws->object.scene_tree->node, false);

	ws->output = dest_output;

	wl_list_insert(&dest_output->workspaces, &ws->output_link);
	wl_signal_emit_mutable(&dest_output->events.ws_change, dest_output);
}

void comp_output_focus_workspace(struct comp_output *output,
								 struct comp_workspace *ws) {
	assert(ws);

	// Disable the previous workspace
	if (output->active_workspace) {
		wlr_scene_node_set_enabled(
			&output->active_workspace->object.scene_tree->node, false);
	}
	output->prev_workspace = output->active_workspace;

	// Enable the active workspace
	output->active_workspace = ws;

	// Make sure that all other workspaces are disabled
	struct comp_workspace *workspace;
	wl_list_for_each(workspace, &output->workspaces, output_link) {
		wlr_scene_node_set_enabled(&workspace->object.scene_tree->node,
								   workspace == output->active_workspace);
	}

	comp_output_arrange_output(output);

	// Refocus the lastest focused toplevel
	if (!wl_list_empty(&ws->toplevels)) {
		struct comp_toplevel *latest = comp_workspace_get_latest_focused(ws);
		if (latest) {
			comp_seat_surface_focus(&latest->object,
									comp_toplevel_get_wlr_surface(latest));
		}
	}

	wl_signal_emit_mutable(&output->events.ws_change, output);
}

enum workspace_dir {
	WORKSPACE_DIR_NEXT,
	WORKSPACE_DIR_PREV,
};

static struct comp_workspace *
comp_output_dir_workspace(struct comp_output *output, bool should_wrap,
						  enum workspace_dir dir) {
	if (!output) {
		wlr_log(WLR_ERROR, "Could not switch workspace on NULL output");
		return NULL;
	}

	struct wl_list *link;
	switch (dir) {
	case WORKSPACE_DIR_NEXT:
		link = output->active_workspace->output_link.prev;
		if (link == &output->workspaces) {
			if (!should_wrap) {
				return NULL;
			}
			link = output->workspaces.prev;
		}
		break;
	case WORKSPACE_DIR_PREV:
		link = output->active_workspace->output_link.next;
		if (link == &output->workspaces) {
			if (!should_wrap) {
				return NULL;
			}
			link = output->workspaces.next;
		}
		break;
	}

	struct comp_workspace *ws = wl_container_of(link, ws, output_link);
	return ws;
}

struct comp_workspace *comp_output_prev_workspace(struct comp_output *output,
												  bool should_wrap) {
	return comp_output_dir_workspace(output, should_wrap, WORKSPACE_DIR_PREV);
}

struct comp_workspace *comp_output_next_workspace(struct comp_output *output,
												  bool should_wrap) {
	return comp_output_dir_workspace(output, should_wrap, WORKSPACE_DIR_NEXT);
}

/*
 * Arrange functions
 */

void comp_output_arrange_output(struct comp_output *output) {
	// Center Workspace Switcher
	if (output->ws_indicator) {
		comp_widget_center_on_output(&output->ws_indicator->widget, output);
	}

	// Arrange workspaces
	struct comp_workspace *ws;
	wl_list_for_each_reverse(ws, &output->workspaces, output_link) {
		tiling_node_mark_workspace_dirty(ws);

		bool is_fullscreen = ws->type == COMP_WORKSPACE_TYPE_FULLSCREEN &&
							 !wl_list_empty(&ws->toplevels);
		if (is_fullscreen) {
			// Update the position and size of the fullscreen toplevel
			struct comp_toplevel *toplevel;
			wl_list_for_each_reverse(toplevel, &ws->toplevels, workspace_link) {
				if (!toplevel->fullscreen) {
					continue;
				}
				struct wlr_box output_box =
					toplevel->workspace->output->geometry;
				comp_toplevel_set_position(toplevel, 0, 0);
				comp_toplevel_set_size(toplevel, output_box.width,
									   output_box.height);
				comp_object_mark_dirty(&toplevel->object);
			}
		}
	}
	comp_transaction_commit_dirty(true);

	ws = output->active_workspace;
	bool is_locked = server.comp_session_lock.locked;
	bool is_fullscreen = ws->type == COMP_WORKSPACE_TYPE_FULLSCREEN &&
						 !wl_list_empty(&ws->toplevels);

	// Disable all layers when locked but also disable background, bottom,
	// and top layers when fullscreen
	wlr_scene_node_set_enabled(&output->layers.shell_background->node,
							   !is_fullscreen && !is_locked);
	wlr_scene_node_set_enabled(&output->layers.shell_bottom->node,
							   !is_fullscreen && !is_locked);
	wlr_scene_node_set_enabled(&output->layers.optimized_blur_node->node,
							   !is_fullscreen && !is_locked);
	wlr_scene_node_set_enabled(&output->layers.workspaces->node, !is_locked);
	wlr_scene_node_set_enabled(&output->layers.shell_top->node,
							   !is_fullscreen && !is_locked);
	wlr_scene_node_set_enabled(&output->layers.shell_overlay->node, !is_locked);
}

static void arrange_layer_surfaces(struct comp_output *output,
								   const struct wlr_box *full_area,
								   struct wlr_box *usable_area,
								   struct wlr_scene_tree *tree) {
	struct wlr_scene_node *node;
	wl_list_for_each(node, &tree->children, link) {
		struct comp_object *object = node->data;
		if (!object || object->type != COMP_OBJECT_TYPE_LAYER_SURFACE) {
			continue;
		}
		struct comp_layer_surface *layer_surface = object->data;

		// surface could be null during destruction
		if (!layer_surface || !layer_surface->scene_layer ||
			!layer_surface->scene_layer->layer_surface->initialized) {
			continue;
		}

		wlr_scene_layer_surface_v1_configure(layer_surface->scene_layer,
											 full_area, usable_area);
	}
}

void comp_output_arrange_layers(struct comp_output *output) {
	struct wlr_box usable_area = {0};
	wlr_output_effective_resolution(output->wlr_output, &usable_area.width,
									&usable_area.height);
	const struct wlr_box full_area = usable_area;

	arrange_layer_surfaces(output, &full_area, &usable_area,
						   output->layers.shell_background);
	arrange_layer_surfaces(output, &full_area, &usable_area,
						   output->layers.shell_bottom);
	arrange_layer_surfaces(output, &full_area, &usable_area,
						   output->layers.shell_top);
	arrange_layer_surfaces(output, &full_area, &usable_area,
						   output->layers.shell_overlay);

	if (!wlr_box_equal(&usable_area, &output->usable_area)) {
		wlr_log(WLR_DEBUG, "Usable area changed, rearranging output");
		output->usable_area = usable_area;
		comp_output_arrange_output(output);
	}

	// Update and focus the topmost layer surface
	struct comp_seat *seat = server.seat;
	seat->exclusive_layer = false;

	const struct wlr_scene_tree *layers_above_shell[] = {
		output->layers.shell_overlay,
		output->layers.shell_top,
	};
	for (size_t i = 0; i < 2; i++) {
		const struct wlr_scene_tree *layer = layers_above_shell[i];
		struct wlr_scene_node *node;
		wl_list_for_each_reverse(node, &layer->children, link) {
			struct comp_object *obj = node->data;
			if (!obj || obj->type != COMP_OBJECT_TYPE_LAYER_SURFACE) {
				continue;
			}
			struct comp_layer_surface *surface = obj->data;
			if (surface &&
				surface->wlr_layer_surface->current.keyboard_interactive ==
					ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE &&
				surface->wlr_layer_surface->surface &&
				surface->wlr_layer_surface->surface->mapped) {
				comp_seat_surface_focus(&surface->object,
										surface->wlr_layer_surface->surface);
				return;
			}
		}
	}

	// Not found
	if (seat->focused_layer_surface &&
		seat->focused_layer_surface->wlr_layer_surface->current
				.keyboard_interactive !=
			ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE) {
		comp_seat_surface_focus(
			&seat->focused_layer_surface->object,
			seat->focused_layer_surface->wlr_layer_surface->surface);
	}
}
