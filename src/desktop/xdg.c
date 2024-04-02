#include <limits.h>
#include <scenefx/types/fx/shadow_data.h>
#include <scenefx/types/wlr_scene.h>
#include <stdbool.h>
#include <stddef.h>
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

static struct wlr_box xdg_get_geometry(struct comp_toplevel *toplevel) {
	struct comp_xdg_toplevel *toplevel_xdg = toplevel->toplevel_xdg;
	struct wlr_box geometry;
	wlr_xdg_surface_get_geometry(toplevel_xdg->xdg_toplevel->base, &geometry);
	return geometry;
}

static void xdg_unfocus(struct comp_toplevel *toplevel) {
}

static struct wlr_surface *xdg_get_wlr_surface(struct comp_toplevel *toplevel) {
	struct comp_xdg_toplevel *toplevel_xdg = toplevel->toplevel_xdg;
	return toplevel_xdg->xdg_toplevel->base->surface;
}

static char *xdg_get_title(struct comp_toplevel *toplevel) {
	struct comp_xdg_toplevel *toplevel_xdg = toplevel->toplevel_xdg;
	if (toplevel_xdg->xdg_toplevel) {
		return toplevel_xdg->xdg_toplevel->title;
	}
	return NULL;
}

static void xdg_set_size(struct comp_toplevel *toplevel, int width,
						 int height) {
	struct comp_xdg_toplevel *toplevel_xdg = toplevel->toplevel_xdg;
	wlr_xdg_toplevel_set_size(toplevel_xdg->xdg_toplevel, width, height);
}

static void xdg_set_activated(struct comp_toplevel *toplevel, bool state) {
	struct comp_xdg_toplevel *toplevel_xdg = toplevel->toplevel_xdg;
	wlr_xdg_toplevel_set_activated(toplevel_xdg->xdg_toplevel, state);
}

static void xdg_set_fullscreen(struct comp_toplevel *toplevel, bool state) {
	struct comp_xdg_toplevel *toplevel_xdg = toplevel->toplevel_xdg;
	wlr_xdg_toplevel_set_fullscreen(toplevel_xdg->xdg_toplevel, state);
}

static void xdg_close(struct comp_toplevel *toplevel) {
	struct comp_xdg_toplevel *toplevel_xdg = toplevel->toplevel_xdg;
	wlr_xdg_toplevel_send_close(toplevel_xdg->xdg_toplevel);
}

static void xdg_update(struct comp_toplevel *toplevel, int width, int height) {
	struct comp_titlebar *titlebar = toplevel->titlebar;

	struct comp_xdg_toplevel *toplevel_xdg = toplevel->toplevel_xdg;
	if (toplevel_xdg->xdg_toplevel->base->client->shell->version >=
			XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION &&
		toplevel->object.width >= 0 && toplevel->object.height >= 0) {
		wlr_xdg_toplevel_set_bounds(toplevel_xdg->xdg_toplevel,
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

		struct wlr_xdg_toplevel_state *state =
			&toplevel_xdg->xdg_toplevel->current;
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
	}
}

static const struct comp_toplevel_impl xdg_impl = {
	.get_geometry = xdg_get_geometry,
	.get_wlr_surface = xdg_get_wlr_surface,
	.get_title = xdg_get_title,
	.set_size = xdg_set_size,
	.set_activated = xdg_set_activated,
	.set_fullscreen = xdg_set_fullscreen,
	.update = xdg_update,
	.close = xdg_close,
	.unfocus = xdg_unfocus,
};

/*
 * XDG Popup
 */

static void handle_new_popup(struct wl_listener *listener, void *data) {
	struct comp_xdg_toplevel *toplevel_xdg =
		wl_container_of(listener, toplevel_xdg, new_popup);
	struct wlr_xdg_popup *wlr_popup = data;
	// TODO: Somehow raise above comp_resize_edge
	xdg_new_xdg_popup(wlr_popup, &toplevel_xdg->toplevel->object,
					  toplevel_xdg->toplevel->toplevel_scene_tree);
}

/*
 * XDG Toplevel
 */

static void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
	/* Called when the surface is mapped, or ready to display on-screen. */
	struct comp_xdg_toplevel *toplevel_xdg =
		wl_container_of(listener, toplevel_xdg, commit);
	struct comp_toplevel *toplevel = toplevel_xdg->toplevel;

	// Set geometry
	struct wlr_box geometry = xdg_get_geometry(toplevel);
	toplevel->object.width = geometry.width;
	toplevel->object.height = geometry.height;

	if (!toplevel->fullscreen) {
		toplevel->object.width += 2 * BORDER_WIDTH;
		toplevel->object.height += BORDER_WIDTH;
		if (!comp_titlebar_should_be_shown(toplevel)) {
			toplevel->object.height += BORDER_WIDTH;
		}
	} else {
		wlr_scene_node_set_position(&toplevel->object.scene_tree->node, 0, 0);
	}

	comp_toplevel_update(toplevel, toplevel->object.width,
						 toplevel->object.height);
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
	/* Called when the xdg_toplevel is destroyed. */
	struct comp_xdg_toplevel *toplevel_xdg =
		wl_container_of(listener, toplevel_xdg, destroy);
	struct comp_toplevel *toplevel = toplevel_xdg->toplevel;

	wl_list_remove(&toplevel_xdg->map.link);
	wl_list_remove(&toplevel_xdg->unmap.link);
	wl_list_remove(&toplevel_xdg->commit.link);
	wl_list_remove(&toplevel_xdg->destroy.link);

	free(toplevel_xdg);

	comp_toplevel_destroy(toplevel);
}

static void xdg_toplevel_request_move(struct wl_listener *listener,
									  void *data) {
	/* This event is raised when a client would like to begin an interactive
	 * move, typically because the user clicked on their client-side
	 * decorations. Note that a more sophisticated compositor should check the
	 * provided serial against a list of button press serials sent to this
	 * client, to prevent the client from requesting this whenever they want. */
	struct comp_xdg_toplevel *toplevel_xdg =
		wl_container_of(listener, toplevel_xdg, request_move);
	struct comp_toplevel *toplevel = toplevel_xdg->toplevel;

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
	struct comp_xdg_toplevel *toplevel_xdg =
		wl_container_of(listener, toplevel_xdg, request_resize);
	struct comp_toplevel *toplevel = toplevel_xdg->toplevel;

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
	struct comp_xdg_toplevel *toplevel_xdg =
		wl_container_of(listener, toplevel_xdg, request_maximize);
	wlr_xdg_surface_schedule_configure(toplevel_xdg->xdg_toplevel->base);
}

static void xdg_toplevel_request_fullscreen(struct wl_listener *listener,
											void *data) {
	/* Just as with request_maximize, we must send a configure here. */
	struct comp_xdg_toplevel *toplevel_xdg =
		wl_container_of(listener, toplevel_xdg, request_fullscreen);
	struct comp_toplevel *toplevel = toplevel_xdg->toplevel;
	struct wlr_xdg_toplevel *xdg_toplevel = toplevel_xdg->xdg_toplevel;

	if (!xdg_toplevel->base->surface->mapped) {
		return;
	}

	struct wlr_xdg_toplevel_requested *req = &xdg_toplevel->requested;
	comp_toplevel_set_fullscreen(toplevel, req->fullscreen);
}

static void xdg_toplevel_set_title(struct wl_listener *listener, void *data) {
	struct comp_xdg_toplevel *toplevel_xdg =
		wl_container_of(listener, toplevel_xdg, set_title);
	struct comp_toplevel *toplevel = toplevel_xdg->toplevel;

	if (comp_titlebar_should_be_shown(toplevel)) {
		comp_widget_draw(&toplevel->titlebar->widget);
	}
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
	/* Called when the surface is mapped, or ready to display on-screen. */
	struct comp_xdg_toplevel *toplevel_xdg =
		wl_container_of(listener, toplevel_xdg, map);
	struct comp_toplevel *toplevel = toplevel_xdg->toplevel;
	wl_list_insert(&toplevel->workspace->toplevels, &toplevel->workspace_link);

	comp_toplevel_apply_effects(toplevel->toplevel_scene_tree, toplevel);

	// Set geometry
	struct wlr_box geometry = xdg_get_geometry(toplevel);
	toplevel->object.width = geometry.width + 2 * BORDER_WIDTH;
	toplevel->object.height = geometry.height + BORDER_WIDTH;
	if (!comp_titlebar_should_be_shown(toplevel)) {
		toplevel->object.height += BORDER_WIDTH;
	}

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

		comp_toplevel_update(toplevel, toplevel->object.width,
							 toplevel->object.height);
	} else {
		// Tile the new toplevel
		// TODO: Tile
	}

	wl_list_insert(server.seat->focus_order.prev, &toplevel->focus_link);

	wlr_scene_node_set_enabled(&toplevel->object.scene_tree->node, true);
	comp_seat_surface_focus(&toplevel->object,
							toplevel_xdg->xdg_toplevel->base->surface);

	struct wlr_xdg_toplevel *xdg_toplevel = toplevel_xdg->xdg_toplevel;
	toplevel_xdg->new_popup.notify = handle_new_popup;
	wl_signal_add(&xdg_toplevel->base->events.new_popup,
				  &toplevel_xdg->new_popup);
	toplevel_xdg->request_move.notify = xdg_toplevel_request_move;
	wl_signal_add(&xdg_toplevel->events.request_move,
				  &toplevel_xdg->request_move);
	toplevel_xdg->request_resize.notify = xdg_toplevel_request_resize;
	wl_signal_add(&xdg_toplevel->events.request_resize,
				  &toplevel_xdg->request_resize);
	toplevel_xdg->request_maximize.notify = xdg_toplevel_request_maximize;
	wl_signal_add(&xdg_toplevel->events.request_maximize,
				  &toplevel_xdg->request_maximize);
	toplevel_xdg->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
	wl_signal_add(&xdg_toplevel->events.request_fullscreen,
				  &toplevel_xdg->request_fullscreen);
	toplevel_xdg->set_title.notify = xdg_toplevel_set_title;
	wl_signal_add(&xdg_toplevel->events.set_title, &toplevel_xdg->set_title);
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
	/* Called when the surface is unmapped, and should no longer be shown. */
	struct comp_xdg_toplevel *toplevel_xdg =
		wl_container_of(listener, toplevel_xdg, unmap);
	struct comp_toplevel *toplevel = toplevel_xdg->toplevel;

	if (toplevel->fullscreen) {
		comp_toplevel_set_fullscreen(toplevel, false);
	}

	/* Reset the cursor mode if the grabbed toplevel was unmapped. */
	if (toplevel == toplevel->server->seat->grabbed_toplevel) {
		comp_cursor_reset_cursor_mode(toplevel->server->seat);
	}

	wl_list_remove(&toplevel->workspace_link);
	wl_list_remove(&toplevel->focus_link);

	wl_list_remove(&toplevel_xdg->new_popup.link);
	wl_list_remove(&toplevel_xdg->request_move.link);
	wl_list_remove(&toplevel_xdg->request_resize.link);
	wl_list_remove(&toplevel_xdg->request_maximize.link);
	wl_list_remove(&toplevel_xdg->request_fullscreen.link);
	wl_list_remove(&toplevel_xdg->set_title.link);

	comp_seat_surface_unfocus(comp_toplevel_get_wlr_surface(toplevel), true);
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

	struct comp_xdg_toplevel *toplevel_xdg = calloc(1, sizeof(*toplevel_xdg));
	toplevel_xdg->xdg_toplevel = xdg_surface->toplevel;
	bool is_fullscreen = toplevel_xdg->xdg_toplevel->requested.fullscreen;

	// Add the toplevel to the tiled/floating layer
	// TODO: Check if it should be in the floating layer or not
	enum comp_tiling_mode tiling_mode = COMP_TILING_MODE_FLOATING;
	// TODO: Add other condition for tiled
	if (is_fullscreen) {
		tiling_mode = COMP_TILING_MODE_TILED;
	}

	struct comp_output *output = get_active_output(server);
	struct comp_workspace *workspace =
		comp_output_get_active_ws(output, is_fullscreen);

	/* Allocate a comp_toplevel for this surface */
	struct comp_toplevel *toplevel =
		comp_toplevel_init(output, workspace, COMP_TOPLEVEL_TYPE_XDG,
						   tiling_mode, is_fullscreen, &xdg_impl);
	toplevel->using_csd = true;
	toplevel->fullscreen = is_fullscreen;
	toplevel->toplevel_xdg = toplevel_xdg;
	toplevel_xdg->toplevel = toplevel;

	/*
	 * XDG Surface
	 */

	// TODO: event.output_enter/output_leave for primary output
	toplevel->toplevel_scene_tree = wlr_scene_xdg_surface_create(
		toplevel->object.scene_tree, toplevel_xdg->xdg_toplevel->base);
	toplevel->toplevel_scene_tree->node.data = &toplevel->object;
	xdg_surface->data = toplevel->object.scene_tree;

	/*
	 * Events
	 */

	/* Listen to the various events it can emit */
	toplevel_xdg->map.notify = xdg_toplevel_map;
	wl_signal_add(&xdg_surface->surface->events.map, &toplevel_xdg->map);
	toplevel_xdg->unmap.notify = xdg_toplevel_unmap;
	wl_signal_add(&xdg_surface->surface->events.unmap, &toplevel_xdg->unmap);
	toplevel_xdg->commit.notify = xdg_toplevel_commit;
	wl_signal_add(&xdg_surface->surface->events.commit, &toplevel_xdg->commit);
	toplevel_xdg->destroy.notify = xdg_toplevel_destroy;
	wl_signal_add(&xdg_surface->events.destroy, &toplevel_xdg->destroy);
}
