#include <assert.h>
#include <float.h>
#include <scenefx/types/wlr_scene.h>
#include <stdio.h>
#include <wayland-util.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>

#include "comp/border/titlebar.h"
#include "comp/output.h"
#include "comp/server.h"
#include "comp/widget.h"
#include "comp/workspace.h"
#include "desktop/toplevel.h"
#include "seat/cursor.h"

static void comp_wlr_surface_unfocus(struct wlr_surface *surface) {
	struct wlr_xdg_surface *xdg_surface =
		wlr_xdg_surface_try_from_wlr_surface(surface);
	assert(xdg_surface && xdg_surface->toplevel);

	wlr_xdg_toplevel_set_activated(xdg_surface->toplevel, false);

	struct wlr_scene_tree *scene_tree = xdg_surface->data;
	struct comp_toplevel *toplevel = scene_tree->node.data;
	if (toplevel) {
		toplevel->focused = false;

		/*
		 * Redraw
		 */

		comp_widget_draw(&toplevel->titlebar->widget);
	}
}

void comp_toplevel_focus(struct comp_toplevel *toplevel,
						 struct wlr_surface *surface) {
	/* Note: this function only deals with keyboard focus. */
	if (toplevel == NULL) {
		return;
	}
	struct comp_server *server = toplevel->server;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
	if (prev_surface == surface) {
		/* Don't re-focus an already focused surface. */
		return;
	}
	if (prev_surface) {
		/*
		 * Deactivate the previously focused surface. This lets the client know
		 * it no longer has focus and the client will repaint accordingly, e.g.
		 * stop displaying a caret.
		 */
		comp_wlr_surface_unfocus(prev_surface);
	}
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	toplevel->focused = true;
	/* Move the toplevel to the front */
	wlr_scene_node_raise_to_top(&toplevel->object.scene_tree->node);
	wl_list_remove(&toplevel->workspace_link);
	wl_list_insert(&toplevel->workspace->toplevels, &toplevel->workspace_link);
	/* Activate the new surface */
	wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);
	/*
	 * Tell the seat to have the keyboard enter this surface. wlroots will keep
	 * track of this and automatically send key events to the appropriate
	 * clients without additional work on your part.
	 */
	if (keyboard != NULL) {
		wlr_seat_keyboard_notify_enter(
			seat, toplevel->xdg_toplevel->base->surface, keyboard->keycodes,
			keyboard->num_keycodes, &keyboard->modifiers);
	}

	/*
	 * Redraw
	 */

	comp_widget_draw(&toplevel->titlebar->widget);
}

/**
 * Returns the output where the majority size of the toplevel resides
 */
static struct comp_output *find_output(struct comp_toplevel *toplevel) {
	int x, y;
	wlr_scene_node_coords(&toplevel->object.scene_tree->node, &x, &y);

	double center_x = x + (double)toplevel->object.width / 2;
	double center_y = y + (double)toplevel->object.height / 2;
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
	struct comp_toplevel *toplevel = server->grabbed_toplevel;
	if (server->grabbed_toplevel) {
		wlr_scene_node_set_position(
			&toplevel->object.scene_tree->node,
			server->cursor->wlr_cursor->x - server->grab_x,
			server->cursor->wlr_cursor->y - server->grab_y);

		// Update floating toplevels current monitor and workspace.
		// Also raise the output node to the top so that it's floating toplevels
		// remain on top on other outputs (if they intersect)
		if (toplevel->tiling_mode == COMP_TILING_MODE_FLOATING) {
			struct comp_output *new_output = find_output(toplevel);
			struct comp_workspace *ws;
			if (new_output && (ws = comp_output_get_active_ws(
								   new_output, toplevel->fullscreen))) {
				comp_workspace_move_toplevel_to(ws, toplevel);
				// Update the active output
				server->active_output = new_output;
				wlr_scene_node_raise_to_top(&new_output->output_tree->node);
			}
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
	struct comp_toplevel *toplevel = server->grabbed_toplevel;
	double border_x = server->cursor->wlr_cursor->x - server->grab_x;
	double border_y = server->cursor->wlr_cursor->y - server->grab_y;
	int new_left = server->grab_geobox.x;
	int new_right = server->grab_geobox.x + server->grab_geobox.width;
	int new_top = server->grab_geobox.y;
	int new_bottom = server->grab_geobox.y + server->grab_geobox.height;

	if (server->resize_edges & WLR_EDGE_TOP) {
		new_top = border_y;
		if (new_top >= new_bottom) {
			new_top = new_bottom - 1;
		}
	} else if (server->resize_edges & WLR_EDGE_BOTTOM) {
		new_bottom = border_y;
		if (new_bottom <= new_top) {
			new_bottom = new_top + 1;
		}
	}
	if (server->resize_edges & WLR_EDGE_LEFT) {
		new_left = border_x;
		if (new_left >= new_right) {
			new_left = new_right - 1;
		}
	} else if (server->resize_edges & WLR_EDGE_RIGHT) {
		new_right = border_x;
		if (new_right <= new_left) {
			new_right = new_left + 1;
		}
	}

	struct wlr_box geo_box;
	wlr_xdg_surface_get_geometry(toplevel->xdg_toplevel->base, &geo_box);
	wlr_scene_node_set_position(&toplevel->object.scene_tree->node,
								new_left - geo_box.x, new_top - geo_box.y);

	int new_width = new_right - new_left;
	int new_height = new_bottom - new_top;
	wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, new_width, new_height);
}
