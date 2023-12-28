#ifndef FX_COMP_INPUT_H
#define FX_COMP_INPUT_H

#include <wayland-server-core.h>
#include <wayland-util.h>

struct comp_keyboard {
	struct wl_list link;
	struct comp_server *server;
	struct wlr_keyboard *wlr_keyboard;

	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

void comp_seat_new_input(struct wl_listener *listener, void *data);

void comp_seat_request_cursor(struct wl_listener *listener, void *data);

void comp_seat_request_set_selection(struct wl_listener *listener, void *data);

#endif // !FX_COMP_INPUT_H
