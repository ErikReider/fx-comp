#ifndef FX_COMP_DESKTOP_XDG_TOPLEVEL_H
#define FX_COMP_DESKTOP_XDG_TOPLEVEL_H

#include <wayland-server-core.h>

#include "comp/object.h"

/*
 * XDG Toplevel
 */

void xdg_new_xdg_surface(struct wl_listener *listener, void *data);

void xdg_new_decoration(struct wl_listener *listener, void *data);

void xdg_iter_scene_buffers_apply_effects(struct wlr_scene_buffer *buffer,
										  int sx, int sy, void *user_data);

/*
 * XDG Popup
 */

struct comp_xdg_popup {
	struct comp_server *server;
	struct comp_object object;
	struct comp_object *parent_object;

	struct wlr_scene_tree *xdg_scene_tree;
	struct wlr_xdg_popup *wlr_popup;

	struct wl_listener map;
	struct wl_listener destroy;
	struct wl_listener new_popup;

	// Effects
	float opacity;
	int corner_radius;
	struct shadow_data shadow_data;
};

struct comp_xdg_popup *xdg_new_xdg_popup(struct wlr_xdg_popup *wlr_popup,
										 struct comp_object *object,
										 struct wlr_scene_tree *parent);

#endif // !FX_COMP_DESKTOP_XDG_TOPLEVEL_H
