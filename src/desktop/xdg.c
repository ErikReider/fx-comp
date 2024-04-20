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
#include "comp/workspace.h"
#include "desktop/toplevel.h"
#include "desktop/widgets/titlebar.h"
#include "desktop/xdg.h"
#include "seat/cursor.h"

/*
 * Toplevel Implementation
 */

static struct wlr_box xdg_get_geometry(struct comp_toplevel *toplevel) {
	struct comp_xdg_toplevel *toplevel_xdg = toplevel->toplevel_xdg;
	struct wlr_box geometry;
	wlr_xdg_surface_get_geometry(toplevel_xdg->xdg_toplevel->base, &geometry);
	return geometry;
}

static void xdg_get_constraints(struct comp_toplevel *toplevel, int *min_width,
								int *max_width, int *min_height,
								int *max_height) {
	struct comp_xdg_toplevel *toplevel_xdg = toplevel->toplevel_xdg;
	struct wlr_xdg_toplevel_state *state = &toplevel_xdg->xdg_toplevel->current;
	*max_width = state->max_width;
	*max_height = state->max_height;
	*min_width = state->min_width;
	*min_height = state->min_height;
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

static void xdg_configure(struct comp_toplevel *toplevel, int width, int height,
						  int x, int y) {
	struct comp_xdg_toplevel *toplevel_xdg = toplevel->toplevel_xdg;
	wlr_xdg_toplevel_set_size(toplevel_xdg->xdg_toplevel, width, height);
}

static void xdg_set_size(struct comp_toplevel *toplevel, int width,
						 int height) {
	xdg_configure(toplevel, width, height, 0, 0);
}

static void xdg_set_resizing(struct comp_toplevel *toplevel, bool state) {
	struct comp_xdg_toplevel *toplevel_xdg = toplevel->toplevel_xdg;
	wlr_xdg_toplevel_set_resizing(toplevel_xdg->xdg_toplevel, state);
}

static void xdg_set_activated(struct comp_toplevel *toplevel, bool state) {
	struct comp_xdg_toplevel *toplevel_xdg = toplevel->toplevel_xdg;
	wlr_xdg_toplevel_set_activated(toplevel_xdg->xdg_toplevel, state);
}

static void xdg_set_fullscreen(struct comp_toplevel *toplevel, bool state) {
	struct comp_xdg_toplevel *toplevel_xdg = toplevel->toplevel_xdg;
	wlr_xdg_toplevel_set_fullscreen(toplevel_xdg->xdg_toplevel, state);
}

static void xdg_set_pid(struct comp_toplevel *toplevel) {
	struct wl_client *client = wl_resource_get_client(
		comp_toplevel_get_wlr_surface(toplevel)->resource);
	wl_client_get_credentials(client, &toplevel->pid, NULL, NULL);
}

static void xdg_close(struct comp_toplevel *toplevel) {
	struct comp_xdg_toplevel *toplevel_xdg = toplevel->toplevel_xdg;
	wlr_xdg_toplevel_send_close(toplevel_xdg->xdg_toplevel);
}

static void xdg_marked_dirty_cb(struct comp_toplevel *toplevel) {
	struct comp_xdg_toplevel *toplevel_xdg = toplevel->toplevel_xdg;
	if (toplevel_xdg->xdg_toplevel->base->client->shell->version >=
			XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION &&
		toplevel->decorated_size.width >= 0 &&
		toplevel->decorated_size.height >= 0) {
		wlr_xdg_toplevel_set_bounds(toplevel_xdg->xdg_toplevel,
									toplevel->decorated_size.width,
									toplevel->decorated_size.height);
	}
}

static const struct comp_toplevel_impl xdg_impl = {
	.get_geometry = xdg_get_geometry,
	.get_constraints = xdg_get_constraints,
	.get_wlr_surface = xdg_get_wlr_surface,
	.get_title = xdg_get_title,
	.configure = xdg_configure,
	.set_size = xdg_set_size,
	.set_resizing = xdg_set_resizing,
	.set_activated = xdg_set_activated,
	.set_fullscreen = xdg_set_fullscreen,
	.set_pid = xdg_set_pid,
	.marked_dirty_cb = xdg_marked_dirty_cb,
	.close = xdg_close,
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
	struct wlr_xdg_surface *xdg_surface = toplevel_xdg->xdg_toplevel->base;

	if (xdg_surface->initial_commit) {
		if (toplevel_xdg->xdg_decoration != NULL) {
			set_xdg_decoration_mode(toplevel_xdg->xdg_decoration);
		}
		wlr_xdg_surface_schedule_configure(xdg_surface);
		return;
	}

	if (!xdg_surface->surface->mapped) {
		return;
	}

	comp_toplevel_generic_commit(toplevel);
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
	/* Called when the xdg_toplevel is destroyed. */
	struct comp_xdg_toplevel *toplevel_xdg =
		wl_container_of(listener, toplevel_xdg, destroy);
	struct comp_toplevel *toplevel = toplevel_xdg->toplevel;

	toplevel->toplevel_xdg = NULL;

	wl_list_remove(&toplevel_xdg->map.link);
	wl_list_remove(&toplevel_xdg->unmap.link);
	wl_list_remove(&toplevel_xdg->commit.link);
	wl_list_remove(&toplevel_xdg->destroy.link);

	free(toplevel_xdg);

	comp_toplevel_destroy(toplevel);
}

static void xdg_toplevel_request_move(struct wl_listener *listener,
									  void *data) {
	struct comp_xdg_toplevel *toplevel_xdg =
		wl_container_of(listener, toplevel_xdg, request_move);
	struct comp_toplevel *toplevel = toplevel_xdg->toplevel;

	// TODO: Also check if tiled
	if (!toplevel->fullscreen) {
		comp_toplevel_begin_interactive(toplevel, COMP_CURSOR_MOVE, 0);
	}
}

static void xdg_toplevel_request_resize(struct wl_listener *listener,
										void *data) {
	struct wlr_xdg_toplevel_resize_event *event = data;
	struct comp_xdg_toplevel *toplevel_xdg =
		wl_container_of(listener, toplevel_xdg, request_resize);
	struct comp_toplevel *toplevel = toplevel_xdg->toplevel;

	// TODO: Also check if tiled
	if (!toplevel->fullscreen) {
		comp_toplevel_begin_interactive(toplevel, COMP_CURSOR_RESIZE,
										event->edges);
	}
}

static void xdg_toplevel_request_maximize(struct wl_listener *listener,
										  void *data) {
	struct comp_xdg_toplevel *toplevel_xdg =
		wl_container_of(listener, toplevel_xdg, request_maximize);
	wlr_xdg_surface_schedule_configure(toplevel_xdg->xdg_toplevel->base);
}

static void xdg_toplevel_request_fullscreen(struct wl_listener *listener,
											void *data) {
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

	comp_titlebar_change_title(toplevel->titlebar);
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
	struct comp_xdg_toplevel *toplevel_xdg =
		wl_container_of(listener, toplevel_xdg, map);
	struct comp_toplevel *toplevel = toplevel_xdg->toplevel;

	comp_toplevel_generic_map(toplevel);

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

	wl_list_remove(&toplevel_xdg->new_popup.link);
	wl_list_remove(&toplevel_xdg->request_move.link);
	wl_list_remove(&toplevel_xdg->request_resize.link);
	wl_list_remove(&toplevel_xdg->request_maximize.link);
	wl_list_remove(&toplevel_xdg->request_fullscreen.link);
	wl_list_remove(&toplevel_xdg->set_title.link);

	comp_toplevel_generic_unmap(toplevel);
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
	 * Scene
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
