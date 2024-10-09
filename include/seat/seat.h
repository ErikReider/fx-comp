#ifndef FX_SEAT_H
#define FX_SEAT_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_compositor.h>

#include "desktop/layer_shell.h"
#include "desktop/toplevel.h"

struct gesture_data {
	float percent;
};

struct comp_drag {
	struct comp_object object;

	struct wlr_scene_tree *tree;

	struct comp_seat *seat;
	struct wlr_drag *wlr_drag;
	struct wl_listener destroy;
};

struct comp_seat {
	struct comp_server *server;

	struct comp_toplevel *focused_toplevel;
	struct comp_layer_surface *focused_layer_surface;

	bool exclusive_layer;

	struct wlr_seat *wlr_seat;

	// Keyboard
	struct wl_listener new_input;
	struct wl_listener request_cursor;
	struct wl_listener request_start_drag;
	struct wl_listener start_drag;
	struct wl_listener request_set_selection;
	struct wl_list keyboards;

	// Cursor
	struct comp_cursor *cursor;

	struct wl_list focus_order;

	struct comp_widget *hovered_widget;
	struct comp_toplevel *grabbed_toplevel;
	double grab_x, grab_y;
	struct wlr_box grab_geobox;
	uint32_t resize_edges;
};

struct comp_seat *comp_seat_create(struct comp_server *server);

bool comp_seat_object_is_focus(struct comp_seat *seat,
							   struct comp_object *object);

void comp_seat_unfocus_unless_client(struct wl_client *client);

void comp_seat_surface_unfocus(struct wlr_surface *surface,
							   bool focus_previous);

void comp_seat_surface_focus(struct comp_object *object,
							 struct wlr_surface *surface);

void comp_seat_update_dnd_positions(void);

#endif // !FX_SEAT_H
