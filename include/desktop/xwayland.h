#ifndef FX_COMP_DESKTOP_XWAYLAND_H
#define FX_COMP_DESKTOP_XWAYLAND_H

#include <wayland-server-core.h>
#include <wlr/xwayland.h>

#include "desktop/toplevel.h"

/*
 * XWayland
 */

struct wlr_scene_tree *get_parent_tree(struct wlr_xwayland_surface *xsurface);

void move_into_parent_tree(struct comp_toplevel *toplevel,
						   struct wlr_scene_tree *parent);

/*
 * XWayland Toplevel
 */

struct comp_xwayland_toplevel {
	struct comp_toplevel *toplevel;
	struct wlr_scene_tree *parent_tree;

	struct wlr_xwayland_surface *xwayland_surface;

	// Signals
	struct wl_listener surface_tree_destroy;
	struct wl_listener commit;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_minimize;
	struct wl_listener request_configure;
	struct wl_listener request_fullscreen;
	struct wl_listener request_activate;
	struct wl_listener set_title;
	struct wl_listener set_startup_id;
	struct wl_listener set_hints;
	struct wl_listener set_decorations;
	struct wl_listener associate;
	struct wl_listener dissociate;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener override_redirect;
};

struct comp_xwayland_toplevel *
xway_create_toplevel(struct wlr_xwayland_surface *xsurface);

/*
 * XWayland Unmanaged
 */

struct comp_xwayland_unmanaged {
	struct comp_object object;

	struct wlr_scene_tree *parent_tree;

	struct wlr_scene_surface *surface_scene;

	struct wlr_xwayland_surface *xwayland_surface;

	// Signals
	struct wl_listener request_activate;
	struct wl_listener request_configure;
	struct wl_listener request_fullscreen;
	struct wl_listener set_geometry;
	struct wl_listener associate;
	struct wl_listener dissociate;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener override_redirect;
};

struct comp_xwayland_unmanaged *
xway_create_unmanaged(struct wlr_xwayland_surface *xsurface);

#endif // !FX_COMP_DESKTOP_XWAYLAND_H
