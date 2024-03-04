#include <assert.h>
#include <limits.h>
#include <scenefx/types/fx/shadow_data.h>
#include <scenefx/types/wlr_scene.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>

#include "comp/border/edge.h"
#include "comp/border/titlebar.h"
#include "comp/object.h"
#include "comp/output.h"
#include "comp/server.h"
#include "comp/toplevel.h"
#include "comp/widget.h"
#include "comp/workspace.h"
#include "constants.h"
#include "desktop/xdg.h"
#include "seat/cursor.h"
#include "util.h"

static void xdg_resize(struct comp_toplevel *toplevel, int width, int height) {
	toplevel->object.width = width;
	toplevel->object.height = height;

	int top_height = BORDER_WIDTH;
	if (comp_titlebar_should_be_shown(toplevel)) {
		// TODO: Calculate titlebar height
		top_height = TITLEBAR_HEIGHT;
	}

	if (toplevel->xdg_toplevel->base->client->shell->version >=
			XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION &&
		toplevel->object.width >= 0 && toplevel->object.height >= 0) {
		wlr_xdg_toplevel_set_bounds(toplevel->xdg_toplevel,
									toplevel->object.width,
									toplevel->object.height);
	}

	struct wlr_xdg_toplevel_state *state = &toplevel->xdg_toplevel->current;
	int max_width = state->max_width;
	int max_height = state->max_height;
	int min_width = state->min_width;
	int min_height = state->min_height;

	toplevel->object.width =
		fmax(min_width + (2 * BORDER_WIDTH), toplevel->object.width);
	toplevel->object.height =
		fmax(min_height + top_height, toplevel->object.height);

	if (max_width > 0 && !(2 * BORDER_WIDTH > INT_MAX - max_width)) {
		toplevel->object.width =
			fmin(max_width + (2 * BORDER_WIDTH), toplevel->object.width);
	}
	if (max_height > 0 && !(top_height > INT_MAX - max_height)) {
		toplevel->object.height =
			fmin(max_height + top_height, toplevel->object.height);
	}

	// Only redraw the titlebar if the size has changed
	int new_titlebar_height =
		top_height + toplevel->object.height - BORDER_WIDTH;
	struct comp_titlebar *titlebar = toplevel->titlebar;
	if (toplevel->titlebar &&
		(titlebar->widget.object.width != toplevel->object.width ||
		 titlebar->widget.object.height != new_titlebar_height)) {
		comp_widget_draw_resize(&titlebar->widget, toplevel->object.width,
								new_titlebar_height);
		// Position the titlebar above the window
		wlr_scene_node_set_position(&titlebar->widget.object.scene_tree->node,
									-BORDER_WIDTH, -top_height);

		// Adjust edges
		comp_widget_draw_resize(&toplevel->edge->widget,
								toplevel->object.width +
									BORDER_RESIZE_WIDTH * 2,
								new_titlebar_height + BORDER_RESIZE_WIDTH * 2);
		wlr_scene_node_set_position(
			&toplevel->edge->widget.object.scene_tree->node,
			-BORDER_RESIZE_WIDTH - BORDER_WIDTH,
			-BORDER_RESIZE_WIDTH - top_height);
	}

	// wlr_scene_node_set_position(&toplevel->scene_tree->node, BORDER_WIDTH,
	// 							top_height);
	// toplevel->resize_serial = wlr_xdg_toplevel_set_size(
	// 	toplevel->xdg_toplevel, toplevel->width - 2 * BORDER_WIDTH,
	// 	toplevel->height - top_height);
}

static void iter_xdg_scene_buffers(struct wlr_scene_buffer *buffer, int sx,
								   int sy, void *user_data) {
	struct wlr_scene_surface *scene_surface =
		wlr_scene_surface_try_from_buffer(buffer);
	if (!scene_surface) {
		return;
	}

	struct comp_toplevel *toplevel = user_data;
	if (toplevel) {
		// TODO: Be able to set whole decoration_data instead of calling
		// each individually?
		wlr_scene_buffer_set_opacity(buffer, toplevel->opacity);

		// Only add shadows and clip to geometry for XDG toplevels
		struct wlr_xdg_surface *xdg_surface =
			wlr_xdg_surface_try_from_wlr_surface(scene_surface->surface);
		if (xdg_surface && xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
			wlr_scene_buffer_set_corner_radius(buffer, toplevel->corner_radius);

			if (toplevel->titlebar) {
				struct comp_widget *titlebar_widget =
					&toplevel->titlebar->widget;
				struct wlr_scene_buffer *titlebar_buffer =
					titlebar_widget->scene_buffer;
				wlr_scene_buffer_set_corner_radius(
					titlebar_buffer, titlebar_widget->corner_radius);
				wlr_scene_buffer_set_shadow_data(titlebar_buffer,
												 titlebar_widget->shadow_data);
			} else {
				wlr_scene_buffer_set_shadow_data(buffer, toplevel->shadow_data);
			}
		}
	}
}

static void xdg_popup_map(struct wl_listener *listener, void *data) {
	/* Called when the surface is mapped, or ready to display on-screen. */
	struct comp_toplevel *toplevel = wl_container_of(listener, toplevel, map);

	wlr_scene_node_for_each_buffer(&toplevel->xdg_scene_tree->node,
								   iter_xdg_scene_buffers, toplevel);
}

static void xdg_popup_destroy(struct wl_listener *listener, void *data) {
	/* Called when the xdg_toplevel is destroyed. */
	struct comp_toplevel *toplevel =
		wl_container_of(listener, toplevel, destroy);

	wl_list_remove(&toplevel->map.link);
	wl_list_remove(&toplevel->destroy.link);

	free(toplevel);
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
	/* Called when the surface is mapped, or ready to display on-screen. */
	struct comp_toplevel *toplevel = wl_container_of(listener, toplevel, map);
	wl_list_insert(&toplevel->server->toplevels, &toplevel->link);

	// Set the effects for each scene_buffer
	wlr_scene_node_for_each_buffer(&toplevel->xdg_scene_tree->node,
								   iter_xdg_scene_buffers, toplevel);

	// Set geometry
	struct wlr_box geometry;
	wlr_xdg_surface_get_geometry(toplevel->xdg_toplevel->base, &geometry);
	toplevel->initial_width = geometry.width + 2 * BORDER_WIDTH;
	toplevel->initial_height = geometry.height + 2 * BORDER_WIDTH;
	toplevel->object.width = toplevel->initial_width;
	toplevel->object.height = toplevel->initial_height;

	if (toplevel->tiling_mode == COMP_TILING_MODE_FLOATING) {
		// Open new floating toplevels in the center of the output
		struct wlr_box box;
		wlr_output_layout_get_box(toplevel->server->output_layout, NULL, &box);
		wlr_scene_node_set_position(
			&toplevel->object.scene_tree->node,
			box.x + (box.width - toplevel->initial_width) / 2,
			box.y + (box.height - toplevel->initial_height) / 2);

		xdg_resize(toplevel, toplevel->initial_width, toplevel->initial_height);
	} else {
		// Tile the new toplevel
		// TODO: Tile
	}

	wlr_scene_node_set_enabled(&toplevel->object.scene_tree->node, true);
	comp_toplevel_focus(toplevel, toplevel->xdg_toplevel->base->surface);
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
	/* Called when the surface is unmapped, and should no longer be shown. */
	struct comp_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);

	/* Reset the cursor mode if the grabbed toplevel was unmapped. */
	if (toplevel == toplevel->server->grabbed_toplevel) {
		comp_cursor_reset_cursor_mode(toplevel->server);
	}

	wl_list_remove(&toplevel->link);
}

static void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
	/* Called when the surface is mapped, or ready to display on-screen. */
	struct comp_toplevel *toplevel =
		wl_container_of(listener, toplevel, commit);

	// Set geometry
	struct wlr_box geometry;
	wlr_xdg_surface_get_geometry(toplevel->xdg_toplevel->base, &geometry);
	toplevel->object.width = geometry.width + 2 * BORDER_WIDTH;
	toplevel->object.height = geometry.height + 2 * BORDER_WIDTH;

	// Open new floating toplevels in the center of the output
	if (toplevel->tiling_mode == COMP_TILING_MODE_FLOATING) {
		xdg_resize(toplevel, toplevel->object.width, toplevel->object.height);
	} else {
		// Tile the new toplevel
		// TODO: Tile
	}
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
	/* Called when the xdg_toplevel is destroyed. */
	struct comp_toplevel *toplevel =
		wl_container_of(listener, toplevel, destroy);

	wl_list_remove(&toplevel->map.link);
	wl_list_remove(&toplevel->unmap.link);
	wl_list_remove(&toplevel->commit.link);
	wl_list_remove(&toplevel->destroy.link);
	wl_list_remove(&toplevel->request_move.link);
	wl_list_remove(&toplevel->request_resize.link);
	wl_list_remove(&toplevel->request_maximize.link);
	wl_list_remove(&toplevel->request_fullscreen.link);

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
		server->seat->pointer_state.focused_surface;
	/* Deny move/resize requests from unfocused clients. */
	if (focused_surface && toplevel->xdg_toplevel->base->surface !=
							   wlr_surface_get_root_surface(focused_surface)) {
		return;
	}

	server->grabbed_toplevel = toplevel;
	server->cursor->cursor_mode = mode;

	if (mode == COMP_CURSOR_MOVE) {
		server->grab_x =
			server->cursor->wlr_cursor->x - toplevel->object.scene_tree->node.x;
		server->grab_y =
			server->cursor->wlr_cursor->y - toplevel->object.scene_tree->node.y;
		xdg_resize(toplevel, toplevel->object.width, toplevel->object.height);
	} else {
		struct wlr_box geo_box;
		wlr_xdg_surface_get_geometry(toplevel->xdg_toplevel->base, &geo_box);

		double border_x = (toplevel->object.scene_tree->node.x + geo_box.x) +
						  ((edges & WLR_EDGE_RIGHT) ? geo_box.width : 0);
		double border_y = (toplevel->object.scene_tree->node.y + geo_box.y) +
						  ((edges & WLR_EDGE_BOTTOM) ? geo_box.height : 0);
		server->grab_x = server->cursor->wlr_cursor->x - border_x;
		server->grab_y = server->cursor->wlr_cursor->y - border_y;

		server->grab_geobox = geo_box;
		server->grab_geobox.x += toplevel->object.scene_tree->node.x;
		server->grab_geobox.y += toplevel->object.scene_tree->node.y;

		server->resize_edges = edges;

		// TODO: RESIZE
		toplevel->object.width = server->grab_geobox.width + 2 * BORDER_WIDTH;
		toplevel->object.height = server->grab_geobox.height + 2 * BORDER_WIDTH;
		xdg_resize(toplevel, toplevel->object.width, toplevel->object.height);
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
	comp_toplevel_begin_interactive(toplevel, COMP_CURSOR_MOVE, 0);
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
	comp_toplevel_begin_interactive(toplevel, COMP_CURSOR_RESIZE, event->edges);
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
	wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
}

void xdg_new_xdg_surface(struct wl_listener *listener, void *data) {
	/* This event is raised when wlr_xdg_shell receives a new xdg surface from a
	 * client, either a toplevel (application window) or popup. */
	struct comp_server *server =
		wl_container_of(listener, server, new_xdg_surface);
	struct wlr_xdg_surface *xdg_surface = data;

	/* Allocate a tinywl_toplevel for this surface */
	struct comp_toplevel *toplevel = calloc(1, sizeof(*toplevel));
	toplevel->server = server;
	toplevel->focused = false;
	toplevel->using_csd = true;
	toplevel->output = get_active_output(server);
	/* Set the scene_nodes decoration data */
	toplevel->opacity = 1;
	toplevel->corner_radius = EFFECTS_CORNER_RADII;
	toplevel->shadow_data = shadow_data_get_default();
	toplevel->shadow_data.enabled = true;

	switch (xdg_surface->role) {
	case WLR_XDG_SURFACE_ROLE_NONE:
		// Ignore surfaces with the role none
		free(toplevel);
		return;
	case WLR_XDG_SURFACE_ROLE_POPUP:;
		/* We must add xdg popups to the scene graph so they get rendered. The
		 * wlroots scene graph provides a helper for this, but to use it we must
		 * provide the proper parent scene node of the xdg popup. To enable
		 * this, we always set the user data field of xdg_surfaces to the
		 * corresponding scene node. */
		struct wlr_xdg_surface *parent =
			wlr_xdg_surface_try_from_wlr_surface(xdg_surface->popup->parent);
		assert(parent != NULL);
		struct wlr_scene_tree *parent_tree = parent->data;

		toplevel->tiling_mode = COMP_TILING_MODE_FLOATING;

		toplevel->xdg_toplevel = NULL;
		toplevel->xdg_popup = xdg_surface->popup;
		toplevel->xdg_scene_tree = wlr_scene_xdg_surface_create(
			parent_tree, toplevel->xdg_popup->base);
		toplevel->xdg_scene_tree->node.data = toplevel;
		xdg_surface->data = toplevel->xdg_scene_tree;

		/* Listen to the various events it can emit */
		toplevel->map.notify = xdg_popup_map;
		wl_signal_add(&xdg_surface->surface->events.map, &toplevel->map);
		toplevel->destroy.notify = xdg_popup_destroy;
		wl_signal_add(&xdg_surface->events.destroy, &toplevel->destroy);
		break;
	case WLR_XDG_SURFACE_ROLE_TOPLEVEL:
		toplevel->xdg_toplevel = xdg_surface->toplevel;
		toplevel->xdg_popup = NULL;

		toplevel->fullscreen = toplevel->xdg_toplevel->pending.fullscreen;

		// Add the toplevel to the tiled/floating layer
		// TODO: Check if it should be in the floating layer or not
		toplevel->tiling_mode = COMP_TILING_MODE_FLOATING;
		// TODO: Add other condition for tiled
		if (toplevel->fullscreen) {
			toplevel->tiling_mode = COMP_TILING_MODE_TILED;
		}
		struct comp_output *output = toplevel->output;
		struct comp_workspace *active_workspace =
			comp_output_get_active_ws(output, toplevel->fullscreen);
		switch (toplevel->tiling_mode) {
		case COMP_TILING_MODE_FLOATING:
			toplevel->object.scene_tree =
				alloc_tree(active_workspace->layers.floating);
			break;
		case COMP_TILING_MODE_TILED:
			toplevel->object.scene_tree =
				wlr_scene_tree_create(active_workspace->layers.lower);
			break;
		}

		/*
		 * Resize borders
		 */

		toplevel->edge = comp_edge_init(server, toplevel);

		/*
		 * Titlebar
		 */

		toplevel->titlebar = comp_titlebar_init(toplevel->server, toplevel);

		/*
		 * XDG Surface
		 */

		toplevel->xdg_scene_tree = wlr_scene_xdg_surface_create(
			toplevel->object.scene_tree, toplevel->xdg_toplevel->base);
		toplevel->xdg_scene_tree->node.data = &toplevel->object;
		toplevel->object.scene_tree->node.data = toplevel;
		toplevel->object.data = toplevel;
		toplevel->object.type = COMP_OBJECT_TYPE_TOPLEVEL;
		xdg_surface->data = toplevel->object.scene_tree;

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

		/* cotd */
		struct wlr_xdg_toplevel *xdg_toplevel = xdg_surface->toplevel;
		toplevel->request_move.notify = xdg_toplevel_request_move;
		wl_signal_add(&xdg_toplevel->events.request_move,
					  &toplevel->request_move);
		toplevel->request_resize.notify = xdg_toplevel_request_resize;
		wl_signal_add(&xdg_toplevel->events.request_resize,
					  &toplevel->request_resize);
		toplevel->request_maximize.notify = xdg_toplevel_request_maximize;
		wl_signal_add(&xdg_toplevel->events.request_maximize,
					  &toplevel->request_maximize);
		toplevel->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
		wl_signal_add(&xdg_toplevel->events.request_fullscreen,
					  &toplevel->request_fullscreen);
		break;
	}
}
