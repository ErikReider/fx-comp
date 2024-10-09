#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/util/log.h>

#include "comp/output.h"
#include "comp/tiling_node.h"
#include "comp/transaction.h"
#include "comp/xwayland_mgr.h"
#include "desktop/toplevel.h"
#include "desktop/widgets/titlebar.h"
#include "desktop/xwayland.h"
#include "seat/seat.h"
#include "util.h"

// Credits goes to Sway for most of the implementation :D

static inline struct wlr_xwayland_surface *
get_xsurface(struct comp_toplevel *toplevel) {
	switch (toplevel->type) {
	case COMP_TOPLEVEL_TYPE_XWAYLAND:
		return toplevel->toplevel_xway->xwayland_surface;
	case COMP_TOPLEVEL_TYPE_XDG:
		abort();
	}

	return NULL;
}

/*
 * Toplevel Implementation
 */

static struct wlr_box xway_get_geometry(struct comp_toplevel *toplevel) {
	struct wlr_xwayland_surface *xsurface = get_xsurface(toplevel);
	return (struct wlr_box){
		.x = 0,
		.y = 0,
		.width = xsurface->width,
		.height = xsurface->height,
	};
}

static bool xway_get_using_csd(struct wlr_xwayland_surface *xsurface) {
	return xsurface->decorations != WLR_XWAYLAND_SURFACE_DECORATIONS_ALL;
}

static void xway_get_constraints(struct comp_toplevel *toplevel, int *min_width,
								 int *max_width, int *min_height,
								 int *max_height) {
	*min_width = 0, *max_width = 0, *min_height = 0, *max_height = 0;

	struct wlr_xwayland_surface *xsurface = get_xsurface(toplevel);
	xcb_size_hints_t *size_hints = xsurface->size_hints;

	if (size_hints == NULL) {
		*min_width = INT_MIN;
		*max_width = INT_MAX;
		*min_height = INT_MIN;
		*max_height = INT_MAX;
		return;
	}

	*min_width = size_hints->min_width > 0 ? size_hints->min_width : INT_MIN;
	*max_width = size_hints->max_width > 0 ? size_hints->max_width : INT_MAX;
	*min_height = size_hints->min_height > 0 ? size_hints->min_height : INT_MIN;
	*max_height = size_hints->max_height > 0 ? size_hints->max_height : INT_MAX;
}

static struct wlr_surface *
xway_get_wlr_surface(struct comp_toplevel *toplevel) {
	struct wlr_xwayland_surface *xsurface = get_xsurface(toplevel);
	return xsurface->surface;
}

static char *xway_get_title(struct comp_toplevel *toplevel) {
	struct wlr_xwayland_surface *xsurface = get_xsurface(toplevel);
	if (xsurface) {
		return xsurface->title;
	}
	return NULL;
}

static bool xway_get_always_floating(struct comp_toplevel *toplevel) {
	struct wlr_xwayland_surface *xsurface = get_xsurface(toplevel);
	struct comp_xwayland_mgr *xwayland = &server.xwayland_mgr;

	if (xsurface->modal) {
		return true;
	}

	for (size_t i = 0; i < xsurface->window_type_len; ++i) {
		xcb_atom_t type = xsurface->window_type[i];
		if (type == xwayland->atoms[NET_WM_WINDOW_TYPE_DIALOG] ||
			type == xwayland->atoms[NET_WM_WINDOW_TYPE_UTILITY] ||
			type == xwayland->atoms[NET_WM_WINDOW_TYPE_TOOLBAR] ||
			type == xwayland->atoms[NET_WM_WINDOW_TYPE_SPLASH]) {
			return true;
		}
	}

	xcb_size_hints_t *size_hints = xsurface->size_hints;
	if (size_hints != NULL && size_hints->min_width > 0 &&
		size_hints->min_height > 0 &&
		(size_hints->max_width == size_hints->min_width ||
		 size_hints->max_height == size_hints->min_height)) {
		return true;
	}

	return false;
}

static struct wlr_scene_tree *
xway_get_parent_tree(struct comp_toplevel *toplevel) {
	struct wlr_xwayland_surface *xsurface = get_xsurface(toplevel);
	return xsurface_get_parent_tree(xsurface);
}

// NOTE: Always assume that node-relative coords are provided, not xsurface
// coords
static uint32_t xway_configure(struct comp_toplevel *toplevel, int width,
							   int height, int x, int y) {
	struct wlr_xwayland_surface *xsurface = get_xsurface(toplevel);

	if (toplevel->state.workspace && toplevel->state.workspace->output) {
		double lx = 0, ly = 0;
		wlr_output_layout_output_coords(
			server.output_layout, toplevel->state.workspace->output->wlr_output,
			&lx, &ly);
		x -= (int16_t)lx;
		y -= (int16_t)ly;
	}

	wlr_xwayland_surface_configure(xsurface, x, y, width, height);

	// xwayland doesn't give us a serial for the configure
	return 0;
}

static void xway_set_resizing(struct comp_toplevel *toplevel, bool state) {
	// no-op
}

static void xway_set_activated(struct comp_toplevel *toplevel, bool state) {
	struct wlr_xwayland_surface *xsurface = get_xsurface(toplevel);

	if (state && xsurface->minimized) {
		wlr_xwayland_surface_set_minimized(xsurface, false);
	}

	wlr_xwayland_surface_activate(xsurface, state);
	wlr_xwayland_surface_restack(xsurface, NULL, XCB_STACK_MODE_ABOVE);
}

static void xway_set_fullscreen(struct comp_toplevel *toplevel, bool state) {
	struct wlr_xwayland_surface *xsurface = get_xsurface(toplevel);
	wlr_xwayland_surface_set_fullscreen(xsurface, state);
}

static bool xway_get_is_fullscreen(struct comp_toplevel *toplevel) {
	return toplevel->toplevel_xway->xwayland_surface->fullscreen;
}

static void xway_set_tiled(struct comp_toplevel *toplevel, bool state) {
	struct wlr_xwayland_surface *xsurface = get_xsurface(toplevel);
	wlr_xwayland_surface_set_maximized(xsurface, state);
}

static void xway_set_pid(struct comp_toplevel *toplevel) {
	toplevel->pid = get_xsurface(toplevel)->pid;
}

static void xway_close(struct comp_toplevel *toplevel) {
	struct wlr_xwayland_surface *xsurface = get_xsurface(toplevel);
	wlr_xwayland_surface_close(xsurface);
}

static void xway_marked_dirty_cb(struct comp_toplevel *toplevel) {
	// no-op
}

static bool should_run_transaction(struct comp_toplevel *toplevel) {
	struct wlr_xwayland_surface *xsurface =
		toplevel->toplevel_xway->xwayland_surface;
	struct wlr_surface_state *state = &xsurface->surface->current;
	struct comp_transaction_instruction *instruction =
		toplevel->object.instruction;

	// Convert the output node relative coords to scene root coords.
	// XWayland uses root coords, input hitboxes get all messed up when output
	// position is changed...
	double lx = 0, ly = 0;
	wlr_output_layout_output_coords(
		server.output_layout, toplevel->state.workspace->output->wlr_output,
		&lx, &ly);
	int x = instruction->state.x - (int)lx;
	int y = instruction->state.y - (int)ly;

	return instruction && x == xsurface->x && y == xsurface->y &&
		   instruction->state.width == state->width &&
		   instruction->state.height == state->height;
}

// Handles both regular and unmanaged XWayland
static const struct comp_toplevel_impl xwayland_impl = {
	.get_geometry = xway_get_geometry,
	.get_constraints = xway_get_constraints,
	.get_wlr_surface = xway_get_wlr_surface,
	.get_title = xway_get_title,
	.get_always_floating = xway_get_always_floating,
	.get_parent_tree = xway_get_parent_tree,
	.configure = xway_configure,
	.set_resizing = xway_set_resizing,
	.set_activated = xway_set_activated,
	.set_fullscreen = xway_set_fullscreen,
	.get_is_fullscreen = xway_get_is_fullscreen,
	.set_tiled = xway_set_tiled,
	.set_pid = xway_set_pid,
	.marked_dirty_cb = xway_marked_dirty_cb,
	.close = xway_close,
	.should_run_transaction = should_run_transaction,
};

/*
 * XWayland Toplevel
 */

static void xway_toplevel_request_fullscreen(struct wl_listener *listener,
											 void *data) {
	struct comp_xwayland_toplevel *toplevel_xway =
		wl_container_of(listener, toplevel_xway, request_fullscreen);
	struct comp_toplevel *toplevel = toplevel_xway->toplevel;
	struct wlr_xwayland_surface *xsurface = toplevel_xway->xwayland_surface;
	if (xsurface->surface == NULL || !xsurface->surface->mapped) {
		return;
	}

	comp_toplevel_set_fullscreen(toplevel, xsurface->fullscreen);
}

static void xway_toplevel_request_minimize(struct wl_listener *listener,
										   void *data) {
	struct comp_xwayland_toplevel *toplevel_xway =
		wl_container_of(listener, toplevel_xway, request_minimize);
	struct comp_toplevel *toplevel = toplevel_xway->toplevel;
	struct wlr_xwayland_minimize_event *event = data;
	struct wlr_xwayland_surface *xsurface = toplevel_xway->xwayland_surface;
	if (xsurface->surface == NULL || !xsurface->surface->mapped) {
		return;
	}

	bool focused = server.seat->focused_toplevel == toplevel;
	wlr_xwayland_surface_set_minimized(xsurface, !focused && event->minimize);
}

static void xway_toplevel_request_activate(struct wl_listener *listener,
										   void *data) {
	struct comp_xwayland_toplevel *toplevel_xway =
		wl_container_of(listener, toplevel_xway, request_activate);
	struct wlr_xwayland_surface *xsurface = toplevel_xway->xwayland_surface;
	if (xsurface->surface == NULL || !xsurface->surface->mapped) {
		return;
	}

	// TODO: XWayland activate

	int16_t x = xsurface->x;
	int16_t y = xsurface->y;
	// Make surface coords output scene node relative, instead of scene root
	// relative. Helps when output position isn't 0
	struct comp_toplevel *toplevel = toplevel_xway->toplevel;
	if (toplevel->state.workspace && toplevel->state.workspace->output) {
		double lx = 0, ly = 0;
		wlr_output_layout_output_coords(
			server.output_layout, toplevel->state.workspace->output->wlr_output,
			&lx, &ly);
		x += (int16_t)lx;
		y += (int16_t)ly;
	}
	comp_toplevel_configure(toplevel_xway->toplevel, xsurface->width,
							xsurface->height, x, y);

	comp_object_mark_dirty(&toplevel_xway->toplevel->object);
	comp_transaction_commit_dirty(true);
}

static void xway_toplevel_request_move(struct wl_listener *listener,
									   void *data) {
	struct comp_xwayland_toplevel *toplevel_xway =
		wl_container_of(listener, toplevel_xway, request_move);
	struct comp_toplevel *toplevel = toplevel_xway->toplevel;
	struct wlr_xwayland_surface *xsurface = toplevel_xway->xwayland_surface;
	if (xsurface->surface == NULL || !xsurface->surface->mapped) {
		return;
	}

	// TODO: Also check if tiled
	if (!toplevel->fullscreen) {
		comp_toplevel_begin_interactive(toplevel, COMP_CURSOR_MOVE, 0);
	}
}

static void xway_toplevel_request_resize(struct wl_listener *listener,
										 void *data) {
	struct comp_xwayland_toplevel *toplevel_xway =
		wl_container_of(listener, toplevel_xway, request_resize);
	struct comp_toplevel *toplevel = toplevel_xway->toplevel;
	struct wlr_xwayland_surface *xsurface = toplevel_xway->xwayland_surface;
	if (xsurface->surface == NULL || !xsurface->surface->mapped) {
		return;
	}

	// TODO: Also check if tiled
	if (!toplevel->fullscreen) {
		struct wlr_xwayland_resize_event *event = data;
		comp_toplevel_begin_interactive(toplevel, COMP_CURSOR_RESIZE,
										event->edges);
	}
}

static void xway_toplevel_set_title(struct wl_listener *listener, void *data) {
	struct comp_xwayland_toplevel *toplevel_xway =
		wl_container_of(listener, toplevel_xway, set_title);
	struct comp_toplevel *toplevel = toplevel_xway->toplevel;
	struct wlr_xwayland_surface *xsurface = toplevel_xway->xwayland_surface;
	if (xsurface->surface == NULL || !xsurface->surface->mapped) {
		return;
	}

	comp_titlebar_change_title(toplevel->titlebar);
}

static void xway_toplevel_set_startup_id(struct wl_listener *listener,
										 void *data) {
	struct comp_xwayland_toplevel *toplevel_xway =
		wl_container_of(listener, toplevel_xway, set_startup_id);
	struct wlr_xwayland_surface *xsurface = toplevel_xway->xwayland_surface;
	if (xsurface->startup_id == NULL) {
		return;
	}

	// TODO: XWayland set startup id and use xdg_activation_v1
}

static void xway_toplevel_set_hints(struct wl_listener *listener, void *data) {
	struct comp_xwayland_toplevel *toplevel_xway =
		wl_container_of(listener, toplevel_xway, set_hints);
	struct wlr_xwayland_surface *xsurface = toplevel_xway->xwayland_surface;
	if (xsurface->surface == NULL || !xsurface->surface->mapped) {
		return;
	}

	// TODO: XWayland urgency
}

static void xway_toplevel_set_decorations(struct wl_listener *listener,
										  void *data) {
	struct comp_xwayland_toplevel *toplevel_xway =
		wl_container_of(listener, toplevel_xway, set_decorations);
	struct comp_toplevel *toplevel = toplevel_xway->toplevel;
	struct wlr_xwayland_surface *xsurface = toplevel_xway->xwayland_surface;

	toplevel->using_csd = xway_get_using_csd(xsurface);
	comp_object_mark_dirty(&toplevel->object);
	comp_transaction_commit_dirty(true);
	if (toplevel->tiling_mode == COMP_TILING_MODE_TILED &&
		toplevel->tiling_node) {
		tiling_node_mark_workspace_dirty(toplevel->state.workspace);
	}
}

static void xway_toplevel_commit(struct wl_listener *listener, void *data) {
	struct comp_xwayland_toplevel *toplevel_xway =
		wl_container_of(listener, toplevel_xway, commit);
	struct comp_toplevel *toplevel = toplevel_xway->toplevel;

	comp_toplevel_generic_commit(toplevel);
}

static void xway_toplevel_destroy(struct wl_listener *listener, void *data) {
	struct comp_xwayland_toplevel *toplevel_xway =
		wl_container_of(listener, toplevel_xway, destroy);
	struct comp_toplevel *toplevel = toplevel_xway->toplevel;

	if (toplevel_xway->xwayland_surface->surface) {
		listener_emit(&toplevel_xway->unmap, NULL);
		listener_remove(&toplevel_xway->commit);
	}

	comp_toplevel_destroy(toplevel);

	toplevel_xway->xwayland_surface = NULL;
	toplevel->toplevel_xway = NULL;

	listener_remove(&toplevel_xway->destroy);
	listener_remove(&toplevel_xway->request_configure);
	listener_remove(&toplevel_xway->request_fullscreen);
	listener_remove(&toplevel_xway->request_minimize);
	listener_remove(&toplevel_xway->request_move);
	listener_remove(&toplevel_xway->request_resize);
	listener_remove(&toplevel_xway->request_activate);
	listener_remove(&toplevel_xway->set_title);
	listener_remove(&toplevel_xway->set_startup_id);
	listener_remove(&toplevel_xway->set_hints);
	listener_remove(&toplevel_xway->set_decorations);
	listener_remove(&toplevel_xway->associate);
	listener_remove(&toplevel_xway->dissociate);
	listener_remove(&toplevel_xway->override_redirect);

	free(toplevel_xway);
}

static void xway_toplevel_request_configure(struct wl_listener *listener,
											void *data) {
	struct comp_xwayland_toplevel *toplevel_xway =
		wl_container_of(listener, toplevel_xway, request_configure);
	struct comp_toplevel *toplevel = toplevel_xway->toplevel;

	struct wlr_xwayland_surface_configure_event *event = data;
	struct wlr_xwayland_surface *xsurface = toplevel_xway->xwayland_surface;
	if (xsurface->surface == NULL || !xsurface->surface->mapped) {
		wlr_xwayland_surface_configure(xsurface, event->x, event->y,
									   event->width, event->height);
		return;
	}

	if (toplevel->tiling_mode == COMP_TILING_MODE_FLOATING) {
		toplevel->natural_width = event->width;
		toplevel->natural_height = event->height;
		comp_toplevel_center(toplevel, toplevel->natural_width,
							 toplevel->natural_height, false);
		xway_configure(toplevel, toplevel->pending_state.width,
					   toplevel->pending_state.width, toplevel->pending_state.x,
					   toplevel->pending_state.y);
		comp_object_mark_dirty(&toplevel->object);
	} else {
		xway_configure(toplevel, toplevel->state.width, toplevel->state.height,
					   toplevel->state.x, toplevel->state.y);
	}
}

static void handle_surface_tree_destroy(struct wl_listener *listener,
										void *data) {
	struct comp_xwayland_toplevel *toplevel_xway =
		wl_container_of(listener, toplevel_xway, surface_tree_destroy);
	toplevel_xway->toplevel->toplevel_scene_tree = NULL;
}

static void xway_toplevel_map(struct wl_listener *listener, void *data) {
	struct comp_xwayland_toplevel *toplevel_xway =
		wl_container_of(listener, toplevel_xway, map);
	struct comp_toplevel *toplevel = toplevel_xway->toplevel;

	// Insert the surface into the scene
	toplevel->toplevel_scene_tree = wlr_scene_subsurface_tree_create(
		toplevel->object.content_tree,
		toplevel_xway->xwayland_surface->surface);
	toplevel->toplevel_scene_tree->node.data = &toplevel->object;
	if (toplevel->toplevel_scene_tree) {
		listener_connect(&toplevel->toplevel_scene_tree->node.events.destroy,
						 &toplevel_xway->surface_tree_destroy,
						 handle_surface_tree_destroy);
	}

	wlr_scene_node_raise_to_top(&toplevel->saved_scene_tree->node);
	wlr_scene_node_raise_to_top(&toplevel->decoration_scene_tree->node);

	listener_connect(&toplevel_xway->xwayland_surface->surface->events.commit,
					 &toplevel_xway->commit, xway_toplevel_commit);

	comp_toplevel_generic_map(toplevel);
}

static void xway_toplevel_unmap(struct wl_listener *listener, void *data) {
	/* Called when the surface is unmapped, and should no longer be shown. */
	struct comp_xwayland_toplevel *toplevel_xway =
		wl_container_of(listener, toplevel_xway, unmap);
	struct comp_toplevel *toplevel = toplevel_xway->toplevel;

	listener_remove(&toplevel_xway->commit);
	listener_remove(&toplevel_xway->surface_tree_destroy);

	comp_toplevel_generic_unmap(toplevel);
}

static void xway_toplevel_associate(struct wl_listener *listener, void *data) {
	struct comp_xwayland_toplevel *toplevel_xway =
		wl_container_of(listener, toplevel_xway, associate);
	struct wlr_xwayland_surface *xsurface = toplevel_xway->xwayland_surface;

	listener_connect(&xsurface->surface->events.unmap, &toplevel_xway->unmap,
					 xway_toplevel_unmap);
	listener_connect(&xsurface->surface->events.map, &toplevel_xway->map,
					 xway_toplevel_map);
}

static void xway_toplevel_dissociate(struct wl_listener *listener, void *data) {
	struct comp_xwayland_toplevel *toplevel_xway =
		wl_container_of(listener, toplevel_xway, dissociate);
	listener_remove(&toplevel_xway->map);
	listener_remove(&toplevel_xway->unmap);
}

static void xway_toplevel_override_redirect(struct wl_listener *listener,
											void *data) {
	struct comp_xwayland_toplevel *toplevel_xway =
		wl_container_of(listener, toplevel_xway, override_redirect);
	struct wlr_xwayland_surface *xsurface = toplevel_xway->xwayland_surface;

	bool associated = xsurface->surface != NULL;
	bool mapped = associated && xsurface->surface->mapped;
	if (mapped) {
		xway_toplevel_unmap(&toplevel_xway->unmap, NULL);
	}
	if (associated) {
		xway_toplevel_dissociate(&toplevel_xway->dissociate, NULL);
	}

	xway_toplevel_destroy(&toplevel_xway->destroy, NULL);
	xsurface->data = NULL;

	struct comp_xwayland_unmanaged *unmanaged = xway_create_unmanaged(xsurface);
	if (associated) {
		listener_emit(&unmanaged->associate, NULL);
	}
	if (mapped) {
		listener_emit(&unmanaged->map, NULL);
	}
}

struct comp_xwayland_toplevel *
xway_create_toplevel(struct wlr_xwayland_surface *xsurface) {
	struct comp_xwayland_toplevel *toplevel_xway =
		calloc(1, sizeof(*toplevel_xway));
	if (!toplevel_xway) {
		wlr_log(WLR_ERROR, "Could not allocate comp_xwayland_toplevel");
		return NULL;
	}
	toplevel_xway->xwayland_surface = xsurface;

	bool is_fullscreen = xsurface->fullscreen;
	// Add the toplevel to the tiled/floating layer
	enum comp_tiling_mode tiling_mode = COMP_TILING_MODE_TILED;

	struct comp_output *output = get_active_output(&server);
	struct comp_workspace *workspace =
		comp_output_get_active_ws(output, is_fullscreen);

	/* Allocate a comp_toplevel for this surface */
	struct comp_toplevel *toplevel =
		comp_toplevel_init(output, workspace, COMP_TOPLEVEL_TYPE_XWAYLAND,
						   tiling_mode, is_fullscreen, &xwayland_impl);
	toplevel->using_csd = xway_get_using_csd(xsurface);
	toplevel->fullscreen = is_fullscreen;
	toplevel->toplevel_xway = toplevel_xway;
	toplevel_xway->toplevel = toplevel;
	toplevel_xway->xwayland_surface->data = toplevel->object.scene_tree;

	// Move into the predefined layer
	comp_toplevel_move_into_parent_tree(toplevel, NULL);

	/*
	 * Initialize listeners
	 */

	listener_init(&toplevel_xway->surface_tree_destroy);
	listener_init(&toplevel_xway->commit);
	listener_init(&toplevel_xway->request_move);
	listener_init(&toplevel_xway->request_resize);
	listener_init(&toplevel_xway->request_maximize);
	listener_init(&toplevel_xway->request_minimize);
	listener_init(&toplevel_xway->request_configure);
	listener_init(&toplevel_xway->request_fullscreen);
	listener_init(&toplevel_xway->request_activate);
	listener_init(&toplevel_xway->set_title);
	listener_init(&toplevel_xway->set_startup_id);
	listener_init(&toplevel_xway->set_hints);
	listener_init(&toplevel_xway->set_decorations);
	listener_init(&toplevel_xway->associate);
	listener_init(&toplevel_xway->dissociate);
	listener_init(&toplevel_xway->map);
	listener_init(&toplevel_xway->unmap);
	listener_init(&toplevel_xway->destroy);
	listener_init(&toplevel_xway->override_redirect);

	/*
	 * Events
	 */

	listener_connect(&xsurface->events.destroy, &toplevel_xway->destroy,
					 xway_toplevel_destroy);

	listener_connect(&xsurface->events.request_configure,
					 &toplevel_xway->request_configure,
					 xway_toplevel_request_configure);

	listener_connect(&xsurface->events.request_fullscreen,
					 &toplevel_xway->request_fullscreen,
					 xway_toplevel_request_fullscreen);

	listener_connect(&xsurface->events.request_minimize,
					 &toplevel_xway->request_minimize,
					 xway_toplevel_request_minimize);

	listener_connect(&xsurface->events.request_activate,
					 &toplevel_xway->request_activate,
					 xway_toplevel_request_activate);

	listener_connect(&xsurface->events.request_move,
					 &toplevel_xway->request_move, xway_toplevel_request_move);

	listener_connect(&xsurface->events.request_resize,
					 &toplevel_xway->request_resize,
					 xway_toplevel_request_resize);

	listener_connect(&xsurface->events.set_title, &toplevel_xway->set_title,
					 xway_toplevel_set_title);

	listener_connect(&xsurface->events.set_startup_id,
					 &toplevel_xway->set_startup_id,
					 xway_toplevel_set_startup_id);

	listener_connect(&xsurface->events.set_hints, &toplevel_xway->set_hints,
					 xway_toplevel_set_hints);

	listener_connect(&xsurface->events.set_decorations,
					 &toplevel_xway->set_decorations,
					 xway_toplevel_set_decorations);

	listener_connect(&xsurface->events.associate, &toplevel_xway->associate,
					 xway_toplevel_associate);

	listener_connect(&xsurface->events.dissociate, &toplevel_xway->dissociate,
					 xway_toplevel_dissociate);

	listener_connect(&xsurface->events.set_override_redirect,
					 &toplevel_xway->override_redirect,
					 xway_toplevel_override_redirect);

	return toplevel_xway;
}
