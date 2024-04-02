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
#include "desktop/xdg.h"
#include "seat/cursor.h"
#include "util.h"

/*
 * XDG Popup
 */

static void xdg_popup_map(struct wl_listener *listener, void *data) {
	struct comp_xdg_popup *popup = wl_container_of(listener, popup, map);

	comp_toplevel_apply_effects(popup->xdg_scene_tree, popup);
}

static void xdg_popup_destroy(struct wl_listener *listener, void *data) {
	struct comp_xdg_popup *popup = wl_container_of(listener, popup, destroy);

	wl_list_remove(&popup->map.link);
	wl_list_remove(&popup->destroy.link);
	wl_list_remove(&popup->new_popup.link);

	free(popup);
}

static void xdg_popup_new_popup(struct wl_listener *listener, void *data) {
	struct comp_xdg_popup *popup = wl_container_of(listener, popup, new_popup);
	struct wlr_xdg_popup *wlr_popup = data;
	xdg_new_xdg_popup(wlr_popup, popup->parent_object->data,
					  popup->xdg_scene_tree);
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
	if (popup->object.scene_tree == NULL) {
		goto error_scene;
	}
	popup->xdg_scene_tree =
		wlr_scene_xdg_surface_create(popup->object.scene_tree, wlr_popup->base);
	if (popup->xdg_scene_tree == NULL) {
		goto error_xdg;
	}
	popup->xdg_scene_tree->node.data = &popup->object;
	popup->object.scene_tree->node.data = &popup->object;
	popup->object.type = COMP_OBJECT_TYPE_XDG_POPUP;
	popup->object.data = popup;

	popup->wlr_popup->base->data = popup;

	/* Set the scene_nodes decoration data */
	popup->opacity = 1;
	popup->corner_radius = 0;
	popup->shadow_data = shadow_data_get_default();
	popup->shadow_data.enabled = false;

	popup->map.notify = xdg_popup_map;
	wl_signal_add(&wlr_popup->base->surface->events.map, &popup->map);

	popup->destroy.notify = xdg_popup_destroy;
	wl_signal_add(&wlr_popup->base->events.destroy, &popup->destroy);

	popup->new_popup.notify = xdg_popup_new_popup;
	wl_signal_add(&wlr_popup->base->events.new_popup, &popup->new_popup);

	return popup;

error_xdg:
	wlr_scene_node_destroy(&popup->object.scene_tree->node);
error_scene:
	free(popup);
error_alloc:
	wlr_xdg_popup_destroy(wlr_popup);
	return NULL;
}
