#include <stdio.h>
#include <stdlib.h>
#include <wayland-util.h>

#include "comp/object.h"
#include "comp/tiling_node.h"
#include "desktop/toplevel.h"
#include "desktop/xdg.h"
#include "desktop/xdg_decoration.h"

void set_xdg_decoration_mode(struct comp_xdg_decoration *deco) {
	struct comp_xdg_toplevel *toplevel_xdg = deco->toplevel;
	struct comp_toplevel *toplevel = toplevel_xdg->toplevel;

	enum wlr_xdg_toplevel_decoration_v1_mode mode =
		WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
	enum wlr_xdg_toplevel_decoration_v1_mode client_mode =
		deco->wlr_xdg_decoration->requested_mode;

	bool floating = toplevel->tiling_mode == COMP_TILING_MODE_FLOATING;
	toplevel->using_csd =
		client_mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;

	comp_toplevel_refresh_titlebar(toplevel);
	comp_toplevel_mark_dirty(toplevel, false);

	if (floating && client_mode) {
		mode = client_mode;
	}

	if (!floating && toplevel->tiling_node && toplevel->state.workspace) {
		tiling_node_mark_workspace_dirty(toplevel->state.workspace);
	}

	if (toplevel_xdg->xdg_toplevel->base->initialized) {
		wlr_xdg_toplevel_decoration_v1_set_mode(deco->wlr_xdg_decoration, mode);
	}
}

static void xdg_decoration_handle_destroy(struct wl_listener *listener,
										  void *data) {
	struct comp_xdg_decoration *deco = wl_container_of(listener, deco, destroy);
	if (deco->toplevel) {
		deco->toplevel->xdg_decoration = NULL;
	}
	wl_list_remove(&deco->destroy.link);
	wl_list_remove(&deco->request_mode.link);
	wl_list_remove(&deco->link);
	free(deco);
}

static void xdg_decoration_handle_request_mode(struct wl_listener *listener,
											   void *data) {
	struct comp_xdg_decoration *deco =
		wl_container_of(listener, deco, request_mode);
	set_xdg_decoration_mode(deco);
}

/* Ensure that the XDG toplevels use our server-side decorations */
void handle_xdg_decoration(struct wl_listener *listener, void *data) {
	struct wlr_xdg_toplevel_decoration_v1 *wlr_deco = data;
	struct wlr_scene_tree *tree = wlr_deco->toplevel->base->data;
	struct comp_object *object = tree->node.data;
	if (!object || object->type != COMP_OBJECT_TYPE_TOPLEVEL) {
		return;
	}
	struct comp_toplevel *comp_toplevel = object->data;
	if (comp_toplevel->type != COMP_TOPLEVEL_TYPE_XDG) {
		return;
	}
	struct comp_xdg_toplevel *toplevel_xdg = comp_toplevel->toplevel_xdg;

	struct comp_xdg_decoration *deco = calloc(1, sizeof(*deco));
	if (deco == NULL) {
		return;
	}

	deco->toplevel = toplevel_xdg;
	deco->toplevel->xdg_decoration = deco;
	deco->wlr_xdg_decoration = wlr_deco;

	wl_signal_add(&wlr_deco->events.destroy, &deco->destroy);
	deco->destroy.notify = xdg_decoration_handle_destroy;

	wl_signal_add(&wlr_deco->events.request_mode, &deco->request_mode);
	deco->request_mode.notify = xdg_decoration_handle_request_mode;

	wl_list_insert(&server.xdg_decorations, &deco->link);

	set_xdg_decoration_mode(deco);
}
