#include <stdio.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/util/log.h>

#include "comp/server.h"
#include "comp/xwayland_mgr.h"
#include "desktop/xwayland.h"

// Credits goes to Sway for most of the implementation :D

static const char *atom_map[ATOM_LAST] = {
	[NET_WM_WINDOW_TYPE_NORMAL] = "_NET_WM_WINDOW_TYPE_NORMAL",
	[NET_WM_WINDOW_TYPE_DIALOG] = "_NET_WM_WINDOW_TYPE_DIALOG",
	[NET_WM_WINDOW_TYPE_UTILITY] = "_NET_WM_WINDOW_TYPE_UTILITY",
	[NET_WM_WINDOW_TYPE_TOOLBAR] = "_NET_WM_WINDOW_TYPE_TOOLBAR",
	[NET_WM_WINDOW_TYPE_SPLASH] = "_NET_WM_WINDOW_TYPE_SPLASH",
	[NET_WM_WINDOW_TYPE_MENU] = "_NET_WM_WINDOW_TYPE_MENU",
	[NET_WM_WINDOW_TYPE_DROPDOWN_MENU] = "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU",
	[NET_WM_WINDOW_TYPE_POPUP_MENU] = "_NET_WM_WINDOW_TYPE_POPUP_MENU",
	[NET_WM_WINDOW_TYPE_TOOLTIP] = "_NET_WM_WINDOW_TYPE_TOOLTIP",
	[NET_WM_WINDOW_TYPE_NOTIFICATION] = "_NET_WM_WINDOW_TYPE_NOTIFICATION",
	[NET_WM_STATE_MODAL] = "_NET_WM_STATE_MODAL",
};

void xwayland_ready_cb(struct wl_listener *listener, void *data) {
	struct comp_server *server =
		wl_container_of(listener, server, xwayland_ready);
	struct comp_xwayland_mgr *xwayland = &server->xwayland_mgr;

	xcb_connection_t *xcb_conn = xcb_connect(NULL, NULL);
	int err = xcb_connection_has_error(xcb_conn);
	if (err) {
		wlr_log(WLR_ERROR, "XCB connect failed: %d", err);
		return;
	}

	xcb_intern_atom_cookie_t cookies[ATOM_LAST];
	for (size_t i = 0; i < ATOM_LAST; i++) {
		cookies[i] =
			xcb_intern_atom(xcb_conn, 0, strlen(atom_map[i]), atom_map[i]);
	}
	for (size_t i = 0; i < ATOM_LAST; i++) {
		xcb_generic_error_t *error = NULL;
		xcb_intern_atom_reply_t *reply =
			xcb_intern_atom_reply(xcb_conn, cookies[i], &error);
		if (reply != NULL && error == NULL) {
			xwayland->atoms[i] = reply->atom;
		}
		free(reply);

		if (error != NULL) {
			wlr_log(WLR_ERROR, "could not resolve atom %s, X11 error code %d",
					atom_map[i], error->error_code);
			free(error);
			break;
		}
	}

	xcb_disconnect(xcb_conn);
}

void xwayland_new_surface(struct wl_listener *listener, void *data) {
	struct wlr_xwayland_surface *xsurface = data;

	if (xsurface->override_redirect) {
		wlr_log(WLR_DEBUG, "New xwayland unmanaged surface");
		xway_create_unmanaged(xsurface);
		return;
	}

	xway_create_toplevel(xsurface);
}
