#include <pixman.h>
#include <stdbool.h>
#include <wayland-server-core.h>
#include <wayland-util.h>

#include "comp/server.h"

#ifndef FX_COMP_SEAT_CURSOR_H
#define FX_COMP_SEAT_CURSOR_H

struct comp_cursor {
	struct comp_server *server;
	struct comp_seat *seat;
	struct wlr_cursor *wlr_cursor;
	struct {
		double x, y;
	} previous;
	struct wlr_xcursor_manager *cursor_mgr;
	struct wl_list tablets;
	struct wl_list tablet_pads;

	enum comp_cursor_mode cursor_mode;

	const char *image;
	struct wl_client *image_client;
	struct wlr_surface *image_surface;
	int hotspot_x, hotspot_y;

	struct wlr_pointer_constraint_v1 *active_constraint;
	pixman_region32_t confine; // invalid if active_constraint == NULL
	bool active_confine_requires_warp;

	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;

	struct wl_listener image_surface_destroy;

	struct wl_listener constraint_commit;

	struct wl_event_source *hide_source;
	bool hidden;

	size_t pressed_button_count;
};

void comp_cursor_reset_cursor_mode(struct comp_seat *seat);

void comp_cursor_destroy(struct comp_cursor *cursor);

struct comp_cursor *comp_cursor_create(struct comp_seat *seat);

#endif // !FX_COMP_SEAT_CURSOR_H
