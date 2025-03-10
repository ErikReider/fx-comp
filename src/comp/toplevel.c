#include <assert.h>
#include <float.h>
#include <glib.h>
#include <scenefx/types/fx/corner_location.h>
#include <scenefx/types/wlr_scene.h>
#include <stdbool.h>
#include <stdio.h>
#include <wayland-util.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>

#include "comp/animation_mgr.h"
#include "comp/object.h"
#include "comp/output.h"
#include "comp/saved_object.h"
#include "comp/server.h"
#include "comp/tiling_node.h"
#include "comp/transaction.h"
#include "comp/widget.h"
#include "comp/workspace.h"
#include "constants.h"
#include "desktop/toplevel.h"
#include "desktop/widgets/resize_edge.h"
#include "desktop/widgets/titlebar.h"
#include "seat/cursor.h"
#include "seat/seat.h"
#include "util.h"

/*
 * Animations
 */

/** Get the dest/initial toplevel size for the scale (un)map animation */
static struct comp_toplevel_state
get_open_close_small_state(struct comp_toplevel_state pending) {
	const int small_width = pending.width / 2;
	const int small_height = pending.height / 2;
	return (struct comp_toplevel_state){
		.x = pending.x + (pending.width - small_width) / 2,
		.y = pending.y + (pending.height - small_height) / 2,
		.width = small_width,
		.height = small_height,
	};
}

static void cancel_all_resize_animations(struct comp_toplevel *toplevel) {
	comp_animation_client_cancel(server.animation_mgr,
								 toplevel->anim.open_close.client);
	comp_animation_client_cancel(server.animation_mgr,
								 toplevel->anim.resize.client);
}

/* Open/Close Animation */

void comp_toplevel_add_open_close_animation(
	struct comp_toplevel *toplevel, struct animation_open_close_data from,
	struct animation_open_close_data to) {

	cancel_all_resize_animations(toplevel);

	toplevel->anim.open_close.fade_opacity = from.opacity;
	toplevel->anim.open_close.from = from;
	toplevel->anim.open_close.to = to;

	comp_animation_client_add(server.animation_mgr,
							  toplevel->anim.open_close.client, true);
}

static void open_close_animation_update(struct comp_animation_mgr *mgr,
										struct comp_animation_client *client) {
	struct comp_toplevel *toplevel = client->data;
	wlr_scene_node_set_enabled(&toplevel->object.scene_tree->node, true);

	const float progress = ease_out_cubic(client->progress);

	const float opacity = lerp(toplevel->anim.open_close.from.opacity,
							   toplevel->anim.open_close.to.opacity, progress);
	toplevel->anim.open_close.fade_opacity = opacity;

	const struct comp_toplevel_state *from_state =
		&toplevel->anim.open_close.from.state;
	const struct comp_toplevel_state *to_state =
		&toplevel->anim.open_close.to.state;

	int x = from_state->x + (to_state->x - from_state->x) * progress;
	int y = from_state->y + (to_state->y - from_state->y) * progress;
	int width =
		from_state->width + (to_state->width - from_state->width) * progress;
	int height =
		from_state->height + (to_state->height - from_state->height) * progress;

	comp_toplevel_set_size(toplevel, width, height);
	comp_toplevel_set_position(toplevel, x, y);
	comp_toplevel_refresh(toplevel, false);
}

static void open_close_animation_done(struct comp_animation_mgr *mgr,
									  struct comp_animation_client *client,
									  bool cancelled) {
	struct comp_toplevel *toplevel = client->data;
	comp_toplevel_remove_buffer(toplevel);
	toplevel->anim.open_close.fade_opacity =
		toplevel->anim.open_close.to.opacity;

	comp_toplevel_refresh_titlebar_effects(toplevel);

	// Continue destroying the toplevel
	if (toplevel->object.destroying) {
		comp_toplevel_destroy(toplevel);
	}
}

const struct comp_animation_client_impl open_close_animation_impl = {
	.done = open_close_animation_done,
	.update = open_close_animation_update,
};

/* Resize Animation */

void comp_toplevel_add_size_animation(struct comp_toplevel *toplevel,
									  struct comp_toplevel_state from,
									  struct comp_toplevel_state to) {
	// Skip animation if there's no difference. Avoids the issue that the
	// transaction will ignore commits where there's no size difference
	if (comp_toplevel_state_is_same(&to, &toplevel->state) ||
		(comp_toplevel_state_is_same(&from, &toplevel->anim.resize.from) &&
		 comp_toplevel_state_is_same(&to, &toplevel->anim.resize.to))) {
		return;
	}

	bool run_now = false;
	// Fixes XDG toplevels not running the animation if the size is constant,
	// but the position needs to change (don't wait until the matching commit)
	if (comp_toplevel_state_same_size(&to, &toplevel->state) &&
		!comp_toplevel_state_same_pos(&to, &toplevel->state)) {
		run_now = true;
	}

	cancel_all_resize_animations(toplevel);

	// Save the initial buffer
	comp_toplevel_refresh_titlebar_effects(toplevel);
	comp_toplevel_save_buffer(toplevel);

	toplevel->anim.resize.crossfade_opacity = 1.0f;
	toplevel->anim.resize.from = from;
	toplevel->anim.resize.to = to;

	// Wait until the surface has commited with the new size
	comp_animation_client_add(server.animation_mgr,
							  toplevel->anim.resize.client, run_now);
	toplevel->pending_state = to;
	comp_object_mark_dirty(&toplevel->object);
	comp_transaction_commit_dirty(true);
}

static void resize_animation_update(struct comp_animation_mgr *mgr,
									struct comp_animation_client *client) {
	struct comp_toplevel *toplevel = client->data;
	if (toplevel->unmapped || toplevel->object.destroying) {
		return;
	}

	wlr_scene_node_set_enabled(&toplevel->toplevel_scene_tree->node, true);

	const float progress = ease_out_cubic(client->progress);
	int x = lerp(toplevel->anim.resize.from.x, toplevel->anim.resize.to.x,
				 progress);
	int y = lerp(toplevel->anim.resize.from.y, toplevel->anim.resize.to.y,
				 progress);
	int width = lerp(toplevel->anim.resize.from.width,
					 toplevel->anim.resize.to.width, progress);
	int height = lerp(toplevel->anim.resize.from.height,
					  toplevel->anim.resize.to.height, progress);
	toplevel->anim.resize.crossfade_opacity = lerp(1.0f, 0.0f, progress);

	comp_toplevel_set_size(toplevel, width, height);
	comp_toplevel_set_position(toplevel, x, y);
	comp_toplevel_refresh(toplevel, false);
}

static void resize_animation_done(struct comp_animation_mgr *mgr,
								  struct comp_animation_client *client,
								  bool cancelled) {
	struct comp_toplevel *toplevel = client->data;
	if (toplevel->unmapped || toplevel->object.destroying) {
		return;
	}

	toplevel->anim.resize.crossfade_opacity = 1.0f;
	comp_toplevel_remove_buffer(toplevel);
	comp_toplevel_refresh_titlebar_effects(toplevel);
}

const struct comp_animation_client_impl resize_animation_impl = {
	.done = resize_animation_done,
	.update = resize_animation_update,
};

static void save_state(struct comp_toplevel *toplevel,
					   struct comp_toplevel_state *state) {
	toplevel->saved_state.x = state->x;
	toplevel->saved_state.y = state->y;
	toplevel->saved_state.width = state->width;
	toplevel->saved_state.height = state->height;
}

static void restore_state(struct comp_toplevel *toplevel) {
	struct comp_output *output = toplevel->workspace->output;
	struct comp_workspace *fs_ws = toplevel->workspace;

	if (fs_ws->type == COMP_WORKSPACE_TYPE_FULLSCREEN) {
		struct comp_workspace *prev_ws = toplevel->saved_workspace;
		// Make sure that the workspace still exists...
		struct comp_workspace *pos, *ws = NULL;
		wl_list_for_each_reverse(pos, &output->workspaces, output_link) {
			if (pos == prev_ws) {
				ws = prev_ws;
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
			// Mark as dirty later
			if (toplevel_pos != toplevel) {
				comp_object_mark_dirty(&toplevel->object);
				comp_transaction_commit_dirty(true);
			}
		}
		comp_output_remove_workspace(output, fs_ws);
		comp_output_focus_workspace(output, ws);
	}

	comp_toplevel_state_print(&toplevel->saved_state, "LOAD:");
	comp_toplevel_set_position(toplevel, toplevel->saved_state.x,
							   toplevel->saved_state.y);
	comp_toplevel_set_size(toplevel, toplevel->saved_state.width,
						   toplevel->saved_state.height);
	comp_object_mark_dirty(&toplevel->object);
	comp_transaction_commit_dirty(true);

	toplevel->saved_state.x = 0;
	toplevel->saved_state.y = 0;
	toplevel->saved_state.width = 0;
	toplevel->saved_state.height = 0;
	toplevel->saved_workspace = NULL;
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
		wlr_output_layout_output_coords(server->output_layout,
										toplevel->workspace->output->wlr_output,
										&lx, &ly);
		// Let the animation adjust the position
		if (toplevel->anim.resize.client->state == ANIMATION_STATE_NONE) {
			comp_toplevel_set_position(toplevel, lx, ly);
		} else {
			toplevel->anim.resize.to.x = lx;
			toplevel->anim.resize.to.y = ly;
		}
		comp_object_mark_dirty(&toplevel->object);
		comp_transaction_commit_dirty(true);

		// Update floating toplevels current monitor and workspace.
		// Also raise the output node to the top so that it's floating toplevels
		// remain on top on other outputs (if they intersect)
		struct comp_output *new_output = find_output(toplevel);
		struct comp_workspace *ws;
		if (new_output && (ws = comp_output_get_active_ws(
							   new_output, toplevel->fullscreen))) {
			comp_workspace_move_toplevel_to(ws, toplevel);
			comp_object_mark_dirty(&toplevel->object);
			comp_transaction_commit_dirty(true);
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
	// Don't resize while fullscreen or animating
	if (toplevel->fullscreen ||
		toplevel->anim.resize.client->state != ANIMATION_STATE_NONE) {
		return;
	}

	switch (toplevel->tiling_mode) {
	case COMP_TILING_MODE_TILED:
		tiling_node_resize(toplevel);
		comp_transaction_commit_dirty(true);
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
	int x = new_left - geo_box.x;
	int y = new_top - geo_box.y;
	int width = toplevel->state.width;
	int height = toplevel->state.height;
	comp_toplevel_set_position(toplevel, x, y);

	// Don't allow resizing fixed sized toplevels
	int max_width, max_height, min_width, min_height;
	comp_toplevel_get_constraints(toplevel, &min_width, &max_width, &min_height,
								  &max_height);
	if (min_width != 0 && min_height != 0 &&
		(min_width == max_width || min_height == max_height)) {
		goto done;
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

	width = new_width;
	height = new_height;
	comp_toplevel_set_size(toplevel, width, height);

done:
	comp_object_mark_dirty(&toplevel->object);
	comp_transaction_commit_dirty(true);
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
	// Don't resize while animating
	if (mode == COMP_CURSOR_RESIZE &&
		toplevel->anim.resize.client->state != ANIMATION_STATE_NONE) {
		return;
	}
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
								  toplevel->workspace->output->wlr_output,
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
			comp_object_mark_dirty(&toplevel->object);
			comp_transaction_commit_dirty(true);
		} else {
			tiling_node_resize_start(toplevel);
		}
		break;
	}
}

struct wlr_scene_tree *comp_toplevel_get_layer(struct comp_toplevel *toplevel) {
	assert(toplevel->workspace);
	switch (toplevel->workspace->type) {
	case COMP_WORKSPACE_TYPE_FULLSCREEN:
		if (toplevel->fullscreen) {
			return toplevel->workspace->layers.lower;
		}
		// Always float sub toplevels
		return toplevel->workspace->layers.floating;
	case COMP_WORKSPACE_TYPE_REGULAR:
		switch (toplevel->tiling_mode) {
		case COMP_TILING_MODE_FLOATING:
			return toplevel->workspace->layers.floating;
		case COMP_TILING_MODE_TILED:
			return toplevel->workspace->layers.lower;
		}
		break;
	}

	return NULL;
}

void comp_toplevel_refresh_titlebar_effects(struct comp_toplevel *toplevel) {
	// Refresh titlebar, no need to step through the entire toplevel tree
	bool has_effects = !toplevel->fullscreen;
	struct comp_titlebar *titlebar = toplevel->titlebar;
	struct wlr_scene_buffer *buffer = titlebar->widget.scene_buffer;
	float opacity = 1;

	if (toplevel->anim.open_close.client->state == ANIMATION_STATE_RUNNING) {
		opacity *= toplevel->anim.open_close.fade_opacity;
	}
	wlr_scene_buffer_set_opacity(buffer, opacity);

	comp_titlebar_refresh_corner_radii(toplevel->titlebar);
	wlr_scene_buffer_set_corner_radius(
		buffer, has_effects ? titlebar->widget.corner_radius : 0,
		has_effects ? CORNER_LOCATION_ALL : CORNER_LOCATION_NONE);

	wlr_scene_buffer_set_backdrop_blur(
		buffer, has_effects && titlebar->widget.backdrop_blur);
	wlr_scene_buffer_set_backdrop_blur_optimized(
		buffer, titlebar->widget.backdrop_blur_optimized);
	wlr_scene_buffer_set_backdrop_blur_ignore_transparent(
		buffer, titlebar->widget.backdrop_blur_ignore_transparent);

	comp_widget_refresh_shadow(&titlebar->widget);
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

void comp_toplevel_center(struct comp_toplevel *toplevel, int width, int height,
						  bool center_on_cursor) {
	struct comp_toplevel_state original_state = toplevel->state;
	toplevel->state.width = width;
	toplevel->state.height = height;
	comp_toplevel_refresh_titlebar(toplevel);

	struct comp_workspace *ws = toplevel->workspace;

	double x = 0;
	double y = 0;
	if (center_on_cursor) {
		// Adjust for the output position
		x = server.seat->cursor->wlr_cursor->x -
			toplevel->decorated_size.width * 0.5;
		y = server.seat->cursor->wlr_cursor->y -
			toplevel->decorated_size.height * 0.5;
		wlr_output_layout_output_coords(server.output_layout,
										ws->output->wlr_output, &x, &y);
		// TODO: Center on titlebar when dragging from tiled?
	} else {
		struct wlr_box relative_box = {0};

		wlr_output_layout_get_box(toplevel->server->output_layout,
								  ws->output->wlr_output, &relative_box);
		x = (relative_box.width - toplevel->decorated_size.width) * 0.5;
		y = (relative_box.height - toplevel->decorated_size.height) * 0.5;
	}

	// Restore the original state
	toplevel->state = original_state;
	comp_toplevel_refresh_titlebar(toplevel);

	comp_toplevel_set_position(toplevel, x, y);
}

void comp_toplevel_save_buffer(struct comp_toplevel *toplevel) {
	if (toplevel->object.destroying) {
		return;
	}
	if (!wl_list_empty(&toplevel->saved_scene_tree->children)) {
		wlr_log(WLR_INFO, "Trying to save already saved buffer...");
		comp_toplevel_remove_buffer(toplevel);
	}

	wlr_scene_node_set_enabled(&toplevel->toplevel_scene_tree->node, true);
	wlr_scene_tree_snapshot(&toplevel->toplevel_scene_tree->node,
							toplevel->saved_scene_tree);

	wlr_scene_node_set_enabled(&toplevel->toplevel_scene_tree->node, false);
	wlr_scene_node_set_enabled(&toplevel->saved_scene_tree->node, true);
}

void comp_toplevel_remove_buffer(struct comp_toplevel *toplevel) {
	if (toplevel->unmapped || toplevel->object.destroying) {
		return;
	}
	if (!wl_list_empty(&toplevel->saved_scene_tree->children)) {
		struct wlr_scene_node *node, *tmp;
		wl_list_for_each_safe(node, tmp, &toplevel->saved_scene_tree->children,
							  link) {
			wlr_scene_node_destroy(node);
		}
	}
	wlr_scene_node_set_enabled(&toplevel->saved_scene_tree->node, false);
	wlr_scene_node_set_enabled(&toplevel->toplevel_scene_tree->node, true);
}

void comp_toplevel_set_minimized(struct comp_toplevel *toplevel, bool state) {
	if (toplevel->minimized == state) {
		return;
	}

	// HACK: Come up with a way of restoring to tiled state
	if (state) {
		comp_toplevel_set_tiled(toplevel, false, true);
	}
	toplevel->minimized = state;

	if (toplevel->impl && toplevel->impl->set_minimized) {
		toplevel->impl->set_minimized(toplevel, state);
	}

	if (state) {
		// Save the floating state when not fullscreen. The fullscreen logic
		// already saved the floating position
		if (toplevel->fullscreen) {
			comp_toplevel_set_fullscreen(toplevel, false, true);
			toplevel->fullscreen = true;
		} else {
			save_state(toplevel, &toplevel->pending_state);
		}
	} else {
		// Move to the focused workspace and output
		struct comp_output *output = get_active_output(&server);
		struct comp_workspace *workspace =
			comp_output_get_active_ws(output, false);
		if (workspace != toplevel->workspace) {
			comp_workspace_move_toplevel_to(workspace, toplevel);
		}

		// Restore fullscreen state
		if (toplevel->fullscreen) {
			comp_toplevel_set_fullscreen(toplevel, true, true);
		} else {
			restore_state(toplevel);
		}
	}

	// TODO: Minimize animation
	wlr_scene_node_set_enabled(&toplevel->object.scene_tree->node, !state);

	if (!toplevel->fullscreen) {
		comp_object_mark_dirty(&toplevel->object);
		comp_transaction_commit_dirty(true);
	}

	if (toplevel->wlr_foreign_toplevel) {
		wlr_foreign_toplevel_handle_v1_set_minimized(
			toplevel->wlr_foreign_toplevel, state);
	}
}

void comp_toplevel_set_fullscreen(struct comp_toplevel *toplevel, bool state,
								  bool force) {
	if ((toplevel->fullscreen == state && !force) ||
		!comp_toplevel_can_fullscreen(toplevel)) {
		return;
	}

	// HACK: Come up with a way of restoring to tiled state
	if (state) {
		comp_toplevel_set_tiled(toplevel, false, true);
	}
	toplevel->fullscreen = state;

	if (toplevel->impl && toplevel->impl->set_fullscreen) {
		toplevel->impl->set_fullscreen(toplevel, state);
	}

	if (state) {
		// Save the floating state
		save_state(toplevel, &toplevel->pending_state);

		// Create a new neighbouring fullscreen workspace
		struct comp_workspace *fs_ws = comp_output_new_workspace(
			toplevel->workspace->output, COMP_WORKSPACE_TYPE_FULLSCREEN);

		fs_ws->fullscreen_toplevel = toplevel;

		comp_workspace_move_toplevel_to(fs_ws, toplevel);
	} else {
		if (toplevel->workspace->type == COMP_WORKSPACE_TYPE_FULLSCREEN) {
			toplevel->workspace->fullscreen_toplevel = NULL;

			// Restore the floating state
			restore_state(toplevel);
		}
	}

	// Update the output
	comp_output_arrange_output(toplevel->workspace->output);

	if (toplevel->wlr_foreign_toplevel) {
		wlr_foreign_toplevel_handle_v1_set_fullscreen(
			toplevel->wlr_foreign_toplevel, state);
	}
}

void comp_toplevel_set_tiled(struct comp_toplevel *toplevel, bool state,
							 bool skip_remove_animation) {
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
		comp_toplevel_center(toplevel, toplevel->pending_state.width,
							 toplevel->pending_state.height, false);
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
				&toplevel->workspace->output->usable_area;
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
		comp_toplevel_center(toplevel, toplevel->pending_state.width,
							 toplevel->pending_state.height,
							 toplevel->dragging_tiled);

		if (!skip_remove_animation) {
			comp_toplevel_add_size_animation(toplevel, toplevel->state,
											 toplevel->pending_state);
		}
	}

	if (toplevel->impl && toplevel->impl->set_tiled) {
		toplevel->impl->set_tiled(toplevel, state);
	}
}

void comp_toplevel_refresh_titlebar(struct comp_toplevel *toplevel) {
	toplevel->decorated_size.width = toplevel->state.width + 2 * BORDER_WIDTH;
	toplevel->decorated_size.height = toplevel->state.height + 2 * BORDER_WIDTH;

	struct comp_titlebar *titlebar = toplevel->titlebar;
	if (!titlebar) {
		return;
	}
	comp_titlebar_calculate_bar_height(titlebar);
	toplevel->decorated_size.top_border_height = BORDER_WIDTH;
	if (comp_titlebar_should_be_shown(toplevel)) {
		toplevel->decorated_size.height += toplevel->titlebar->bar_height;
		toplevel->decorated_size.top_border_height +=
			toplevel->titlebar->bar_height;
	}
}

static void send_frame_done_iterator(struct wlr_scene_buffer *scene_buffer,
									 int x, int y, void *data) {
	struct timespec *when = data;
	wl_signal_emit_mutable(&scene_buffer->events.frame_done, when);
}

void comp_toplevel_send_frame_done(struct comp_toplevel *toplevel) {
	struct timespec when;
	clock_gettime(CLOCK_MONOTONIC, &when);

	struct wlr_scene_node *node;
	wl_list_for_each(node, &toplevel->toplevel_scene_tree->children, link) {
		wlr_scene_node_for_each_buffer(node, send_frame_done_iterator, &when);
	}
}

static void comp_toplevel_center_and_clip(struct comp_toplevel *toplevel,
										  struct wlr_box *clip) {
	if (toplevel->unmapped || !toplevel->toplevel_scene_tree) {
		return;
	}

	wlr_scene_node_set_position(&toplevel->toplevel_scene_tree->node, 0, 0);
	wlr_scene_node_set_position(&toplevel->saved_scene_tree->node, 0, 0);

	clip->width = MIN(toplevel->state.width, clip->width);
	clip->height = MIN(toplevel->state.height, clip->height);
	wlr_scene_subsurface_tree_set_clip(&toplevel->toplevel_scene_tree->node,
									   toplevel->fullscreen ? NULL : clip);
}

void comp_toplevel_transaction_timed_out(struct comp_toplevel *toplevel) {
	// Run the open/close animation if the first visible commit timed out
	if (!toplevel->object.destroying && toplevel->unmapped) {
		toplevel->unmapped = false;
		struct animation_open_close_data from = {
			.opacity = 0.0,
			.state = get_open_close_small_state(toplevel->pending_state),
		};
		struct animation_open_close_data to = {
			.opacity = 1.0,
			.state = toplevel->pending_state,
		};
		comp_toplevel_add_open_close_animation(toplevel, from, to);
	}
}

void comp_toplevel_refresh(struct comp_toplevel *toplevel,
						   bool is_instruction) {
	// Assume that there's a pending state. Update the decorations with said
	// pending state
	if (!is_instruction) {
		toplevel->state = toplevel->pending_state;
	}

	if (toplevel->impl && toplevel->impl->marked_dirty_cb) {
		toplevel->impl->marked_dirty_cb(toplevel);
	}

	// Set decoration size
	comp_toplevel_refresh_titlebar(toplevel);

	bool animating =
		toplevel->anim.resize.client->state != ANIMATION_STATE_NONE;
	if ((is_instruction && !animating) || !is_instruction) {
		wlr_scene_node_set_position(&toplevel->object.scene_tree->node,
									toplevel->state.x, toplevel->state.y);
	}

	struct wlr_box geometry = comp_toplevel_get_geometry(toplevel);
	comp_toplevel_center_and_clip(toplevel, &geometry);

	// Adjust edges
	for (size_t i = 0; i < NUMBER_OF_RESIZE_TARGETS; i++) {
		struct comp_resize_edge *edge = toplevel->edges[i];
		wlr_scene_node_set_enabled(&edge->widget.object.scene_tree->node,
								   !toplevel->fullscreen);
		if (toplevel->fullscreen) {
			continue;
		}
		int width, height, x, y;
		comp_resize_edge_get_geometry(edge, &width, &height, &x, &y);

		comp_widget_draw_resize(&edge->widget, width, height);
		wlr_scene_node_set_position(&edge->widget.object.scene_tree->node, x,
									y);
	}

	wlr_scene_node_set_enabled(&toplevel->decoration_scene_tree->node,
							   !toplevel->fullscreen);
	if (toplevel->fullscreen) {
		goto post_deocations;
	}

	// Only redraw the titlebar if the size has changed or there's a force
	// update
	struct comp_titlebar *titlebar = toplevel->titlebar;
	if (!is_instruction ||
		titlebar->widget.width != toplevel->decorated_size.width ||
		titlebar->widget.height != toplevel->decorated_size.height) {
		// Assume that the whole surface has changed
		if (!is_instruction) {
			titlebar->widget.width = toplevel->decorated_size.width;
			titlebar->widget.height = toplevel->decorated_size.height;
			comp_widget_draw_full(&titlebar->widget);
		} else {
			comp_widget_draw_resize(&titlebar->widget,
									toplevel->decorated_size.width,
									toplevel->decorated_size.height);
		}
		// Position the titlebar above the window
		wlr_scene_node_set_position(
			&titlebar->widget.object.scene_tree->node, -BORDER_WIDTH,
			-toplevel->decorated_size.top_border_height);
	}

post_deocations:
	comp_toplevel_refresh_titlebar_effects(toplevel);
}

/*
 * WLR Foreign handlers
 */

static void handle_wlr_foreign_activate_request(struct wl_listener *listener,
												void *data) {
	struct comp_toplevel *toplevel =
		wl_container_of(listener, toplevel, wlr_foreign_activate_request);
	struct wlr_foreign_toplevel_handle_v1_activated_event *event = data;

	if (server.seat->wlr_seat != event->seat) {
		wlr_log(WLR_ERROR,
				"Could not activate foreign toplevel, not the same seat");
		return;
	}

	// Un-minimize
	comp_toplevel_set_minimized(toplevel, false);

	comp_seat_surface_focus(&toplevel->object,
							comp_toplevel_get_wlr_surface(toplevel));
}

static void handle_wlr_foreign_minimize_request(struct wl_listener *listener,
												void *data) {
	struct comp_toplevel *toplevel =
		wl_container_of(listener, toplevel, wlr_foreign_minimize_request);
	struct wlr_foreign_toplevel_handle_v1_minimized_event *event = data;

	comp_toplevel_set_minimized(toplevel, event->minimized);
}

static void handle_wlr_foreign_fullscreen_request(struct wl_listener *listener,
												  void *data) {
	struct comp_toplevel *toplevel =
		wl_container_of(listener, toplevel, wlr_foreign_fullscreen_request);
	struct wlr_foreign_toplevel_handle_v1_fullscreen_event *event = data;

	// Ignore the event output hint
	// TODO: Use output hint in future
	comp_toplevel_set_fullscreen(toplevel, event->fullscreen, false);
}

static void handle_wlr_foreign_close_request(struct wl_listener *listener,
											 void *data) {
	struct comp_toplevel *toplevel =
		wl_container_of(listener, toplevel, wlr_foreign_close_request);

	comp_toplevel_close(toplevel);
}

static void handle_wlr_foreign_destroy(struct wl_listener *listener,
									   void *data) {
	struct comp_toplevel *toplevel =
		wl_container_of(listener, toplevel, wlr_foreign_destroy);

	listener_remove(&toplevel->wlr_foreign_activate_request);
	listener_remove(&toplevel->wlr_foreign_minimize_request);
	listener_remove(&toplevel->wlr_foreign_fullscreen_request);
	listener_remove(&toplevel->wlr_foreign_close_request);
	listener_remove(&toplevel->wlr_foreign_destroy);
}

/*
 * Toplevel
 */

void comp_toplevel_destroy(struct comp_toplevel *toplevel) {
	toplevel->object.destroying = true;
	if (toplevel->anim.open_close.client->state != ANIMATION_STATE_NONE) {
		wlr_log(WLR_DEBUG, "Delaying destroy until animation finishes");
		return;
	}

	comp_animation_client_destroy(toplevel->anim.open_close.client);
	comp_animation_client_destroy(toplevel->anim.resize.client);

	comp_saved_object_destroy(toplevel->saved_scene_tree->node.data);

	wlr_scene_node_destroy(&toplevel->object.scene_tree->node);

	free(toplevel);
}

struct comp_toplevel *
comp_toplevel_init(struct comp_output *output, struct comp_workspace *workspace,
				   enum comp_toplevel_type type,
				   enum comp_tiling_mode tiling_mode,
				   const struct comp_toplevel_impl *impl) {
	struct comp_toplevel *toplevel = calloc(1, sizeof(*toplevel));
	if (!toplevel) {
		wlr_log(WLR_ERROR, "Could not allocate comp_toplevel");
		return NULL;
	}
	toplevel->server = &server;
	toplevel->type = type;
	toplevel->using_csd = false;
	toplevel->fullscreen = false;
	toplevel->unmapped = true;
	toplevel->impl = impl;
	memset(toplevel->title, 0, sizeof(toplevel->title));

	/* Set the scene_nodes decoration data */
	toplevel->opacity = 1;
	toplevel->corner_radius = EFFECTS_CORNER_RADII;

	toplevel->dragging_tiled = false;
	toplevel->tiling_mode = tiling_mode;
	toplevel->workspace = workspace;
	struct wlr_scene_tree *tree = comp_toplevel_get_layer(toplevel);
	toplevel->object.scene_tree = alloc_tree(tree);
	toplevel->object.content_tree = alloc_tree(toplevel->object.scene_tree);

	toplevel->object.scene_tree->node.data = &toplevel->object;
	toplevel->object.data = toplevel;
	toplevel->object.type = COMP_OBJECT_TYPE_TOPLEVEL;
	toplevel->object.destroying = false;

	toplevel->saved_scene_tree = alloc_tree(toplevel->object.content_tree);
	toplevel->saved_scene_tree->node.data =
		comp_saved_object_init(&toplevel->object);
	toplevel->decoration_scene_tree = alloc_tree(toplevel->object.content_tree);

	// Initialize saved position/size
	toplevel->saved_state.x = 0;
	toplevel->saved_state.y = 0;
	toplevel->saved_state.width = 0;
	toplevel->saved_state.height = 0;

	toplevel->pending_state = (struct comp_toplevel_state){0};

	toplevel->anim.open_close.client = comp_animation_client_init(
		server.animation_mgr, TOPLEVEL_ANIMATION_OPEN_CLOSE_DURATION_MS,
		&open_close_animation_impl, toplevel);
	toplevel->anim.resize.client = comp_animation_client_init(
		server.animation_mgr, TOPLEVEL_ANIMATION_RESIZE_DURATION_MS,
		&resize_animation_impl, toplevel);

	/*
	 * Decorations
	 */

	// Titlebar
	toplevel->titlebar = comp_titlebar_init(toplevel->server, toplevel);
	assert(toplevel->titlebar);
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

/*
 * Implementation generic functions
 */

void comp_toplevel_generic_map(struct comp_toplevel *toplevel) {
	struct comp_workspace *ws = toplevel->workspace;

	// EXT Foreign protocol
	struct wlr_ext_foreign_toplevel_handle_v1_state foreign_toplevel_state = {
		.app_id = comp_toplevel_get_foreign_id(toplevel),
		.title = comp_toplevel_get_title(toplevel),
	};
	toplevel->ext_foreign_toplevel = wlr_ext_foreign_toplevel_handle_v1_create(
		server.ext_foreign_toplevel_list, &foreign_toplevel_state);
	comp_toplevel_refresh_ext_foreign_toplevel(toplevel);

	// WLR Foreign protocol
	toplevel->wlr_foreign_toplevel = wlr_foreign_toplevel_handle_v1_create(
		server.wlr_foreign_toplevel_manager);
	listener_connect_init(
		&toplevel->wlr_foreign_toplevel->events.request_activate,
		&toplevel->wlr_foreign_activate_request,
		handle_wlr_foreign_activate_request);
	listener_connect_init(
		&toplevel->wlr_foreign_toplevel->events.request_minimize,
		&toplevel->wlr_foreign_minimize_request,
		handle_wlr_foreign_minimize_request);
	listener_connect_init(
		&toplevel->wlr_foreign_toplevel->events.request_fullscreen,
		&toplevel->wlr_foreign_fullscreen_request,
		handle_wlr_foreign_fullscreen_request);
	listener_connect_init(&toplevel->wlr_foreign_toplevel->events.request_close,
						  &toplevel->wlr_foreign_close_request,
						  handle_wlr_foreign_close_request);
	listener_connect_init(&toplevel->wlr_foreign_toplevel->events.destroy,
						  &toplevel->wlr_foreign_destroy,
						  handle_wlr_foreign_destroy);
	wlr_foreign_toplevel_handle_v1_set_app_id(
		toplevel->wlr_foreign_toplevel, comp_toplevel_get_foreign_id(toplevel));

	comp_toplevel_set_pid(toplevel);

	bool fullscreen = comp_toplevel_get_is_fullscreen(toplevel);
	// Always tile toplevels
	if (fullscreen) {
		toplevel->tiling_mode = COMP_TILING_MODE_TILED;
	} else if (comp_toplevel_get_always_floating(toplevel) ||
			   ws->fullscreen_toplevel) {
		toplevel->tiling_mode = COMP_TILING_MODE_FLOATING;
	}

	// Move into the predefined layer
	comp_toplevel_move_into_parent_tree(toplevel, NULL);

	comp_toplevel_refresh_titlebar_effects(toplevel);

	// Open new floating toplevels in the center of the output/parent with the
	// natural size. If tiling, save the centered state so untiling would center
	comp_toplevel_set_size(toplevel, toplevel->natural_width,
						   toplevel->natural_height);
	comp_toplevel_center(toplevel, toplevel->natural_width,
						 toplevel->natural_height, false);
	save_state(toplevel, &toplevel->pending_state);

	wl_list_insert(&ws->toplevels, &toplevel->workspace_link);
	wl_list_insert(server.seat->focus_order.prev, &toplevel->focus_link);

	comp_seat_surface_focus(&toplevel->object,
							comp_toplevel_get_wlr_surface(toplevel));

	if (fullscreen && comp_toplevel_can_fullscreen(toplevel)) {
		comp_toplevel_set_fullscreen(toplevel, true, false);
		toplevel->unmapped = false;
	} else {
		toplevel->fullscreen = false;

		// Tile/float the new toplevel
		if (toplevel->tiling_mode == COMP_TILING_MODE_TILED) {
			comp_toplevel_set_tiled(toplevel, true, false);
		} else if (toplevel->tiling_mode == COMP_TILING_MODE_FLOATING) {
			comp_toplevel_set_tiled(toplevel, false, false);
		}

		// We display the toplevel instantly if there isn't a size change.
		bool pending_size_change =
			toplevel->state.width != toplevel->pending_state.width ||
			toplevel->state.height != toplevel->pending_state.height ||
			toplevel->state.x != toplevel->pending_state.x ||
			toplevel->state.y != toplevel->pending_state.y;
		wlr_scene_node_set_enabled(&toplevel->object.scene_tree->node,
								   !pending_size_change);
		toplevel->unmapped = pending_size_change;
		if (!pending_size_change) {
			struct animation_open_close_data from = {
				.opacity = 0.0,
				.state = get_open_close_small_state(toplevel->pending_state),
			};
			struct animation_open_close_data to = {
				.opacity = 1.0,
				.state = toplevel->pending_state,
			};
			comp_toplevel_add_open_close_animation(toplevel, from, to);
		}

		comp_object_mark_dirty(&toplevel->object);
		comp_transaction_commit_dirty(true);
	}
}

void comp_toplevel_generic_unmap(struct comp_toplevel *toplevel) {
	toplevel->unmapped = true;

	if (toplevel->ext_foreign_toplevel) {
		wlr_ext_foreign_toplevel_handle_v1_destroy(
			toplevel->ext_foreign_toplevel);
		toplevel->ext_foreign_toplevel = NULL;
	}

	if (toplevel->wlr_foreign_toplevel) {
		wlr_foreign_toplevel_handle_v1_destroy(toplevel->wlr_foreign_toplevel);
		toplevel->wlr_foreign_toplevel = NULL;
	}

	if (toplevel->fullscreen) {
		comp_toplevel_set_fullscreen(toplevel, false, false);
	}

	// Don't animate if already destroying
	if (!toplevel->object.destroying) {
		// Refresh all of the widgets and sizes before saving the nodes
		comp_toplevel_refresh(toplevel, false);
		struct animation_open_close_data from = {
			.opacity = toplevel->opacity,
			.state = toplevel->state,
		};
		struct animation_open_close_data to = {
			.opacity = 0.0,
			.state = get_open_close_small_state(toplevel->state),
		};
		comp_toplevel_add_open_close_animation(toplevel, from, to);
		comp_toplevel_save_buffer(toplevel);
	}

	/* Reset the cursor mode if the grabbed toplevel was unmapped. */
	if (toplevel == toplevel->server->seat->grabbed_toplevel) {
		comp_cursor_reset_cursor_mode(toplevel->server->seat);
	}

	if (toplevel->tiling_mode == COMP_TILING_MODE_TILED) {
		tiling_node_remove_toplevel(toplevel);
		comp_object_mark_dirty(&toplevel->object);
		comp_transaction_commit_dirty(true);
	}

	// Focus parent toplevel if applicable
	struct comp_toplevel *parent_toplevel = NULL;
	struct wlr_scene_tree *parent_tree =
		comp_toplevel_get_parent_tree(toplevel);
	if (parent_tree) {
		struct comp_object *parent = parent_tree->node.data;
		if (parent && parent->type == COMP_OBJECT_TYPE_TOPLEVEL &&
			parent->data) {
			struct comp_toplevel *toplevel = parent->data;
			if (!parent->destroying && !toplevel->unmapped) {
				parent_toplevel = parent->data;
			}
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

	if (server.seat->cursor->active_constraint) {
		struct wlr_surface *constrain_surface =
			server.seat->cursor->active_constraint->surface;
		if (comp_toplevel_from_wlr_surface(constrain_surface) == toplevel) {
			comp_cursor_constrain(server.seat->cursor, NULL);
		}
	}

	wl_list_remove(&toplevel->workspace_link);
	wl_list_remove(&toplevel->focus_link);
}

void comp_toplevel_generic_commit(struct comp_toplevel *toplevel) {
	struct wlr_box new_geo = comp_toplevel_get_geometry(toplevel);

	bool new_size = new_geo.width != toplevel->geometry.width ||
					new_geo.height != toplevel->geometry.height ||
					new_geo.x != toplevel->geometry.x ||
					new_geo.y != toplevel->geometry.y;
	if (new_size) {
		if (toplevel->anim.resize.client->state == ANIMATION_STATE_NONE) {
			toplevel->geometry = new_geo;
			if (toplevel->tiling_mode == COMP_TILING_MODE_FLOATING) {
				comp_toplevel_set_size(toplevel, new_geo.width, new_geo.height);
				if (toplevel->type == COMP_TOPLEVEL_TYPE_XDG) {
					comp_toplevel_configure(toplevel, new_geo.width,
											new_geo.height, 0, 0);
				}
				comp_object_mark_dirty(&toplevel->object);
				comp_transaction_commit_dirty(false);
			}
			struct wlr_box clip = {
				.width = toplevel->state.width,
				.height = toplevel->state.height,
				.x = toplevel->geometry.x,
				.y = toplevel->geometry.y,
			};
			comp_toplevel_center_and_clip(toplevel, &clip);
		}
	}

	if (toplevel->object.instruction) {
		if (toplevel->impl->should_run_transaction(toplevel)) {
			if (toplevel->unmapped) {
				toplevel->unmapped = false;
				comp_toplevel_refresh(toplevel, false);
				struct animation_open_close_data from = {
					.opacity = 0.0,
					.state =
						get_open_close_small_state(toplevel->pending_state),
				};
				struct animation_open_close_data to = {
					.opacity = 1.0,
					.state = toplevel->pending_state,
				};
				comp_toplevel_add_open_close_animation(toplevel, from, to);
			}

			// Start the resize animation
			if (toplevel->anim.resize.client->state ==
				ANIMATION_STATE_WAITING) {
				struct comp_toplevel_state *state = &toplevel->anim.resize.from;
				comp_toplevel_set_size(toplevel, state->width, state->height);
				comp_toplevel_set_position(toplevel, state->x, state->y);
				comp_toplevel_refresh(toplevel, false);

				comp_animation_client_start(server.animation_mgr,
											toplevel->anim.resize.client);
			}

			struct comp_transaction_instruction *instruction =
				toplevel->object.instruction;
			comp_transaction_instruction_mark_ready(instruction);
		} else if (!wl_list_empty(&toplevel->saved_scene_tree->children)) {
			comp_toplevel_send_frame_done(toplevel);
		}
	}
}

void comp_toplevel_generic_set_natural_size(struct comp_toplevel *toplevel,
											int width, int height) {
	struct comp_output *output = toplevel->workspace->output;
	struct wlr_box box = output->usable_area;

	if (width < TOPLEVEL_MIN_WIDTH) {
		width = box.width * 0.5;
	}
	if (height < TOPLEVEL_MIN_HEIGHT) {
		height = box.height * 0.75;
	}

	toplevel->natural_width =
		MAX(TOPLEVEL_MIN_WIDTH, MIN(width, output->geometry.width));
	toplevel->natural_height =
		MAX(TOPLEVEL_MIN_HEIGHT, MIN(height, output->geometry.height));

	comp_toplevel_set_size(toplevel, toplevel->natural_width,
						   toplevel->natural_height);
}
