#ifndef FX_COMP_XWAYLAND_H
#define FX_COMP_XWAYLAND_H

#include <wayland-server-core.h>
#include <wlr/xwayland.h>
#include <xcb/xproto.h>

enum atom_name {
	NET_WM_WINDOW_TYPE_NORMAL,
	NET_WM_WINDOW_TYPE_DIALOG,
	NET_WM_WINDOW_TYPE_UTILITY,
	NET_WM_WINDOW_TYPE_TOOLBAR,
	NET_WM_WINDOW_TYPE_SPLASH,
	NET_WM_WINDOW_TYPE_MENU,
	NET_WM_WINDOW_TYPE_DROPDOWN_MENU,
	NET_WM_WINDOW_TYPE_POPUP_MENU,
	NET_WM_WINDOW_TYPE_TOOLTIP,
	NET_WM_WINDOW_TYPE_NOTIFICATION,
	NET_WM_STATE_MODAL,
	ATOM_LAST,
};

struct comp_xwayland_mgr {
	struct wlr_xwayland *wlr_xwayland;
	struct wlr_xcursor_manager *xcursor_manager;

	xcb_atom_t atoms[ATOM_LAST];
};

void xwayland_ready_cb(struct wl_listener *listener, void *data);

void xwayland_new_surface(struct wl_listener *listener, void *data);

#endif // !FX_COMP_XWAYLAND_H
