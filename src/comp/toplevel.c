#include <assert.h>
#include <float.h>
#include <glib.h>
#include <scenefx/types/wlr_scene.h>
#include <stdbool.h>
#include <stdio.h>
#include <wayland-util.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>

#include "comp/object.h"
#include "comp/output.h"
#include "comp/server.h"
#include "comp/tiling_node.h"
#include "comp/workspace.h"
#include "constants.h"
#include "desktop/toplevel.h"
#include "desktop/widgets/resize_edge.h"
#include "desktop/widgets/titlebar.h"
#include "seat/cursor.h"
#include "seat/seat.h"
#include "util.h"

static void center_toplevel(struct comp_toplevel *toplevel,
							bool center_on_cursor) {
	double x = 0;
	double y = 0;
	if (center_on_cursor) {
		x = server.seat->cursor->wlr_cursor->x -
			toplevel->decorated_size.width * 0.5;
		y = server.seat->cursor->wlr_cursor->y -
			toplevel->decorated_size.height * 0.5;
	} else {
		struct comp_workspace *ws = toplevel->state.workspace;
		struct comp_object *parent_object = NULL;
		struct wlr_box relative_box = {0};

		if (toplevel->parent_tree &&
			(parent_object = toplevel->parent_tree->node.data) &&
			parent_object->type == COMP_OBJECT_TYPE_TOPLEVEL) {
			relative_box = comp_toplevel_get_geometry(parent_object->data);
		} else {
			wlr_output_layout_get_box(toplevel->server->output_layout,
									  ws->output->wlr_output, &relative_box);
		}
		x = (relative_box.width - toplevel->decorated_size.width) * 0.5;
		y = (relative_box.height - toplevel->decorated_size.height) * 0.5;
	}
	comp_toplevel_set_position(toplevel, x, y);
}

static void save_state(struct comp_toplevel *toplevel) {
	// Position
	int x, y;
	wlr_scene_node_coords(&toplevel->object.scene_tree->node, &x, &y);
	// Adjust the node coordinates to be output-relative
	double lx = x;
	double ly = y;
	wlr_output_layout_output_coords(
		server.output_layout, toplevel->state.workspace->output->wlr_output,
		&lx, &ly);
	toplevel->saved_state.x = lx;
	toplevel->saved_state.y = ly;
	// Size
	toplevel->saved_state.width = toplevel->state.width;
	toplevel->saved_state.height = toplevel->state.height;

	toplevel->saved_state.workspace = toplevel->state.workspace;
}

static void restore_state(struct comp_toplevel *toplevel) {
	struct comp_output *output = toplevel->state.workspace->output;
	struct comp_workspace *fs_ws = toplevel->state.workspace;

	if (fs_ws->type == COMP_WORKSPACE_TYPE_FULLSCREEN) {
		struct comp_workspace *saved_ws = toplevel->saved_state.workspace;
		// Make sure that the workspace still exists...
		struct comp_workspace *pos, *ws = NULL;
		wl_list_for_each_reverse(pos, &output->workspaces, output_link) {
			if (pos == saved_ws) {
				ws = saved_ws;
				break;
			}
		}
		// If the saved workspace got removed, move to the closest previous
		// workspace
		if (!ws) {
			ws = comp_output_prev_workspace(output, true);
		}

		// Move all toplevels to the regular workspace
		struct comp_toplevel *toplevel_pos, *tmp;
		wl_list_for_each_reverse_safe(toplevel_pos, tmp, &fs_ws->toplevels,
									  workspace_link) {
			comp_workspace_move_toplevel_to(ws, toplevel_pos);
		}
		comp_output_remove_workspace(output, fs_ws);
		comp_output_focus_workspace(output, ws);
	}

	comp_toplevel_set_position(toplevel, toplevel->saved_state.x,
							   toplevel->saved_state.y);
	comp_toplevel_set_size(toplevel, toplevel->saved_state.width,
						   toplevel->saved_state.height);
	comp_toplevel_mark_dirty(toplevel);

	toplevel->saved_state.x = 0;
	toplevel->saved_state.y = 0;
	toplevel->saved_state.width = 0;
	toplevel->saved_state.height = 0;
	toplevel->saved_state.workspace = NULL;
}

/**
 * Returns the output where the majority size of the toplevel resides
 */
static struct comp_output *find_output(struct comp_toplevel *toplevel) {
	int x, y;
	wlr_scene_node_coords(&toplevel->object.scene_tree->node, &x, &y);

	double center_x = x + (double)toplevel->decorated_size.width / 2;
	double center_y = y + (double)toplevel->decorated_size.height / 2;
	struct comp_output *closest_output = NULL;
	double closest_distance = DBL_MAX;

	struct comp_output *output = NULL;
	wl_list_for_each(output, &server.outputs, link) {
		struct wlr_box geometry = output->geometry;
		double closest_x, closest_y;
		wlr_box_closest_point(&geometry, center_x, center_y, &closest_x,
							  &closest_y);
		if (center_x == closest_x && center_y == closest_y) {
			// The center of the floating container is on this output
			return output;
		}
		double x_dist = closest_x - center_x;
		double y_dist = closest_y - center_y;
		double distance = x_dist * x_dist + y_dist * y_dist;
		if (distance < closest_distance) {
			closest_output = output;
			closest_distance = distance;
		}
	}
	return closest_output;
}

void comp_toplevel_process_cursor_move(struct comp_server *server,
									   uint32_t time) {
	/* Move the grabbed toplevel to the new position. */
	struct comp_toplevel *toplevel = server->seat->grabbed_toplevel;
	if (toplevel && !toplevel->fullscreen &&
		toplevel->tiling_mode == COMP_TILING_MODE_FLOATING) {
		// Adjust the toplevel coordinates to be root-relative
		double lx = server->seat->cursor->wlr_cursor->x - server->seat->grab_x;
		double ly = server->seat->cursor->wlr_cursor->y - server->seat->grab_y;
		if (toplevel->dragging_tiled) {
			// Always center the toplevel when dragging a tiled toplevel
			lx = server->seat->cursor->wlr_cursor->x -
				 toplevel->decorated_size.width * 0.5;
			ly = server->seat->cursor->wlr_cursor->y -
				 toplevel->decorated_size.height * 0.5;
		}
		wlr_output_layout_output_coords(
			server->output_layout,
			toplevel->state.workspace->output->wlr_output, &lx, &ly);
		comp_toplevel_set_position(toplevel, lx, ly);

		// Update floating toplevels current monitor and workspace.
		// Also raise the output node to the top so that it's floating toplevels
		// remain on top on other outputs (if they intersect)
		struct comp_output *new_output = find_output(toplevel);
		struct comp_workspace *ws;
		if (new_output && (ws = comp_output_get_active_ws(
							   new_output, toplevel->fullscreen))) {
			comp_workspace_move_toplevel_to(ws, toplevel);
			// Update the active output
			server->active_output = new_output;
			wlr_scene_node_raise_to_top(&new_output->object.scene_tree->node);
		}
	}
}

void comp_toplevel_process_cursor_resize(struct comp_server *server,
										 uint32_t time) {
	/*
	 * Resizing the grabbed toplevel can be a little bit complicated, because we
	 * could be resizing from any corner or edge. This not only resizes the
	 * toplevel on one or two axes, but can also move the toplevel if you resize
	 * from the top or left edges (or top-left corner).
	 *
	 * Note that some shortcuts are taken here. In a more fleshed-out
	 * compositor, you'd wait for the client to prepare a buffer at the new
	 * size, then commit any movement that was prepared.
	 */
	struct comp_toplevel *toplevel = server->seat->grabbed_toplevel;
	if (toplevel->fullscreen) {
		return;
	}

	switch (toplevel->tiling_mode) {
	case COMP_TILING_MODE_TILED:
		tiling_node_resize(toplevel);
		return;
	case COMP_TILING_MODE_FLOATING:
		break;
	}

	double border_x =
		server->seat->cursor->wlr_cursor->x - server->seat->grab_x;
	double border_y =
		server->seat->cursor->wlr_cursor->y - server->seat->grab_y;
	int new_left = server->seat->grab_geobox.x;
	int new_right =
		server->seat->grab_geobox.x + server->seat->grab_geobox.width;
	int new_top = server->seat->grab_geobox.y;
	int new_bottom =
		server->seat->grab_geobox.y + server->seat->grab_geobox.height;

	if (server->seat->resize_edges & WLR_EDGE_TOP) {
		new_top = border_y;
		if (new_top >= new_bottom) {
			new_top = new_bottom - 1;
		}
	} else if (server->seat->resize_edges & WLR_EDGE_BOTTOM) {
		new_bottom = border_y;
		if (new_bottom <= new_top) {
			new_bottom = new_top + 1;
		}
	}
	if (server->seat->resize_edges & WLR_EDGE_LEFT) {
		new_left = border_x;
		if (new_left >= new_right) {
			new_left = new_right - 1;
		}
	} else if (server->seat->resize_edges & WLR_EDGE_RIGHT) {
		new_right = border_x;
		if (new_right <= new_left) {
			new_right = new_left + 1;
		}
	}
	int new_width = new_right - new_left;
	int new_height = new_bottom - new_top;

	struct wlr_box geo_box = comp_toplevel_get_geometry(toplevel);
	comp_toplevel_set_position(toplevel, new_left - geo_box.x,
							   new_top - geo_box.y);

	// Don't allow resizing fixed sized toplevels
	int max_width, max_height, min_width, min_height;
	comp_toplevel_get_constraints(toplevel, &min_width, &max_width, &min_height,
								  &max_height);
	if (min_width != 0 && min_height != 0 &&
		(min_width == max_width || min_height == max_height)) {
		return;
	}

	// Respect minimum and maximum sizes
	if (max_width) {
		new_width = MIN(max_width, new_width);
	}
	if (min_width) {
		new_width = MAX(min_width, new_width);
	}
	if (max_height) {
		new_height = MIN(max_height, new_height);
	}
	if (min_height) {
		new_height = MAX(min_height, new_height);
	}

	comp_toplevel_set_size(toplevel, new_width, new_height);
	comp_toplevel_mark_dirty(toplevel);
}

uint32_t
comp_toplevel_get_edge_from_cursor_coords(struct comp_toplevel *toplevel,
										  struct comp_cursor *cursor) {
	uint32_t edge = 0;
	if (toplevel->decorated_size.width == 0 ||
		toplevel->decorated_size.height == 0) {
		return edge;
	}

	int lx, ly;
	wlr_scene_node_coords(&toplevel->object.scene_tree->node, &lx, &ly);

	const double y =
		MAX(0, (cursor->wlr_cursor->y - ly) / toplevel->decorated_size.height);
	if (y > 0.5) {
		edge |= WLR_EDGE_BOTTOM;
	} else if (y < 0.5) {
		edge |= WLR_EDGE_TOP;
	}

	const double x =
		MAX(0, (cursor->wlr_cursor->x - lx) / toplevel->decorated_size.width);
	if (x > 0.5) {
		edge |= WLR_EDGE_RIGHT;
	} else if (x < 0.5) {
		edge |= WLR_EDGE_LEFT;
	}

	return edge;
}

void comp_toplevel_begin_interactive(struct comp_toplevel *toplevel,
									 enum comp_cursor_mode mode,
									 uint32_t edges) {
	/* This function sets up an interactive move or resize operation, where the
	 * compositor stops propegating pointer events to clients and instead
	 * consumes them itself, to move or resize windows. */
	struct comp_server *server = toplevel->server;
	struct wlr_surface *focused_surface =
		server->seat->wlr_seat->pointer_state.focused_surface;
	/* Deny move/resize requests from unfocused clients. */
	struct wlr_surface *toplevel_surface =
		comp_toplevel_get_wlr_surface(toplevel);
	if (focused_surface &&
		toplevel_surface != wlr_surface_get_root_surface(focused_surface)) {
		return;
	}

	server->seat->grabbed_toplevel = toplevel;
	server->seat->cursor->cursor_mode = mode;

	switch (mode) {
	case COMP_CURSOR_PASSTHROUGH:
		break;
	case COMP_CURSOR_MOVE:;
		if (toplevel_surface) {
			comp_seat_surface_focus(&toplevel->object, toplevel_surface);
		}

		// Adjust the toplevel coordinates to be root-relative
		struct wlr_box output_box;
		wlr_output_layout_get_box(server->output_layout,
								  toplevel->state.workspace->output->wlr_output,
								  &output_box);
		server->seat->grab_x = server->seat->cursor->wlr_cursor->x -
							   toplevel->object.scene_tree->node.x -
							   output_box.x;
		server->seat->grab_y = server->seat->cursor->wlr_cursor->y -
							   toplevel->object.scene_tree->node.y -
							   output_box.y;

		if (toplevel->tiling_mode == COMP_TILING_MODE_TILED) {
			tiling_node_move_start(toplevel);
		}
		break;
	case COMP_CURSOR_RESIZE:;
		if (toplevel_surface) {
			comp_seat_surface_focus(&toplevel->object, toplevel_surface);
		}

		struct wlr_box geo_box = comp_toplevel_get_geometry(toplevel);

		double border_x = (toplevel->object.scene_tree->node.x + geo_box.x) +
						  ((edges & WLR_EDGE_RIGHT) ? geo_box.width : 0);
		double border_y = (toplevel->object.scene_tree->node.y + geo_box.y) +
						  ((edges & WLR_EDGE_BOTTOM) ? geo_box.height : 0);
		server->seat->grab_x = server->seat->cursor->wlr_cursor->x - border_x;
		server->seat->grab_y = server->seat->cursor->wlr_cursor->y - border_y;

		server->seat->grab_geobox = geo_box;
		server->seat->grab_geobox.x += toplevel->object.scene_tree->node.x;
		server->seat->grab_geobox.y += toplevel->object.scene_tree->node.y;

		server->seat->resize_edges = edges;

		comp_toplevel_set_resizing(toplevel, true);
		if (toplevel->tiling_mode == COMP_TILING_MODE_FLOATING) {
			comp_toplevel_set_size(toplevel, geo_box.width, geo_box.height);
			comp_toplevel_mark_dirty(toplevel);
		} else {
			tiling_node_resize_start(toplevel);
		}
		break;
	}
}

struct wlr_scene_tree *comp_toplevel_get_layer(struct comp_toplevel *toplevel) {
	assert(toplevel->state.workspace);
	switch (toplevel->state.workspace->type) {
	case COMP_WORKSPACE_TYPE_FULLSCREEN:
		if (toplevel->fullscreen) {
			return toplevel->state.workspace->layers.lower;
		}
		// Always float sub toplevels
		return toplevel->state.workspace->layers.floating;
	case COMP_WORKSPACE_TYPE_REGULAR:
		switch (toplevel->tiling_mode) {
		case COMP_TILING_MODE_FLOATING:
			return toplevel->state.workspace->layers.floating;
		case COMP_TILING_MODE_TILED:
			return toplevel->state.workspace->layers.lower;
		}
		break;
	}

	return NULL;
}

static void iter_scene_buffers_apply_effects(struct wlr_scene_buffer *buffer,
											 int sx, int sy, void *user_data) {
	struct wlr_scene_surface *scene_surface =
		wlr_scene_surface_try_from_buffer(buffer);
	if (!scene_surface || !user_data) {
		return;
	}

	struct comp_toplevel *toplevel = user_data;
	switch (toplevel->type) {
	case COMP_TOPLEVEL_TYPE_XDG:;
		struct wlr_xdg_surface *xdg_surface;
		if (!(xdg_surface = wlr_xdg_surface_try_from_wlr_surface(
				  scene_surface->surface)) ||
			xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
			return;
		}
		break;
	case COMP_TOPLEVEL_TYPE_XWAYLAND:
		// TODO: Check here
		break;
	}

	bool has_effects = !toplevel->fullscreen;

	// TODO: Be able to set whole decoration_data instead of calling
	// each individually?

	// Toplevel
	wlr_scene_buffer_set_opacity(buffer, has_effects ? toplevel->opacity : 1);
	wlr_scene_buffer_set_corner_radius(
		buffer, has_effects ? toplevel->corner_radius : 0);

	// Titlebar
	struct comp_widget *titlebar_widget = &toplevel->titlebar->widget;
	struct wlr_scene_buffer *titlebar_buffer = titlebar_widget->scene_buffer;
	comp_titlebar_refresh_corner_radii(toplevel->titlebar);
	wlr_scene_buffer_set_corner_radius(
		titlebar_buffer, has_effects ? titlebar_widget->corner_radius : 0);
	wlr_scene_buffer_set_shadow_data(titlebar_buffer,
									 titlebar_widget->shadow_data);
}

void comp_toplevel_mark_effects_dirty(struct comp_toplevel *toplevel) {
	wlr_scene_node_for_each_buffer(&toplevel->toplevel_scene_tree->node,
								   iter_scene_buffers_apply_effects, toplevel);
	comp_toplevel_mark_dirty(toplevel);
}

void comp_toplevel_move_into_parent_tree(struct comp_toplevel *toplevel,
										 struct wlr_scene_tree *parent) {
	if (!parent) {
		// Move back out of the parent tree
		struct wlr_scene_tree *layer = comp_toplevel_get_layer(toplevel);
		if (toplevel->object.scene_tree->node.parent != layer) {
			wlr_scene_node_reparent(&toplevel->object.scene_tree->node, layer);
		}
		return;
	}

	wlr_scene_node_reparent(&toplevel->object.scene_tree->node, parent);
}

char *comp_toplevel_get_title(struct comp_toplevel *toplevel) {
	if (toplevel->impl && toplevel->impl->get_title) {
		return toplevel->impl->get_title(toplevel);
	}

	return NULL;
}

bool comp_toplevel_get_always_floating(struct comp_toplevel *toplevel) {
	if (toplevel->impl && toplevel->impl->get_always_floating) {
		return toplevel->impl->get_always_floating(toplevel);
	}

	return false;
}

struct wlr_scene_tree *
comp_toplevel_get_parent_tree(struct comp_toplevel *toplevel) {
	if (toplevel->impl && toplevel->impl->get_parent_tree) {
		return toplevel->impl->get_parent_tree(toplevel);
	}

	return NULL;
}

struct wlr_surface *
comp_toplevel_get_wlr_surface(struct comp_toplevel *toplevel) {
	if (toplevel->impl && toplevel->impl->get_wlr_surface) {
		return toplevel->impl->get_wlr_surface(toplevel);
	}

	return NULL;
}

struct wlr_box comp_toplevel_get_geometry(struct comp_toplevel *toplevel) {
	struct wlr_box box = {0};
	if (toplevel->impl && toplevel->impl->get_geometry) {
		box = toplevel->impl->get_geometry(toplevel);
	}

	return box;
}

void comp_toplevel_get_constraints(struct comp_toplevel *toplevel,
								   int *min_width, int *max_width,
								   int *min_height, int *max_height) {
	if (toplevel->impl && toplevel->impl->get_constraints) {
		toplevel->impl->get_constraints(toplevel, min_width, max_width,
										min_height, max_height);
	}
}

void comp_toplevel_configure(struct comp_toplevel *toplevel, int width,
							 int height, int x, int y) {
	// Offset the configure events coordinates to be relative to the workspace,
	// not the parent
	if (toplevel->parent_tree) {
		int lx, ly;
		wlr_scene_node_coords(&toplevel->parent_tree->node, &lx, &ly);
		x += lx;
		y += ly;
	}

	if (toplevel->impl && toplevel->impl->configure) {
		toplevel->impl->configure(toplevel, width, height, x, y);
	}
}

void comp_toplevel_set_activated(struct comp_toplevel *toplevel, bool state) {
	if (toplevel->impl && toplevel->impl->set_activated) {
		toplevel->impl->set_activated(toplevel, state);
	}
}

void comp_toplevel_set_fullscreen(struct comp_toplevel *toplevel, bool state) {
	if (toplevel->fullscreen == state ||
		!comp_toplevel_can_fullscreen(toplevel)) {
		return;
	}

	// HACK: Come up with a way of restoring to tiled state
	if (state) {
		comp_toplevel_set_tiled(toplevel, false);
	}
	toplevel->fullscreen = state;

	if (toplevel->impl && toplevel->impl->set_fullscreen) {
		toplevel->impl->set_fullscreen(toplevel, state);
	}
	// TODO: Also call foreign toplevel fullscreen function

	if (state) {
		// Save the floating state
		save_state(toplevel);

		// Create a new neighbouring fullscreen workspace
		struct comp_workspace *fs_ws = comp_output_new_workspace(
			toplevel->state.workspace->output, COMP_WORKSPACE_TYPE_FULLSCREEN);

		fs_ws->fullscreen_toplevel = toplevel;

		comp_workspace_move_toplevel_to(fs_ws, toplevel);
	} else {
		if (toplevel->state.workspace->type == COMP_WORKSPACE_TYPE_FULLSCREEN) {
			toplevel->state.workspace->fullscreen_toplevel = NULL;

			// Restore the floating state
			restore_state(toplevel);
		}
	}

	// Update the output
	comp_output_arrange_output(toplevel->state.workspace->output);
}

void comp_toplevel_toggle_fullscreen(struct comp_toplevel *toplevel) {
	comp_toplevel_set_fullscreen(toplevel, !toplevel->fullscreen);
}

bool comp_toplevel_can_fullscreen(struct comp_toplevel *toplevel) {
	// Don't allow resizing fixed sized toplevels
	int max_width, max_height, min_width, min_height;
	comp_toplevel_get_constraints(toplevel, &min_width, &max_width, &min_height,
								  &max_height);
	if (min_width != 0 && min_height != 0 &&
		(min_width == max_width || min_height == max_height)) {
		return false;
	}

	return true;
}

bool comp_toplevel_get_is_fullscreen(struct comp_toplevel *toplevel) {
	if (toplevel->impl && toplevel->impl->get_is_fullscreen) {
		return toplevel->impl->get_is_fullscreen(toplevel);
	}

	return false;
}

void comp_toplevel_set_tiled(struct comp_toplevel *toplevel, bool state) {
	if (state && toplevel->fullscreen) {
		wlr_log(WLR_DEBUG, "Skipping tiling fullscreen toplevel");
		return;
	}

	const bool is_floating = toplevel->tiling_mode == COMP_TILING_MODE_FLOATING;

	toplevel->tiling_mode =
		state ? COMP_TILING_MODE_TILED : COMP_TILING_MODE_FLOATING;

	if (comp_toplevel_get_always_floating(toplevel)) {
		comp_toplevel_set_size(toplevel, toplevel->natural_width,
							   toplevel->natural_height);
		center_toplevel(toplevel, false);
		comp_toplevel_mark_dirty(toplevel);
		return;
	}

	// Switch layer tree
	comp_toplevel_move_into_parent_tree(toplevel, NULL);

	if (state && !toplevel->tiling_node) {
		tiling_node_add_toplevel(toplevel, is_floating);
	} else if (!state && toplevel->tiling_node) {
		tiling_node_remove_toplevel(toplevel);
		// Center the toplevel
		if (toplevel->dragging_tiled) {
			// Limit to the outputs usable area
			struct wlr_box *usable_area =
				&toplevel->state.workspace->output->usable_area;
			const int WIDTH =
				MIN(toplevel->state.width * TOPLEVEL_TILED_DRAG_SIZE,
					usable_area->width * 0.5) -
				BORDER_WIDTH * 2;
			const int HEIGHT =
				MIN(toplevel->state.height * TOPLEVEL_TILED_DRAG_SIZE,
					usable_area->height * 0.5) -
				toplevel->decorated_size.top_border_height - BORDER_WIDTH;
			comp_toplevel_set_size(toplevel, WIDTH, HEIGHT);
		} else {
			comp_toplevel_set_size(toplevel, toplevel->natural_width,
								   toplevel->natural_height);
		}
		center_toplevel(toplevel, toplevel->dragging_tiled);
		comp_toplevel_mark_dirty(toplevel);
	}

	if (toplevel->impl && toplevel->impl->set_tiled) {
		toplevel->impl->set_tiled(toplevel, state);
	}
}

void comp_toplevel_toggle_tiled(struct comp_toplevel *toplevel) {
	comp_toplevel_set_tiled(toplevel,
							toplevel->tiling_mode == COMP_TILING_MODE_FLOATING);
}

void comp_toplevel_set_pid(struct comp_toplevel *toplevel) {
	if (toplevel->impl && toplevel->impl->set_pid) {
		toplevel->impl->set_pid(toplevel);
	}
}

void comp_toplevel_set_size(struct comp_toplevel *toplevel, int width,
							int height) {
	// Fixes the size sometimes being negative when resizing tiled toplevels
	width = MAX(0, width);
	height = MAX(0, height);

	toplevel->state.width = width;
	toplevel->state.height = height;

	// Set decoration size
	toplevel->decorated_size.width = width + 2 * BORDER_WIDTH;
	toplevel->decorated_size.height = height + 2 * BORDER_WIDTH;

	struct comp_titlebar *titlebar = toplevel->titlebar;
	comp_titlebar_calculate_bar_height(titlebar);
	toplevel->decorated_size.top_border_height = BORDER_WIDTH;
	if (comp_titlebar_should_be_shown(toplevel)) {
		toplevel->decorated_size.height += toplevel->titlebar->bar_height;
		toplevel->decorated_size.top_border_height +=
			toplevel->titlebar->bar_height;
	}

	if (toplevel->impl && toplevel->impl->set_size) {
		toplevel->impl->set_size(toplevel, width, height);
	}

	comp_toplevel_center_and_clip(toplevel);
}

void comp_toplevel_set_resizing(struct comp_toplevel *toplevel, bool state) {
	if (toplevel && toplevel->impl && toplevel->impl->set_resizing) {
		toplevel->impl->set_resizing(toplevel, state);
	}
}

void comp_toplevel_mark_dirty(struct comp_toplevel *toplevel) {
	wlr_scene_node_set_enabled(&toplevel->decoration_scene_tree->node,
							   !toplevel->fullscreen);

	struct comp_titlebar *titlebar = toplevel->titlebar;

	if (toplevel->impl && toplevel->impl->marked_dirty_cb) {
		toplevel->impl->marked_dirty_cb(toplevel);
	}

	if (!toplevel->fullscreen) {
		bool show_full_titlebar = comp_titlebar_should_be_shown(toplevel);

		// Only redraw the titlebar if the size has changed
		if (toplevel->titlebar &&
			(titlebar->widget.width != toplevel->decorated_size.width ||
			 titlebar->widget.height != toplevel->decorated_size.height)) {
			comp_widget_draw_resize(&titlebar->widget,
									toplevel->decorated_size.width,
									toplevel->decorated_size.height);
			// Position the titlebar above the window
			wlr_scene_node_set_position(
				&titlebar->widget.object.scene_tree->node, -BORDER_WIDTH,
				-toplevel->decorated_size.top_border_height);

			// Adjust edges
			for (size_t i = 0; i < NUMBER_OF_RESIZE_TARGETS; i++) {
				struct comp_resize_edge *edge = toplevel->edges[i];
				wlr_scene_node_set_enabled(
					&edge->widget.object.scene_tree->node, show_full_titlebar);
				int width, height, x, y;
				comp_resize_edge_get_geometry(edge, &width, &height, &x, &y);

				comp_widget_draw_resize(&edge->widget, width, height);
				wlr_scene_node_set_position(
					&edge->widget.object.scene_tree->node, x, y);
			}
		}
	} else {
		comp_toplevel_set_position(toplevel, 0, 0);
	}
}

void comp_toplevel_center_and_clip(struct comp_toplevel *toplevel) {
	wlr_scene_node_set_position(&toplevel->toplevel_scene_tree->node, 0, 0);

	struct wlr_box clip = {
		.width = toplevel->state.width,
		.height = toplevel->state.height,
		.x = toplevel->geometry.x,
		.y = toplevel->geometry.y,
	};
	wlr_scene_subsurface_tree_set_clip(&toplevel->toplevel_scene_tree->node,
									   toplevel->fullscreen ? NULL : &clip);
}

void comp_toplevel_set_position(struct comp_toplevel *toplevel, int x, int y) {
	toplevel->state.x = x;
	toplevel->state.y = y;
	wlr_scene_node_set_position(&toplevel->object.scene_tree->node, x, y);

	comp_toplevel_configure(toplevel, toplevel->state.width,
							toplevel->state.height, x, y);
}

void comp_toplevel_close(struct comp_toplevel *toplevel) {
	if (toplevel->impl && toplevel->impl->close) {
		toplevel->impl->close(toplevel);
	}
}

void comp_toplevel_destroy(struct comp_toplevel *toplevel) {
	// Only destroy if no parent or if the parent hasn't been destroyed yet
	if (!toplevel->parent_tree ||
		(toplevel->parent_tree && !toplevel->parent_tree->node.data)) {
		wlr_scene_node_destroy(&toplevel->object.scene_tree->node);
	}

	free(toplevel);
}

struct comp_toplevel *
comp_toplevel_init(struct comp_output *output, struct comp_workspace *workspace,
				   enum comp_toplevel_type type,
				   enum comp_tiling_mode tiling_mode, bool fullscreen,
				   const struct comp_toplevel_impl *impl) {
	struct comp_toplevel *toplevel = calloc(1, sizeof(*toplevel));
	if (!toplevel) {
		wlr_log(WLR_ERROR, "Could not allocate comp_toplevel");
		return NULL;
	}
	toplevel->server = &server;
	toplevel->type = type;
	toplevel->using_csd = false;
	toplevel->fullscreen = fullscreen;
	toplevel->impl = impl;

	/* Set the scene_nodes decoration data */
	toplevel->opacity = 1;
	toplevel->corner_radius = EFFECTS_CORNER_RADII;
	toplevel->shadow_data.enabled = true;
	toplevel->shadow_data.color =
		wlr_render_color_from_color(&(const uint32_t){TOPLEVEL_SHADOW_COLOR});
	toplevel->shadow_data.blur_sigma = TOPLEVEL_SHADOW_BLUR_SIGMA;
	toplevel->shadow_data.offset_x = TOPLEVEL_SHADOW_X_OFFSET;
	toplevel->shadow_data.offset_y = TOPLEVEL_SHADOW_Y_OFFSET;

	toplevel->dragging_tiled = false;
	toplevel->tiling_mode = tiling_mode;
	toplevel->state.workspace = workspace;
	struct wlr_scene_tree *tree = comp_toplevel_get_layer(toplevel);
	toplevel->object.scene_tree = alloc_tree(tree);

	toplevel->object.scene_tree->node.data = &toplevel->object;
	toplevel->object.data = toplevel;
	toplevel->object.type = COMP_OBJECT_TYPE_TOPLEVEL;

	toplevel->decoration_scene_tree = alloc_tree(toplevel->object.scene_tree);

	// Initialize saved position/size
	toplevel->saved_state.x = 0;
	toplevel->saved_state.y = 0;
	toplevel->saved_state.width = 0;
	toplevel->saved_state.height = 0;

	/*
	 * Decorations
	 */

	// Titlebar
	toplevel->titlebar = comp_titlebar_init(toplevel->server, toplevel);
	// Resize borders
	const enum xdg_toplevel_resize_edge edges[NUMBER_OF_RESIZE_TARGETS] = {
		XDG_TOPLEVEL_RESIZE_EDGE_TOP,
		XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM,
		XDG_TOPLEVEL_RESIZE_EDGE_LEFT,
		XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT,
		XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT,
		XDG_TOPLEVEL_RESIZE_EDGE_RIGHT,
		XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT,
		XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT,
	};
	for (size_t i = 0; i < NUMBER_OF_RESIZE_TARGETS; i++) {
		toplevel->edges[i] = comp_resize_edge_init(&server, toplevel, edges[i]);
	}

	return toplevel;
}

static inline void set_natural_size(struct comp_toplevel *toplevel) {
	struct wlr_box box = toplevel->state.workspace->output->usable_area;

	struct wlr_box geometry = comp_toplevel_get_geometry(toplevel);
	toplevel->natural_width =
		MAX(TOPLEVEL_MIN_WIDTH, MIN(geometry.width, box.width * 0.5));
	toplevel->natural_height =
		MAX(TOPLEVEL_MIN_HEIGHT, MIN(geometry.height, box.height * 0.75));

	comp_toplevel_set_size(toplevel, toplevel->natural_width,
						   toplevel->natural_height);
}

/*
 * Implementation generic functions
 */

void comp_toplevel_generic_map(struct comp_toplevel *toplevel) {
	struct comp_workspace *ws = toplevel->state.workspace;

	comp_toplevel_set_pid(toplevel);

	wlr_scene_node_set_enabled(&toplevel->object.scene_tree->node, true);

	toplevel->fullscreen = comp_toplevel_get_is_fullscreen(toplevel);
	if (toplevel->fullscreen) {
		toplevel->tiling_mode = COMP_TILING_MODE_TILED;
	} else if (comp_toplevel_get_always_floating(toplevel) ||
			   ws->fullscreen_toplevel) {
		toplevel->tiling_mode = COMP_TILING_MODE_FLOATING;
	}

	// Move into parent tree if there's a parent
	toplevel->parent_tree = comp_toplevel_get_parent_tree(toplevel);
	comp_toplevel_move_into_parent_tree(toplevel, toplevel->parent_tree);

	comp_toplevel_mark_effects_dirty(toplevel);

	// Open new floating toplevels in the center of the output/parent
	// If tiling, save the centered state so untiling would center
	set_natural_size(toplevel);
	center_toplevel(toplevel, false);
	save_state(toplevel);

	// Tile/float the new toplevel
	if (ws->type == COMP_WORKSPACE_TYPE_REGULAR &&
		toplevel->tiling_mode == COMP_TILING_MODE_TILED) {
		comp_toplevel_set_tiled(toplevel, true);
	} else {
		comp_toplevel_set_tiled(toplevel, false);
	}

	wl_list_insert(&ws->toplevels, &toplevel->workspace_link);
	wl_list_insert(server.seat->focus_order.prev, &toplevel->focus_link);

	wlr_scene_node_set_enabled(&toplevel->object.scene_tree->node, true);
	comp_seat_surface_focus(&toplevel->object,
							comp_toplevel_get_wlr_surface(toplevel));

	comp_toplevel_mark_dirty(toplevel);
}

void comp_toplevel_generic_unmap(struct comp_toplevel *toplevel) {
	if (toplevel->fullscreen) {
		comp_toplevel_set_fullscreen(toplevel, false);
	}

	wlr_scene_node_set_enabled(&toplevel->object.scene_tree->node, false);

	/* Reset the cursor mode if the grabbed toplevel was unmapped. */
	if (toplevel == toplevel->server->seat->grabbed_toplevel) {
		comp_cursor_reset_cursor_mode(toplevel->server->seat);
	}

	if (toplevel->tiling_mode == COMP_TILING_MODE_TILED) {
		tiling_node_remove_toplevel(toplevel);
	}

	// Focus parent toplevel if applicable
	struct comp_toplevel *parent_toplevel = NULL;
	if (toplevel->parent_tree) {
		struct comp_object *parent = toplevel->parent_tree->node.data;
		if (parent && parent->type == COMP_OBJECT_TYPE_TOPLEVEL) {
			parent_toplevel = parent->data;
		}
	}
	// Only focus the previous toplevel if the unmapped toplevel doesn't have a
	// parent
	comp_seat_surface_unfocus(comp_toplevel_get_wlr_surface(toplevel),
							  parent_toplevel == NULL);
	if (parent_toplevel) {
		comp_seat_surface_focus(&parent_toplevel->object,
								comp_toplevel_get_wlr_surface(parent_toplevel));
	}

	wl_list_remove(&toplevel->workspace_link);
	wl_list_remove(&toplevel->focus_link);
}

void comp_toplevel_generic_commit(struct comp_toplevel *toplevel) {
	struct wlr_box new_geo = comp_toplevel_get_geometry(toplevel);

	bool new_size = new_geo.width != toplevel->state.width ||
					new_geo.height != toplevel->state.height ||
					new_geo.x != toplevel->geometry.x ||
					new_geo.y != toplevel->geometry.y;
	if (new_size) {
		toplevel->geometry = new_geo;
		if (toplevel->tiling_mode == COMP_TILING_MODE_FLOATING) {
			comp_toplevel_set_size(toplevel, new_geo.width, new_geo.height);
			comp_toplevel_mark_dirty(toplevel);
		}

		comp_toplevel_center_and_clip(toplevel);
	}
}
