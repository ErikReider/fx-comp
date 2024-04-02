#ifndef FX_COMP_DESKTOP_XDG_TOPLEVEL_H
#define FX_COMP_DESKTOP_XDG_TOPLEVEL_H

#include <wayland-server-core.h>

#include "comp/object.h"
#include "desktop/toplevel.h"
#include "desktop/xdg_decoration.h"

/*
 * XDG Toplevel
 */

struct comp_xdg_toplevel {
	struct comp_toplevel *toplevel;

	struct wlr_xdg_toplevel *xdg_toplevel;
	struct comp_xdg_decoration *xdg_decoration;

	// Signals
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;

	struct wl_listener new_popup;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	struct wl_listener set_title;
};

void xdg_new_xdg_surface(struct wl_listener *listener, void *data);

void xdg_new_decoration(struct wl_listener *listener, void *data);

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
