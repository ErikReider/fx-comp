#ifndef FX_COMP_VIEW_H
#define FX_COMP_VIEW_H

#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_compositor.h>

struct comp_view {
	struct wl_list link;
	struct comp_server *server;
	struct wlr_xdg_toplevel *xdg_toplevel;
	struct wlr_scene_tree *scene_tree;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	int x, y;
};

struct comp_view *comp_view_at(struct comp_server *server, double lx, double ly,
							   struct wlr_surface **surface, double *sx,
							   double *sy);

void comp_view_focus_view(struct comp_view *view, struct wlr_surface *surface);

#endif // !FX_COMP_VIEW_H
