#include <float.h>
#include <scenefx/types/wlr_scene.h>
#include <stdio.h>
#include <wayland-util.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>

#include "comp/output.h"
#include "comp/server.h"
#include "comp/widget.h"
#include "comp/workspace.h"
#include "desktop/toplevel.h"
#include "desktop/widgets/titlebar.h"
#include "seat/cursor.h"
#include "seat/seat.h"

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
	struct comp_toplevel *toplevel = server->seat->grabbed_toplevel;
	if (toplevel) {
		// Adjust the toplevel coordinates to be root-relative
		double lx = server->seat->cursor->wlr_cursor->x - server->seat->grab_x;
		double ly = server->seat->cursor->wlr_cursor->y - server->seat->grab_y;
		wlr_output_layout_output_coords(server->output_layout,
										toplevel->workspace->output->wlr_output,
										&lx, &ly);
		wlr_scene_node_set_position(&toplevel->object.scene_tree->node, lx, ly);

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
	struct comp_toplevel *toplevel = server->seat->grabbed_toplevel;
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

	struct wlr_box geo_box;
	wlr_xdg_surface_get_geometry(toplevel->xdg_toplevel->base, &geo_box);
	wlr_scene_node_set_position(&toplevel->object.scene_tree->node,
								new_left - geo_box.x, new_top - geo_box.y);

	int new_width = new_right - new_left;
	int new_height = new_bottom - new_top;
	wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, new_width, new_height);
}

uint32_t
comp_toplevel_get_edge_from_cursor_coords(struct comp_toplevel *toplevel,
										  struct comp_cursor *cursor) {
	uint32_t edge = 0;
	if (toplevel->object.width == 0 || toplevel->object.height == 0) {
		return edge;
	}

	int lx, ly;
	wlr_scene_node_coords(&toplevel->object.scene_tree->node, &lx, &ly);

	const double y =
		MAX(0, (cursor->wlr_cursor->y - ly) / toplevel->object.height);
	if (y > 0.5) {
		edge |= WLR_EDGE_BOTTOM;
	} else if (y < 0.5) {
		edge |= WLR_EDGE_TOP;
	}

	const double x =
		MAX(0, (cursor->wlr_cursor->x - lx) / toplevel->object.width);
	if (x > 0.5) {
		edge |= WLR_EDGE_RIGHT;
	} else if (x < 0.5) {
		edge |= WLR_EDGE_LEFT;
	}

	return edge;
}
