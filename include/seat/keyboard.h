#ifndef FX_SEAT_KEYBOARD_H
#define FX_SEAT_KEYBOARD_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_input_device.h>

struct comp_keyboard {
	struct wl_list link;
	struct comp_server *server;
	struct comp_seat *seat;
	struct wlr_keyboard *wlr_keyboard;

	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

void comp_keyboard_create(struct comp_seat *seat,
						  struct wlr_input_device *device);

#endif // !FX_SEAT_KEYBOARD_H
