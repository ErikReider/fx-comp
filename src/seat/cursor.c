#include <scenefx/types/wlr_scene.h>
#include <stdlib.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/edges.h>
#include <wlr/util/region.h>

#include "comp/widget.h"
#include "desktop/toplevel.h"
#include "seat/cursor.h"
#include "wlr/util/log.h"

void comp_cursor_reset_cursor_mode(struct comp_server *server) {
	/* Reset the cursor mode to passthrough. */
	server->cursor->cursor_mode = COMP_CURSOR_PASSTHROUGH;
	server->grabbed_toplevel = NULL;
}

static void process_cursor_move(struct comp_server *server, uint32_t time) {
	/* Move the grabbed toplevel to the new position. */
	struct comp_toplevel *toplevel = server->grabbed_toplevel;
	if (server->grabbed_toplevel) {
		wlr_scene_node_set_position(
			&toplevel->object.scene_tree->node,
			server->cursor->wlr_cursor->x - server->grab_x,
			server->cursor->wlr_cursor->y - server->grab_y);
	}
}

static void process_cursor_resize(struct comp_server *server, uint32_t time) {
	/*
	 * Resizing the grabbed toplevel can be a little bit complicated, because we
	 * could be resizing from any corner or edge. This not only resizes the
	 * toplevel on one or two axes, but can also move the toplevel if you resize
	 * from the top or left edges (or top-left corner).
	 *
	 * Note that some shortcuts are taken here. In a more fleshed-out
	 * compositor, you'd wait for the client to prepare a buffer at the new
	 * size, then commit any movement that was prepared.
	 */
	struct comp_toplevel *toplevel = server->grabbed_toplevel;
	double border_x = server->cursor->wlr_cursor->x - server->grab_x;
	double border_y = server->cursor->wlr_cursor->y - server->grab_y;
	int new_left = server->grab_geobox.x;
	int new_right = server->grab_geobox.x + server->grab_geobox.width;
	int new_top = server->grab_geobox.y;
	int new_bottom = server->grab_geobox.y + server->grab_geobox.height;

	if (server->resize_edges & WLR_EDGE_TOP) {
		new_top = border_y;
		if (new_top >= new_bottom) {
			new_top = new_bottom - 1;
		}
	} else if (server->resize_edges & WLR_EDGE_BOTTOM) {
		new_bottom = border_y;
		if (new_bottom <= new_top) {
			new_bottom = new_top + 1;
		}
	}
	if (server->resize_edges & WLR_EDGE_LEFT) {
		new_left = border_x;
		if (new_left >= new_right) {
			new_left = new_right - 1;
		}
	} else if (server->resize_edges & WLR_EDGE_RIGHT) {
		new_right = border_x;
		if (new_right <= new_left) {
			new_right = new_left + 1;
		}
	}

	struct wlr_box geo_box;
	wlr_xdg_surface_get_geometry(toplevel->xdg_toplevel->base, &geo_box);
	wlr_scene_node_set_position(&toplevel->object.scene_tree->node,
								new_left - geo_box.x, new_top - geo_box.y);

	int new_width = new_right - new_left;
	int new_height = new_bottom - new_top;
	wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, new_width, new_height);
}

static void process_cursor_motion(struct comp_cursor *cursor, uint32_t time) {
	struct comp_server *server = cursor->server;
	// If the mode is non-passthrough, delegate to those functions.
	if (server->cursor->cursor_mode == COMP_CURSOR_MOVE) {
		process_cursor_move(server, time);
		return;
	} else if (server->cursor->cursor_mode == COMP_CURSOR_RESIZE) {
		process_cursor_resize(server, time);
		return;
	}

	// Otherwise, find the toplevel under the pointer and send the event along.
	double sx, sy;
	struct wlr_seat *seat = server->seat;
	struct wlr_scene_buffer *scene_buffer = NULL;
	struct wlr_surface *surface = NULL;
	struct comp_object *object =
		comp_object_at(server, cursor->wlr_cursor->x, cursor->wlr_cursor->y,
					   &sx, &sy, &scene_buffer, &surface);
	if (!object) {
		/* If there's no toplevel under the cursor, set the cursor image to a
		 * default. This is what makes the cursor image appear when you move it
		 * around the screen, not over any toplevels. */
		wlr_cursor_set_xcursor(cursor->wlr_cursor, cursor->cursor_mgr,
							   "default");
		wlr_seat_pointer_notify_clear_focus(seat);

		if (server->hovered_widget) {
			comp_widget_pointer_leave(server->hovered_widget);
			server->hovered_widget = NULL;
		}
	} else {
		switch (object->type) {
		case COMP_OBJECT_TYPE_TOPLEVEL:
			if (surface) {
				/*
				 * Send pointer enter and motion events.
				 *
				 * The enter event gives the surface "pointer focus", which is
				 * distinct from keyboard focus. You get pointer focus by moving
				 * the pointer over a window.
				 *
				 * Note that wlroots will avoid sending duplicate enter/motion
				 * events if the surface has already has pointer focus or if the
				 * client is already aware of the coordinates passed.
				 */
				wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
				wlr_seat_pointer_notify_motion(seat, time, sx, sy);
			} else {
				wlr_seat_pointer_notify_clear_focus(seat);
			}
			if (server->hovered_widget) {
				comp_widget_pointer_leave(server->hovered_widget);
				server->hovered_widget = NULL;
			}
			break;
		case COMP_OBJECT_TYPE_WIDGET:;
			struct comp_widget *widget = object->data;

			if (server->hovered_widget != widget) {
				comp_widget_pointer_leave(server->hovered_widget);
				server->hovered_widget = widget;
				comp_widget_pointer_enter(server->hovered_widget);
			}

			/* Clear pointer focus so future button events and such are not sent
			 * to the last client to have the cursor over it. */
			comp_widget_pointer_motion(widget, sx, sy);
			wlr_seat_pointer_clear_focus(server->seat);
			if (!widget->sets_cursor) {
				wlr_cursor_set_xcursor(cursor->wlr_cursor, cursor->cursor_mgr,
									   "left_ptr");
			}
			break;
		}
	}
}

static void pointer_motion(struct comp_cursor *cursor, uint32_t time,
						   struct wlr_input_device *device, double dx,
						   double dy, double dx_unaccel, double dy_unaccel) {
	wlr_relative_pointer_manager_v1_send_relative_motion(
		cursor->server->relative_pointer_manager, cursor->server->seat,
		(uint64_t)time * 1000, dx, dy, dx_unaccel, dy_unaccel);

	// Only apply pointer constraints to real pointer input.
	if (cursor->active_constraint && device->type == WLR_INPUT_DEVICE_POINTER) {
		struct wlr_scene_buffer *scene_buffer = NULL;
		struct wlr_surface *surface = NULL;
		double sx, sy;
		comp_object_at(cursor->server, cursor->wlr_cursor->x,
					   cursor->wlr_cursor->y, &sx, &sy, &scene_buffer,
					   &surface);

		if (cursor->active_constraint->surface != surface) {
			return;
		}

		double sx_confined, sy_confined;
		if (!wlr_region_confine(&cursor->confine, sx, sy, sx + dx, sy + dy,
								&sx_confined, &sy_confined)) {
			return;
		}

		dx = sx_confined - sx;
		dy = sy_confined - sy;
	}

	wlr_cursor_move(cursor->wlr_cursor, device, dx, dy);

	process_cursor_motion(cursor, time);
}

static void comp_server_cursor_motion(struct wl_listener *listener,
									  void *data) {
	/* This event is forwarded by the cursor when a pointer emits a _relative_
	 * pointer motion event (i.e. a delta) */
	struct comp_cursor *cursor =
		wl_container_of(listener, cursor, cursor_motion);
	struct wlr_pointer_motion_event *event = data;
	/* The cursor doesn't move unless we tell it to. The cursor automatically
	 * handles constraining the motion to the output layout, as well as any
	 * special configuration applied for the specific input device which
	 * generated the event. You can pass NULL for the device if you want to move
	 * the cursor around without any input. */
	// wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x,
	// 				event->delta_y);
	// process_cursor_motion(server, event->time_msec);
	pointer_motion(cursor, event->time_msec, &event->pointer->base,
				   event->delta_x, event->delta_y, event->unaccel_dx,
				   event->unaccel_dy);
}

static void comp_server_cursor_motion_absolute(struct wl_listener *listener,
											   void *data) {
	/* This event is forwarded by the cursor when a pointer emits an _absolute_
	 * motion event, from 0..1 on each axis. This happens, for example, when
	 * wlroots is running under a Wayland window rather than KMS+DRM, and you
	 * move the mouse over the window. You could enter the window from any edge,
	 * so we have to warp the mouse there. There is also some hardware which
	 * emits these events. */
	struct comp_cursor *cursor =
		wl_container_of(listener, cursor, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	// wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x,
	// 						 event->y);

	double lx, ly;
	wlr_cursor_absolute_to_layout_coords(cursor->wlr_cursor,
										 &event->pointer->base, event->x,
										 event->y, &lx, &ly);

	double dx = lx - cursor->wlr_cursor->x;
	double dy = ly - cursor->wlr_cursor->y;

	// process_cursor_motion(server, event->time_msec);
	pointer_motion(cursor, event->time_msec, &event->pointer->base, dx, dy, dx,
				   dy);
}

static void comp_server_cursor_button(struct wl_listener *listener,
									  void *data) {
	/* This event is forwarded by the cursor when a pointer emits a button
	 * event. */
	struct comp_cursor *cursor =
		wl_container_of(listener, cursor, cursor_button);
	struct wlr_pointer_button_event *event = data;
	struct comp_server *server = cursor->server;

	double sx, sy;
	struct wlr_scene_buffer *scene_buffer = NULL;
	struct wlr_surface *surface = NULL;
	struct comp_object *object =
		comp_object_at(server, cursor->wlr_cursor->x, cursor->wlr_cursor->y,
					   &sx, &sy, &scene_buffer, &surface);
	if (event->state == WLR_BUTTON_RELEASED) {
		/* If you released any buttons, we exit interactive move/resize mode. */
		comp_cursor_reset_cursor_mode(server);
	} else if (object) {
		switch (object->type) {
		case COMP_OBJECT_TYPE_TOPLEVEL:
			if (surface) {
				/* Focus that client if the button was _pressed_ */
				comp_toplevel_focus(object->data, surface);
			}
			break;
		case COMP_OBJECT_TYPE_WIDGET:
			comp_widget_pointer_button(object->data, sx, sy, event);
			break;
		}
	}

	/* Notify the client with pointer focus that a button press has occurred */
	wlr_seat_pointer_notify_button(server->seat, event->time_msec,
								   event->button, event->state);
}

static void comp_server_cursor_axis(struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits an axis event,
	 * for example when you move the scroll wheel. */
	struct comp_cursor *cursor = wl_container_of(listener, cursor, cursor_axis);
	struct comp_server *server = cursor->server;
	struct wlr_pointer_axis_event *event = data;
	/* Notify the client with pointer focus of the axis event. */
	wlr_seat_pointer_notify_axis(server->seat, event->time_msec,
								 event->orientation, event->delta,
								 event->delta_discrete, event->source);
}

static void comp_server_cursor_frame(struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits an frame
	 * event. Frame events are sent after regular pointer events to group
	 * multiple events together. For instance, two axis events may happen at the
	 * same time, in which case a frame event won't be sent in between. */
	struct comp_cursor *cursor =
		wl_container_of(listener, cursor, cursor_frame);
	/* Notify the client with pointer focus of the frame event. */
	wlr_seat_pointer_notify_frame(cursor->server->seat);
}

void comp_cursor_destroy(struct comp_cursor *cursor) {
	if (!cursor) {
		return;
	}

	wl_list_remove(&cursor->cursor_motion.link);
	wl_list_remove(&cursor->cursor_motion_absolute.link);
	wl_list_remove(&cursor->cursor_button.link);
	wl_list_remove(&cursor->cursor_axis.link);
	wl_list_remove(&cursor->cursor_frame.link);

	wlr_xcursor_manager_destroy(cursor->cursor_mgr);
	wlr_cursor_destroy(cursor->wlr_cursor);
	free(cursor);
}

struct comp_cursor *comp_cursor_create(struct comp_server *server) {
	struct comp_cursor *cursor = calloc(1, sizeof(struct comp_cursor));
	if (cursor == NULL) {
		wlr_log(WLR_ERROR, "Could not allocate comp_cursor");
		return NULL;
	}

	struct wlr_cursor *wlr_cursor = wlr_cursor_create();
	if (wlr_cursor == NULL) {
		wlr_log(WLR_ERROR, "Could not allocate wlr_cursor");
		free(cursor);
		return NULL;
	}

	cursor->previous.x = wlr_cursor->x;
	cursor->previous.y = wlr_cursor->y;

	cursor->server = server;
	cursor->cursor_mode = COMP_CURSOR_PASSTHROUGH;

	wlr_cursor_attach_output_layout(wlr_cursor, server->output_layout);

	// cursor->hide_source =
	// 	wl_event_loop_add_timer(server.wl_event_loop, hide_notify, cursor);

	// wl_list_init(&cursor->image_surface_destroy.link);
	// cursor->image_surface_destroy.notify = handle_image_surface_destroy;

	/* Creates an xcursor manager, another wlroots utility which loads up
	 * Xcursor themes to source cursor images from and makes sure that cursor
	 * images are available at all scale factors on the screen (necessary for
	 * HiDPI support). */
	cursor->cursor_mgr = wlr_xcursor_manager_create(NULL, 24);

	/*
	 * wlr_cursor *only* displays an image on screen. It does not move around
	 * when the pointer moves. However, we can attach input devices to it, and
	 * it will generate aggregate events for all of them. In these events, we
	 * can choose how we want to process them, forwarding them to clients and
	 * moving the cursor around. More detail on this process is described in
	 * https://drewdevault.com/2018/07/17/Input-handling-in-wlroots.html.
	 *
	 * And more comments are sprinkled throughout the notify functions above.
	 */
	wl_signal_add(&wlr_cursor->events.motion, &cursor->cursor_motion);
	cursor->cursor_motion.notify = comp_server_cursor_motion;

	wl_signal_add(&wlr_cursor->events.motion_absolute,
				  &cursor->cursor_motion_absolute);
	cursor->cursor_motion_absolute.notify = comp_server_cursor_motion_absolute;

	wl_signal_add(&wlr_cursor->events.button, &cursor->cursor_button);
	cursor->cursor_button.notify = comp_server_cursor_button;

	wl_signal_add(&wlr_cursor->events.axis, &cursor->cursor_axis);
	cursor->cursor_axis.notify = comp_server_cursor_axis;

	wl_signal_add(&wlr_cursor->events.frame, &cursor->cursor_frame);
	cursor->cursor_frame.notify = comp_server_cursor_frame;

	// wl_signal_add(&wlr_cursor->events.touch_down, &cursor->touch_down);
	// cursor->touch_down.notify = handle_touch_down;
	//
	// wl_signal_add(&wlr_cursor->events.touch_up, &cursor->touch_up);
	// cursor->touch_up.notify = handle_touch_up;
	//
	// wl_signal_add(&wlr_cursor->events.touch_cancel, &cursor->touch_cancel);
	// cursor->touch_cancel.notify = handle_touch_cancel;
	//
	// wl_signal_add(&wlr_cursor->events.touch_motion, &cursor->touch_motion);
	// cursor->touch_motion.notify = handle_touch_motion;
	//
	// wl_signal_add(&wlr_cursor->events.touch_frame, &cursor->touch_frame);
	// cursor->touch_frame.notify = handle_touch_frame;
	//
	// wl_signal_add(&wlr_cursor->events.tablet_tool_axis, &cursor->tool_axis);
	// cursor->tool_axis.notify = handle_tool_axis;
	//
	// wl_signal_add(&wlr_cursor->events.tablet_tool_tip, &cursor->tool_tip);
	// cursor->tool_tip.notify = handle_tool_tip;
	//
	// wl_signal_add(&wlr_cursor->events.tablet_tool_proximity,
	// 			  &cursor->tool_proximity);
	// cursor->tool_proximity.notify = handle_tool_proximity;

	// wl_signal_add(&wlr_cursor->events.tablet_tool_button,
	// &cursor->tool_button); cursor->tool_button.notify = handle_tool_button;

	wl_list_init(&cursor->constraint_commit.link);
	wl_list_init(&cursor->tablets);
	wl_list_init(&cursor->tablet_pads);

	cursor->wlr_cursor = wlr_cursor;

	wlr_cursor_set_xcursor(cursor->wlr_cursor, cursor->cursor_mgr, "left_ptr");

	return cursor;
}
