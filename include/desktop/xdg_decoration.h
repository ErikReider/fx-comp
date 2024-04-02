#ifndef FX_DESKTOP_XDG_DECORATION_H
#define FX_DESKTOP_XDG_DECORATION_H

#include <wlr/types/wlr_xdg_decoration_v1.h>


struct comp_xdg_decoration {
	struct wlr_xdg_toplevel_decoration_v1 *wlr_xdg_decoration;
	struct wl_list link;

	struct comp_xdg_toplevel *toplevel;

	struct wl_listener destroy;
	struct wl_listener request_mode;
};

void handle_xdg_decoration(struct wl_listener *listener, void *data);

#endif // !FX_DESKTOP_XDG_DECORATION_H
