#include <assert.h>
#include <limits.h>
#include <scenefx/types/fx/shadow_data.h>
#include <scenefx/types/wlr_scene.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>

#include "comp/object.h"
#include "comp/output.h"
#include "comp/server.h"
#include "comp/widget.h"
#include "comp/workspace.h"
#include "constants.h"
#include "desktop/toplevel.h"
#include "desktop/widgets/resize_edge.h"
#include "desktop/widgets/titlebar.h"
#include "desktop/xdg.h"
#include "seat/cursor.h"
#include "seat/seat.h"
#include "util.h"

static void iter_scene_buffers_apply_effects(struct wlr_scene_buffer *buffer,
											 int sx, int sy, void *user_data) {
	struct wlr_scene_surface *scene_surface =
		wlr_scene_surface_try_from_buffer(buffer);
	if (!scene_surface || !user_data) {
		return;
	}

	struct wlr_xdg_surface *xdg_surface =
		wlr_xdg_surface_try_from_wlr_surface(scene_surface->surface);
	if (!xdg_surface) {
		return;
	}

	switch (xdg_surface->role) {
	case WLR_XDG_SURFACE_ROLE_NONE:
		return;
	case WLR_XDG_SURFACE_ROLE_TOPLEVEL:;
		struct comp_toplevel *toplevel = user_data;
		bool has_effects = !toplevel->fullscreen;

		// TODO: Be able to set whole decoration_data instead of calling
		// each individually?

		// Toplevel
		wlr_scene_buffer_set_opacity(buffer,
									 has_effects ? toplevel->opacity : 1);
		wlr_scene_buffer_set_corner_radius(
			buffer, has_effects ? toplevel->corner_radius : 0);

		// Titlebar
		struct comp_widget *titlebar_widget = &toplevel->titlebar->widget;
		struct wlr_scene_buffer *titlebar_buffer =
			titlebar_widget->scene_buffer;
		wlr_scene_buffer_set_corner_radius(
			titlebar_buffer, has_effects ? titlebar_widget->corner_radius : 0);
		wlr_scene_buffer_set_shadow_data(titlebar_buffer,
										 titlebar_widget->shadow_data);
		break;
	case WLR_XDG_SURFACE_ROLE_POPUP:;
		struct comp_xdg_popup *popup = user_data;
		wlr_scene_buffer_set_shadow_data(buffer, popup->shadow_data);
		wlr_scene_buffer_set_corner_radius(buffer, popup->corner_radius);
		wlr_scene_buffer_set_opacity(buffer, popup->opacity);
		break;
	}
}

void xdg_apply_effects(struct wlr_scene_tree *tree, void *data) {
	wlr_scene_node_for_each_buffer(&tree->node,
								   iter_scene_buffers_apply_effects, data);
}

static void save_state(struct comp_toplevel *toplevel) {
	// Position
	int x, y;
	wlr_scene_node_coords(&toplevel->object.scene_tree->node, &x, &y);
	// Adjust the node coordinates to be output-relative
	double lx = x;
	double ly = y;
	wlr_output_layout_output_coords(server.output_layout,
									toplevel->workspace->output->wlr_output,
									&lx, &ly);
	toplevel->saved_state.x = lx;
	toplevel->saved_state.y = ly;
	// Size
	struct wlr_box geometry;
	wlr_xdg_surface_get_geometry(toplevel->xdg_toplevel->base, &geometry);
	toplevel->saved_state.width = geometry.width;
	toplevel->saved_state.height = geometry.height;

	toplevel->saved_state.workspace = toplevel->workspace;
}

static void restore_state(struct comp_toplevel *toplevel) {
	struct comp_output *output = toplevel->workspace->output;
	struct comp_workspace *fs_ws = toplevel->workspace;
	assert(fs_ws->type == COMP_WORKSPACE_TYPE_FULLSCREEN);

	// TODO: Move to the original workspace
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

	wlr_scene_node_set_position(&toplevel->object.scene_tree->node,
								toplevel->saved_state.x,
								toplevel->saved_state.y);
	wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel,
							  toplevel->saved_state.width,
							  toplevel->saved_state.height);
	xdg_update(toplevel, toplevel->saved_state.width,
			   toplevel->saved_state.height);

	toplevel->saved_state.x = 0;
	toplevel->saved_state.y = 0;
	toplevel->saved_state.width = 0;
	toplevel->saved_state.height = 0;
	toplevel->saved_state.workspace = NULL;
}

void xdg_update(struct comp_toplevel *toplevel, int width, int height) {
	struct comp_titlebar *titlebar = toplevel->titlebar;

	toplevel->object.width = width;
	toplevel->object.height = height;

	if (toplevel->xdg_toplevel->base->client->shell->version >=
			XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION &&
		toplevel->object.width >= 0 && toplevel->object.height >= 0) {
		wlr_xdg_toplevel_set_bounds(toplevel->xdg_toplevel,
									toplevel->object.width,
									toplevel->object.height);
	}

	wlr_scene_node_set_enabled(&toplevel->decoration_scene_tree->node,
							   !toplevel->fullscreen);
	if (!toplevel->fullscreen) {
		bool show_full_titlebar = comp_titlebar_should_be_shown(toplevel);
		int top_border_height = BORDER_WIDTH;
		if (show_full_titlebar) {
			comp_titlebar_calculate_bar_height(titlebar);
			top_border_height += toplevel->titlebar->bar_height;
		}

		struct wlr_xdg_toplevel_state *state = &toplevel->xdg_toplevel->current;
		int max_width = state->max_width;
		int max_height = state->max_height;
		int min_width = state->min_width;
		int min_height = state->min_height;

		toplevel->object.width =
			fmax(min_width + (2 * BORDER_WIDTH), toplevel->object.width);
		toplevel->object.height =
			fmax(min_height + BORDER_WIDTH, toplevel->object.height);

		if (max_width > 0 && !(2 * BORDER_WIDTH > INT_MAX - max_width)) {
			toplevel->object.width =
				fmin(max_width + (2 * BORDER_WIDTH), toplevel->object.width);
		}
		if (max_height > 0 &&
			!(top_border_height + BORDER_WIDTH > INT_MAX - max_height)) {
			toplevel->object.height =
				fmin(max_height + top_border_height + BORDER_WIDTH,
					 toplevel->object.height);
		}

		// Only redraw the titlebar if the size has changed
		int new_titlebar_height = top_border_height + toplevel->object.height;
		if (toplevel->titlebar &&
			(titlebar->widget.object.width != toplevel->object.width ||
			 titlebar->widget.object.height != new_titlebar_height)) {
			comp_widget_draw_resize(&titlebar->widget, toplevel->object.width,
									new_titlebar_height);
			// Position the titlebar above the window
			wlr_scene_node_set_position(
				&titlebar->widget.object.scene_tree->node, -BORDER_WIDTH,
				-top_border_height);

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
		wlr_scene_node_set_position(&toplevel->object.scene_tree->node, 0, 0);
	}
}

/*
 * XDG Popup
 */

static void handle_new_popup(struct wl_listener *listener, void *data) {
	struct comp_toplevel *toplevel =
		wl_container_of(listener, toplevel, new_popup);
	struct wlr_xdg_popup *wlr_popup = data;
	// TODO: Somehow raise above comp_resize_edge
	xdg_new_xdg_popup(wlr_popup, &toplevel->object, toplevel->xdg_scene_tree);
}

/*
 * XDG Toplevel
 */

static void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
	/* Called when the surface is mapped, or ready to display on-screen. */
	struct comp_toplevel *toplevel =
		wl_container_of(listener, toplevel, commit);

	// Set geometry
	struct wlr_box geometry;
	wlr_xdg_surface_get_geometry(toplevel->xdg_toplevel->base, &geometry);
	toplevel->object.width = geometry.width;
	toplevel->object.height = geometry.height;

	if (!toplevel->fullscreen) {
		toplevel->object.width += 2 * BORDER_WIDTH;
		toplevel->object.height += 2 * BORDER_WIDTH;
	} else {
		wlr_scene_node_set_position(&toplevel->object.scene_tree->node, 0, 0);
	}

	xdg_update(toplevel, toplevel->object.width, toplevel->object.height);
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
	/* Called when the xdg_toplevel is destroyed. */
	struct comp_toplevel *toplevel =
		wl_container_of(listener, toplevel, destroy);

	wl_list_remove(&toplevel->map.link);
	wl_list_remove(&toplevel->unmap.link);
	wl_list_remove(&toplevel->commit.link);
	wl_list_remove(&toplevel->destroy.link);

	wlr_scene_node_destroy(&toplevel->object.scene_tree->node);

	free(toplevel);
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
	if (focused_surface && toplevel->xdg_toplevel->base->surface !=
							   wlr_surface_get_root_surface(focused_surface)) {
		return;
	}

	server->seat->grabbed_toplevel = toplevel;
	server->seat->cursor->cursor_mode = mode;

	switch (mode) {
	case COMP_CURSOR_PASSTHROUGH:
		break;
	case COMP_CURSOR_MOVE:;
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

		xdg_update(toplevel, toplevel->object.width, toplevel->object.height);
		break;
	case COMP_CURSOR_RESIZE:;
		struct wlr_box geo_box;
		wlr_xdg_surface_get_geometry(toplevel->xdg_toplevel->base, &geo_box);

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

		toplevel->object.width = geo_box.width + 2 * BORDER_WIDTH;
		toplevel->object.height = geo_box.height + 2 * BORDER_WIDTH;
		xdg_update(toplevel, toplevel->object.width, toplevel->object.height);
		break;
	}
}

static void xdg_toplevel_request_move(struct wl_listener *listener,
									  void *data) {
	/* This event is raised when a client would like to begin an interactive
	 * move, typically because the user clicked on their client-side
	 * decorations. Note that a more sophisticated compositor should check the
	 * provided serial against a list of button press serials sent to this
	 * client, to prevent the client from requesting this whenever they want. */
	struct comp_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_move);
	if (!toplevel->fullscreen) {
		comp_toplevel_begin_interactive(toplevel, COMP_CURSOR_MOVE, 0);
	}
}

static void xdg_toplevel_request_resize(struct wl_listener *listener,
										void *data) {
	/* This event is raised when a client would like to begin an interactive
	 * resize, typically because the user clicked on their client-side
	 * decorations. Note that a more sophisticated compositor should check the
	 * provided serial against a list of button press serials sent to this
	 * client, to prevent the client from requesting this whenever they want. */
	struct wlr_xdg_toplevel_resize_event *event = data;
	struct comp_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_resize);
	if (!toplevel->fullscreen) {
		comp_toplevel_begin_interactive(toplevel, COMP_CURSOR_RESIZE,
										event->edges);
	}
}

static void xdg_toplevel_request_maximize(struct wl_listener *listener,
										  void *data) {
	/* This event is raised when a client would like to maximize itself,
	 * typically because the user clicked on the maximize button on
	 * client-side decorations. tinywl doesn't support maximization, but
	 * to conform to xdg-shell protocol we still must send a configure.
	 * wlr_xdg_surface_schedule_configure() is used to send an empty reply. */
	struct comp_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_maximize);
	wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
}

static void xdg_toplevel_request_fullscreen(struct wl_listener *listener,
											void *data) {
	/* Just as with request_maximize, we must send a configure here. */
	struct comp_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_fullscreen);
	struct wlr_xdg_toplevel *xdg_toplevel = toplevel->xdg_toplevel;

	if (!xdg_toplevel->base->surface->mapped) {
		return;
	}

	struct wlr_xdg_toplevel_requested *req = &xdg_toplevel->requested;
	comp_toplevel_set_fullscreen(toplevel, req->fullscreen);
}

static void xdg_toplevel_set_title(struct wl_listener *listener, void *data) {
	struct comp_toplevel *toplevel =
		wl_container_of(listener, toplevel, set_title);

	if (comp_titlebar_should_be_shown(toplevel)) {
		comp_widget_draw(&toplevel->titlebar->widget);
	}
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
	/* Called when the surface is mapped, or ready to display on-screen. */
	struct comp_toplevel *toplevel = wl_container_of(listener, toplevel, map);
	wl_list_insert(&toplevel->workspace->toplevels, &toplevel->workspace_link);

	xdg_apply_effects(toplevel->xdg_scene_tree, toplevel);

	// Set geometry
	struct wlr_box geometry;
	wlr_xdg_surface_get_geometry(toplevel->xdg_toplevel->base, &geometry);
	toplevel->object.width = geometry.width + 2 * BORDER_WIDTH;
	toplevel->object.height = geometry.height + 2 * BORDER_WIDTH;

	if (toplevel->tiling_mode == COMP_TILING_MODE_FLOATING) {
		// Open new floating toplevels in the center of the output
		struct wlr_box output_box;
		wlr_output_layout_get_box(toplevel->server->output_layout,
								  toplevel->workspace->output->wlr_output,
								  &output_box);
		wlr_scene_node_set_position(
			&toplevel->object.scene_tree->node,
			(output_box.width - toplevel->object.width) / 2,
			(output_box.height - toplevel->object.height) / 2);

		xdg_update(toplevel, toplevel->object.width, toplevel->object.height);
	} else {
		// Tile the new toplevel
		// TODO: Tile
	}

	wl_list_insert(server.seat->focus_order.prev, &toplevel->focus_link);

	wlr_scene_node_set_enabled(&toplevel->object.scene_tree->node, true);
	comp_seat_surface_focus(&toplevel->object,
							toplevel->xdg_toplevel->base->surface);

	struct wlr_xdg_toplevel *xdg_toplevel = toplevel->xdg_toplevel;
	toplevel->new_popup.notify = handle_new_popup;
	wl_signal_add(&xdg_toplevel->base->events.new_popup, &toplevel->new_popup);
	toplevel->request_move.notify = xdg_toplevel_request_move;
	wl_signal_add(&xdg_toplevel->events.request_move, &toplevel->request_move);
	toplevel->request_resize.notify = xdg_toplevel_request_resize;
	wl_signal_add(&xdg_toplevel->events.request_resize,
				  &toplevel->request_resize);
	toplevel->request_maximize.notify = xdg_toplevel_request_maximize;
	wl_signal_add(&xdg_toplevel->events.request_maximize,
				  &toplevel->request_maximize);
	toplevel->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
	wl_signal_add(&xdg_toplevel->events.request_fullscreen,
				  &toplevel->request_fullscreen);
	toplevel->set_title.notify = xdg_toplevel_set_title;
	wl_signal_add(&xdg_toplevel->events.set_title, &toplevel->set_title);
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
	/* Called when the surface is unmapped, and should no longer be shown. */
	struct comp_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);

	if (toplevel->fullscreen) {
		comp_toplevel_set_fullscreen(toplevel, false);
	}

	/* Reset the cursor mode if the grabbed toplevel was unmapped. */
	if (toplevel == toplevel->server->seat->grabbed_toplevel) {
		comp_cursor_reset_cursor_mode(toplevel->server->seat);
	}

	wl_list_remove(&toplevel->workspace_link);
	wl_list_remove(&toplevel->focus_link);

	wl_list_remove(&toplevel->new_popup.link);
	wl_list_remove(&toplevel->request_move.link);
	wl_list_remove(&toplevel->request_resize.link);
	wl_list_remove(&toplevel->request_maximize.link);
	wl_list_remove(&toplevel->request_fullscreen.link);
	wl_list_remove(&toplevel->set_title.link);

	comp_seat_surface_unfocus(toplevel->xdg_toplevel->base->surface, true);
}

void xdg_new_xdg_surface(struct wl_listener *listener, void *data) {
	/* This event is raised when wlr_xdg_shell receives a new xdg surface from a
	 * client, either a toplevel (application window) or popup. */
	struct comp_server *server =
		wl_container_of(listener, server, new_xdg_surface);
	struct wlr_xdg_surface *xdg_surface = data;

	switch (xdg_surface->role) {
	case WLR_XDG_SURFACE_ROLE_NONE:
		// Ignore surfaces with the role none
		wlr_log(WLR_ERROR, "Unknown XDG Surface Role");
		return;
	case WLR_XDG_SURFACE_ROLE_POPUP:
		// Ignore surfaces with the role popup and listen to signal after the
		// toplevel has ben mapped
		return;
	case WLR_XDG_SURFACE_ROLE_TOPLEVEL:
		break;
	}

	/* Allocate a tinywl_toplevel for this surface */
	struct comp_toplevel *toplevel = calloc(1, sizeof(*toplevel));
	toplevel->server = server;
	toplevel->using_csd = true;
	/* Set the scene_nodes decoration data */
	toplevel->opacity = 1;
	toplevel->corner_radius = EFFECTS_CORNER_RADII;
	toplevel->shadow_data.enabled = true;
	toplevel->shadow_data.color =
		wlr_render_color_from_color(&(const uint32_t){TOPLEVEL_SHADOW_COLOR});
	toplevel->shadow_data.blur_sigma = TOPLEVEL_SHADOW_BLUR_SIGMA;

	toplevel->xdg_toplevel = xdg_surface->toplevel;

	struct comp_output *output = get_active_output(server);

	toplevel->fullscreen = toplevel->xdg_toplevel->requested.fullscreen;

	// Add the toplevel to the tiled/floating layer
	// TODO: Check if it should be in the floating layer or not
	toplevel->tiling_mode = COMP_TILING_MODE_FLOATING;
	// TODO: Add other condition for tiled
	if (toplevel->fullscreen) {
		toplevel->tiling_mode = COMP_TILING_MODE_TILED;
	}
	toplevel->workspace =
		comp_output_get_active_ws(output, toplevel->fullscreen);
	struct wlr_scene_tree *tree = comp_toplevel_get_layer(toplevel);
	toplevel->object.scene_tree = alloc_tree(tree);

	toplevel->decoration_scene_tree = alloc_tree(toplevel->object.scene_tree);

	// Initialize saved position/size
	toplevel->saved_state.x = 0;
	toplevel->saved_state.y = 0;
	toplevel->saved_state.width = 0;
	toplevel->saved_state.height = 0;

	/*
	 * Titlebar
	 */

	toplevel->titlebar = comp_titlebar_init(toplevel->server, toplevel);

	/*
	 * XDG Surface
	 */

	// TODO: event.output_enter/output_leave for primary output
	toplevel->xdg_scene_tree = wlr_scene_xdg_surface_create(
		toplevel->object.scene_tree, toplevel->xdg_toplevel->base);
	toplevel->xdg_scene_tree->node.data = &toplevel->object;
	toplevel->object.scene_tree->node.data = &toplevel->object;
	toplevel->object.data = toplevel;
	toplevel->object.type = COMP_OBJECT_TYPE_TOPLEVEL;
	xdg_surface->data = toplevel->object.scene_tree;

	/*
	 * Resize borders
	 */
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
		toplevel->edges[i] = comp_resize_edge_init(server, toplevel, edges[i]);
	}

	/*
	 * Events
	 */

	/* Listen to the various events it can emit */
	toplevel->map.notify = xdg_toplevel_map;
	wl_signal_add(&xdg_surface->surface->events.map, &toplevel->map);
	toplevel->unmap.notify = xdg_toplevel_unmap;
	wl_signal_add(&xdg_surface->surface->events.unmap, &toplevel->unmap);
	toplevel->commit.notify = xdg_toplevel_commit;
	wl_signal_add(&xdg_surface->surface->events.commit, &toplevel->commit);
	toplevel->destroy.notify = xdg_toplevel_destroy;
	wl_signal_add(&xdg_surface->events.destroy, &toplevel->destroy);
}

struct wlr_scene_tree *comp_toplevel_get_layer(struct comp_toplevel *toplevel) {
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

void comp_toplevel_set_fullscreen(struct comp_toplevel *toplevel, bool state) {
	if (toplevel->fullscreen == state) {
		return;
	}
	toplevel->fullscreen = state;

	wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, state);
	// TODO: Also call foreign toplevel fullscreen function

	if (state) {
		// Save the floating state
		save_state(toplevel);

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
}

void comp_toplevel_toggle_fullscreen(struct comp_toplevel *toplevel) {
	comp_toplevel_set_fullscreen(toplevel, !toplevel->fullscreen);
}
