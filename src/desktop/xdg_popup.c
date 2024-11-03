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
#include "comp/workspace.h"
#include "desktop/xdg.h"
#include "scenefx/types/fx/corner_location.h"
#include "seat/cursor.h"
#include "util.h"

/*
 * XDG Popup
 */

static struct comp_toplevel *get_root_toplevel(struct comp_xdg_popup *popup) {
	struct comp_object *parent_object = popup->parent_object;
	switch (parent_object->type) {
	case COMP_OBJECT_TYPE_OUTPUT:
	case COMP_OBJECT_TYPE_WORKSPACE:
	case COMP_OBJECT_TYPE_UNMANAGED:
	case COMP_OBJECT_TYPE_LAYER_SURFACE:
	case COMP_OBJECT_TYPE_WIDGET:
	case COMP_OBJECT_TYPE_LOCK_OUTPUT:
	case COMP_OBJECT_TYPE_DND_ICON:
		break;
	case COMP_OBJECT_TYPE_XDG_POPUP:
		return get_root_toplevel(parent_object->data);
	case COMP_OBJECT_TYPE_TOPLEVEL:
		return parent_object->data;
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
			xdg_surface->role != WLR_XDG_SURFACE_ROLE_POPUP) {
			return;
		}
		struct comp_xdg_popup *popup = user_data;
		// TODO: Apply shadows to popups?
		wlr_scene_buffer_set_corner_radius(buffer, popup->corner_radius,
										   CORNER_LOCATION_ALL);
		wlr_scene_buffer_set_opacity(buffer, popup->opacity);
		break;
	case COMP_TOPLEVEL_TYPE_XWAYLAND:
		abort();
		break;
	}
}

/** Set the effects for each scene_buffer */
static void xdg_popup_apply_effects(struct wlr_scene_tree *tree,
									struct comp_xdg_popup *popup) {
	wlr_scene_node_for_each_buffer(&tree->node,
								   iter_scene_buffers_apply_effects, popup);
}

static void xdg_popup_map(struct wl_listener *listener, void *data) {
	struct comp_xdg_popup *popup = wl_container_of(listener, popup, map);

	xdg_popup_apply_effects(popup->xdg_scene_tree, popup);
}

static void xdg_popup_destroy(struct wl_listener *listener, void *data) {
	struct comp_xdg_popup *popup = wl_container_of(listener, popup, destroy);

	listener_remove(&popup->map);
	listener_remove(&popup->destroy);
	listener_remove(&popup->new_popup);
	listener_remove(&popup->commit);
	listener_remove(&popup->reposition);

	free(popup);
}

static void xdg_popup_new_popup(struct wl_listener *listener, void *data) {
	struct comp_xdg_popup *popup = wl_container_of(listener, popup, new_popup);
	struct wlr_xdg_popup *wlr_popup = data;
	xdg_new_xdg_popup(wlr_popup, popup->parent_object->data,
					  popup->xdg_scene_tree);
}

static void popup_unconstrain(struct comp_xdg_popup *popup) {
	struct comp_toplevel *toplevel = get_root_toplevel(popup);
	struct wlr_xdg_popup *wlr_popup = popup->wlr_popup;

	if (!toplevel || !toplevel->workspace || !toplevel->workspace->output) {
		return;
	}
	struct comp_workspace *workspace = toplevel->workspace;

	// the output box expressed in the coordinate system of the toplevel parent
	// of the popup
	struct wlr_box output_box;
	wlr_output_layout_get_box(server.output_layout,
							  workspace->output->wlr_output, &output_box);
	output_box.x = -toplevel->state.x + toplevel->geometry.x;
	output_box.y = -toplevel->state.y + toplevel->geometry.y;

	wlr_xdg_popup_unconstrain_from_box(wlr_popup, &output_box);
}

static void xdg_popup_commit(struct wl_listener *listener, void *data) {
	struct comp_xdg_popup *popup = wl_container_of(listener, popup, commit);
	if (popup->wlr_popup->base->initial_commit) {
		popup_unconstrain(popup);
	}
}

static void xdg_popup_reposition(struct wl_listener *listener, void *data) {
	struct comp_xdg_popup *popup = wl_container_of(listener, popup, reposition);
	popup_unconstrain(popup);
}

struct comp_xdg_popup *xdg_new_xdg_popup(struct wlr_xdg_popup *wlr_popup,
										 struct comp_object *object,
										 struct wlr_scene_tree *parent) {
	struct comp_xdg_popup *popup = calloc(1, sizeof(*popup));
	if (popup == NULL) {
		goto error_alloc;
	}

	popup->parent_object = object;
	popup->wlr_popup = wlr_popup;
	popup->object.scene_tree = alloc_tree(parent);
	popup->object.content_tree = alloc_tree(popup->object.scene_tree);
	if (popup->object.scene_tree == NULL ||
		popup->object.content_tree == NULL) {
		goto error_scene;
	}
	popup->xdg_scene_tree = wlr_scene_xdg_surface_create(
		popup->object.content_tree, wlr_popup->base);
	if (popup->xdg_scene_tree == NULL) {
		goto error_xdg;
	}
	popup->xdg_scene_tree->node.data = &popup->object;
	popup->object.scene_tree->node.data = &popup->object;
	popup->object.type = COMP_OBJECT_TYPE_XDG_POPUP;
	popup->object.data = popup;
	popup->object.destroying = false;

	popup->wlr_popup->base->data = popup;

	/* Set the scene_nodes decoration data */
	popup->opacity = 1;
	popup->corner_radius = 0;
	popup->shadow_data = shadow_data_get_default();

	// Events

	listener_init(&popup->map);
	listener_connect(&wlr_popup->base->surface->events.map, &popup->map,
					 xdg_popup_map);

	listener_init(&popup->destroy);
	listener_connect(&wlr_popup->base->events.destroy, &popup->destroy,
					 xdg_popup_destroy);

	listener_init(&popup->new_popup);
	listener_connect(&wlr_popup->base->events.new_popup, &popup->new_popup,
					 xdg_popup_new_popup);

	listener_init(&popup->commit);
	listener_connect(&wlr_popup->base->surface->events.commit, &popup->commit,
					 xdg_popup_commit);

	listener_init(&popup->reposition);
	listener_connect(&wlr_popup->events.reposition, &popup->reposition,
					 xdg_popup_reposition);

	return popup;

error_xdg:
	wlr_scene_node_destroy(&popup->object.scene_tree->node);
error_scene:
	free(popup);
error_alloc:
	wlr_xdg_popup_destroy(wlr_popup);
	return NULL;
}
