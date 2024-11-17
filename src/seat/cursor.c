#include <assert.h>
#include <libevdev/libevdev.h>
#include <scenefx/types/wlr_scene.h>
#include <stdlib.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>

#include "comp/object.h"
#include "comp/output.h"
#include "comp/server.h"
#include "comp/widget.h"
#include "constants.h"
#include "desktop/toplevel.h"
#include "seat/cursor.h"
#include "seat/seat.h"

static void set_active_output_from_cursor_pos(struct comp_cursor *cursor) {
	// Use cursor position by default
	int coords_x = cursor->wlr_cursor->x;
	int coords_y = cursor->wlr_cursor->y;
	struct wlr_output *hovered_output =
		wlr_output_layout_output_at(server.output_layout, coords_x, coords_y);
	if (hovered_output) {
		struct comp_output *output = hovered_output->data;
		server.active_output = output;
		wlr_scene_node_raise_to_top(&output->object.scene_tree->node);
	}
}

void comp_cursor_reset_cursor_mode(struct comp_seat *seat) {
	if (seat->cursor->cursor_mode == COMP_CURSOR_RESIZE) {
		comp_toplevel_set_resizing(seat->grabbed_toplevel, false);
	}

	/* Reset the cursor mode to passthrough. */
	seat->cursor->cursor_mode = COMP_CURSOR_PASSTHROUGH;
	seat->grabbed_toplevel = NULL;

	// Set the active output
	set_active_output_from_cursor_pos(seat->cursor);
}

static void process_cursor_motion(struct comp_cursor *cursor, uint32_t time) {
	struct comp_server *server = cursor->server;
	// If the mode is non-passthrough, delegate to those functions.
	if (cursor->cursor_mode == COMP_CURSOR_MOVE) {
		comp_toplevel_process_cursor_move(server, time);
		return;
	} else if (cursor->cursor_mode == COMP_CURSOR_RESIZE) {
		comp_toplevel_process_cursor_resize(server, time);
		return;
	}

	// Set the active output
	set_active_output_from_cursor_pos(cursor);

	comp_seat_update_dnd_positions();

	// Otherwise, find the toplevel under the pointer and send the event along.
	double sx, sy;
	struct wlr_seat *seat = server->seat->wlr_seat;
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

		if (server->seat->hovered_widget) {
			comp_widget_pointer_leave(server->seat->hovered_widget);
			server->seat->hovered_widget = NULL;
		}
	} else {
		switch (object->type) {
		case COMP_OBJECT_TYPE_LOCK_OUTPUT:
		case COMP_OBJECT_TYPE_DND_ICON:
			break;
		case COMP_OBJECT_TYPE_TOPLEVEL:
		case COMP_OBJECT_TYPE_XDG_POPUP:
		case COMP_OBJECT_TYPE_LAYER_SURFACE:
		case COMP_OBJECT_TYPE_UNMANAGED:
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

				// Don't refocus a different surface if the cursor is
				// constrained to a surface
				bool is_constrained_surface =
					!(cursor->active_constraint &&
					  cursor->active_constraint->surface != surface);

				if (!server->comp_session_lock.locked &&
					is_constrained_surface) {
					// Only notify enter when there aren't any pointer buttons
					// currently pressed. Fixes middle section of
					// weston-constraints "losing focus" when pressed.
					if (seat->pointer_state.button_count == 0) {
						wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
					}
					wlr_seat_pointer_notify_motion(seat, time, sx, sy);
				}
			} else {
				wlr_seat_pointer_notify_clear_focus(seat);
			}
			if (server->seat->hovered_widget) {
				comp_widget_pointer_leave(server->seat->hovered_widget);
				server->seat->hovered_widget = NULL;
			}
			break;
		case COMP_OBJECT_TYPE_WIDGET:;
			struct comp_widget *widget = object->data;

			if (server->seat->hovered_widget != widget) {
				comp_widget_pointer_leave(server->seat->hovered_widget);
				server->seat->hovered_widget = widget;
				comp_widget_pointer_enter(server->seat->hovered_widget);
			}

			/* Clear pointer focus so future button events and such are not sent
			 * to the last client to have the cursor over it. */
			comp_widget_pointer_motion(widget, sx, sy);
			wlr_seat_pointer_clear_focus(server->seat->wlr_seat);
			if (!widget->sets_cursor) {
				wlr_cursor_set_xcursor(cursor->wlr_cursor, cursor->cursor_mgr,
									   "left_ptr");
			}
			break;
		case COMP_OBJECT_TYPE_OUTPUT:
		case COMP_OBJECT_TYPE_WORKSPACE:
			break;
		}
	}
}

static void pointer_motion(struct comp_cursor *cursor, uint32_t time,
						   struct wlr_input_device *device, double dx,
						   double dy, double dx_unaccel, double dy_unaccel) {
	wlr_relative_pointer_manager_v1_send_relative_motion(
		cursor->server->relative_pointer_manager,
		cursor->server->seat->wlr_seat, (uint64_t)time * 1000, dx, dy,
		dx_unaccel, dy_unaccel);

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

	cursor->previous.x = cursor->wlr_cursor->x;
	cursor->previous.y = cursor->wlr_cursor->y;

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

	pointer_motion(cursor, event->time_msec, &event->pointer->base, dx, dy, dx,
				   dy);
}

static bool try_resize_or_move_toplevel(struct comp_object *object,
										struct wlr_pointer_button_event *event,
										struct comp_cursor *cursor) {
	if (object == NULL || server.comp_session_lock.locked) {
		return false;
	}

	struct comp_toplevel *toplevel = NULL;
	// Get Toplevel
	switch (object->type) {
	case COMP_OBJECT_TYPE_TOPLEVEL:
		toplevel = object->data;
		break;
	case COMP_OBJECT_TYPE_WIDGET:;
		struct comp_widget *widget = object->data;
		return try_resize_or_move_toplevel(widget->parent_object, event,
										   cursor);
	case COMP_OBJECT_TYPE_XDG_POPUP:
	case COMP_OBJECT_TYPE_LAYER_SURFACE:
	case COMP_OBJECT_TYPE_OUTPUT:
	case COMP_OBJECT_TYPE_WORKSPACE:
	case COMP_OBJECT_TYPE_UNMANAGED:
	case COMP_OBJECT_TYPE_LOCK_OUTPUT:
	case COMP_OBJECT_TYPE_DND_ICON:
		return false;
	}

	if (toplevel->fullscreen) {
		return false;
	}

	struct wlr_keyboard *keyboard =
		wlr_seat_get_keyboard(server.seat->wlr_seat);
	uint32_t modifiers = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
	if (modifiers & FLOATING_MOD) {
		switch (event->button) {
		case BTN_LEFT:
			comp_toplevel_begin_interactive(toplevel, COMP_CURSOR_MOVE, 0);
			return true;
		case BTN_RIGHT:;
			uint32_t edge =
				comp_toplevel_get_edge_from_cursor_coords(toplevel, cursor);
			comp_toplevel_begin_interactive(toplevel, COMP_CURSOR_RESIZE, edge);
			return true;
		}
	}

	return false;
}

static void comp_server_cursor_button(struct wl_listener *listener,
									  void *data) {
	/* This event is forwarded by the cursor when a pointer emits a button
	 * event. */
	struct comp_cursor *cursor =
		wl_container_of(listener, cursor, cursor_button);
	struct wlr_pointer_button_event *event = data;
	struct comp_server *server = cursor->server;

	if (server->comp_session_lock.locked) {
		goto notify;
	}

	double sx, sy;
	struct wlr_scene_buffer *scene_buffer = NULL;
	struct wlr_surface *surface = NULL;
	struct comp_object *object =
		comp_object_at(server, cursor->wlr_cursor->x, cursor->wlr_cursor->y,
					   &sx, &sy, &scene_buffer, &surface);
	if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
		// Finish moving tiled window
		if (cursor->cursor_mode == COMP_CURSOR_MOVE &&
			server->seat->grabbed_toplevel &&
			server->seat->grabbed_toplevel->dragging_tiled) {
			tiling_node_move_fini(server->seat->grabbed_toplevel);
		} else if (cursor->cursor_mode == COMP_CURSOR_RESIZE &&
				   server->seat->grabbed_toplevel) {
			tiling_node_resize_fini(server->seat->grabbed_toplevel);
		}

		if (object) {
			switch (object->type) {
			case COMP_OBJECT_TYPE_WIDGET:
				comp_widget_pointer_button(object->data, sx, sy, event);
				break;
			case COMP_OBJECT_TYPE_TOPLEVEL:
			case COMP_OBJECT_TYPE_UNMANAGED:
			case COMP_OBJECT_TYPE_XDG_POPUP:
			case COMP_OBJECT_TYPE_LAYER_SURFACE:;
			case COMP_OBJECT_TYPE_OUTPUT:
			case COMP_OBJECT_TYPE_WORKSPACE:
			case COMP_OBJECT_TYPE_LOCK_OUTPUT:
			case COMP_OBJECT_TYPE_DND_ICON:
				break;
			}
		}

		// Reset focus
		if (surface) {
			wlr_seat_pointer_notify_enter(cursor->seat->wlr_seat, surface, sx,
										  sy);
			wlr_seat_pointer_notify_motion(cursor->seat->wlr_seat,
										   event->time_msec, sx, sy);
		} else {
			wlr_seat_pointer_notify_clear_focus(cursor->seat->wlr_seat);
		}

		/* If you released any buttons, we exit interactive move/resize mode. */
		comp_cursor_reset_cursor_mode(server->seat);
	} else if (object) {
		switch (object->type) {
		case COMP_OBJECT_TYPE_TOPLEVEL:;
			if (try_resize_or_move_toplevel(object, event, cursor)) {
				return;
			}

			// TODO: Also do the same for layer_surface, popups, and unmanaged?
			struct wlr_surface *toplevel_surface =
				comp_toplevel_get_wlr_surface(object->data);
			if (toplevel_surface) {
				/* Focus that client if the button was _pressed_ */
				comp_seat_surface_focus(object, toplevel_surface);
				break;
			}
			/* FALLTHROUGH */
		case COMP_OBJECT_TYPE_XDG_POPUP:
		case COMP_OBJECT_TYPE_LAYER_SURFACE:;
			if (surface) {
				/* Focus that client if the button was _pressed_ */
				comp_seat_surface_focus(object, surface);
			}
			break;
		case COMP_OBJECT_TYPE_WIDGET:
			if (try_resize_or_move_toplevel(object, event, cursor)) {
				return;
			}
			comp_widget_pointer_button(object->data, sx, sy, event);
			break;
		// Don't focus unmanaged. TODO: Check if popup?
		case COMP_OBJECT_TYPE_UNMANAGED:;
			struct wlr_xwayland_surface *xsurface;
			if (surface &&
				(xsurface =
					 wlr_xwayland_surface_try_from_wlr_surface(surface)) &&
				xsurface->override_redirect &&
				wlr_xwayland_or_surface_wants_focus(xsurface)) {
				struct wlr_xwayland *xwayland =
					server->xwayland_mgr.wlr_xwayland;
				wlr_xwayland_set_seat(xwayland, cursor->seat->wlr_seat);
				comp_seat_surface_focus(object, surface);
			}
			break;
		case COMP_OBJECT_TYPE_OUTPUT:
		case COMP_OBJECT_TYPE_WORKSPACE:
		case COMP_OBJECT_TYPE_LOCK_OUTPUT:
		case COMP_OBJECT_TYPE_DND_ICON:
			break;
		}
	}

notify:
	/* Notify the client with pointer focus that a button press has occurred */
	wlr_seat_pointer_notify_button(server->seat->wlr_seat, event->time_msec,
								   event->button, event->state);
}

static void comp_server_cursor_axis(struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits an axis event,
	 * for example when you move the scroll wheel. */
	struct comp_cursor *cursor = wl_container_of(listener, cursor, cursor_axis);
	struct comp_server *server = cursor->server;
	struct wlr_pointer_axis_event *event = data;
	/* Notify the client with pointer focus of the axis event. */
	wlr_seat_pointer_notify_axis(server->seat->wlr_seat, event->time_msec,
								 event->orientation, event->delta,
								 event->delta_discrete, event->source,
								 event->relative_direction);
}

static void comp_server_cursor_frame(struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits an frame
	 * event. Frame events are sent after regular pointer events to group
	 * multiple events together. For instance, two axis events may happen at the
	 * same time, in which case a frame event won't be sent in between. */
	struct comp_cursor *cursor =
		wl_container_of(listener, cursor, cursor_frame);
	/* Notify the client with pointer focus of the frame event. */
	wlr_seat_pointer_notify_frame(server.seat->wlr_seat);
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

struct comp_cursor *comp_cursor_create(struct comp_seat *seat) {
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

	cursor->seat = seat;
	cursor->server = seat->server;
	cursor->cursor_mode = COMP_CURSOR_PASSTHROUGH;

	wlr_cursor_attach_output_layout(wlr_cursor, server.output_layout);

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

/*
 * Pointer constraint
 *
 * Thanks Sway
 */

static void check_constraint_region(struct comp_cursor *cursor) {
	struct wlr_pointer_constraint_v1 *constraint = cursor->active_constraint;
	pixman_region32_t *region = &constraint->region;
	struct comp_toplevel *toplevel =
		comp_toplevel_from_wlr_surface(constraint->surface);
	if (cursor->active_confine_requires_warp && toplevel) {
		cursor->active_confine_requires_warp = false;

		double sx =
			cursor->wlr_cursor->x - toplevel->state.x + toplevel->geometry.x;
		double sy =
			cursor->wlr_cursor->y - toplevel->state.y + toplevel->geometry.y;

		if (!pixman_region32_contains_point(region, floor(sx), floor(sy),
											NULL)) {
			int nboxes;
			pixman_box32_t *boxes = pixman_region32_rectangles(region, &nboxes);
			if (nboxes > 0) {
				double sx = (boxes[0].x1 + boxes[0].x2) / 2.;
				double sy = (boxes[0].y1 + boxes[0].y2) / 2.;

				wlr_cursor_warp_closest(
					cursor->wlr_cursor, NULL,
					sx + toplevel->state.x - toplevel->geometry.x,
					sy + toplevel->state.y - toplevel->geometry.y);

				// cursor_rebase(cursor);
			}
		}
	}

	// A locked pointer will result in an empty region, thus disallowing all
	// movement
	if (constraint->type == WLR_POINTER_CONSTRAINT_V1_CONFINED) {
		pixman_region32_copy(&cursor->confine, region);
	} else {
		pixman_region32_clear(&cursor->confine);
	}
}

static void handle_constraint_commit(struct wl_listener *listener, void *data) {
	struct comp_cursor *cursor =
		wl_container_of(listener, cursor, constraint_commit);
	struct wlr_pointer_constraint_v1 *constraint = cursor->active_constraint;
	assert(constraint->surface == data);

	check_constraint_region(cursor);
}

static void handle_pointer_constraint_set_region(struct wl_listener *listener,
												 void *data) {
	struct comp_pointer_constraint *comp_constraint =
		wl_container_of(listener, comp_constraint, set_region);
	struct comp_cursor *cursor = comp_constraint->cursor;

	cursor->active_confine_requires_warp = true;
};

static void warp_to_constraint_cursor_hint(struct comp_cursor *cursor) {
	struct wlr_pointer_constraint_v1 *constraint = cursor->active_constraint;

	if (constraint->current.cursor_hint.enabled) {
		double sx = constraint->current.cursor_hint.x;
		double sy = constraint->current.cursor_hint.y;

		struct comp_toplevel *toplevel =
			comp_toplevel_from_wlr_surface(constraint->surface);
		if (!toplevel) {
			return;
		}

		double lx = sx + toplevel->state.x - toplevel->geometry.x;
		double ly = sy + toplevel->state.y - toplevel->geometry.y;

		wlr_cursor_warp(cursor->wlr_cursor, NULL, lx, ly);

		// Warp the pointer as well, so that on the next pointer rebase we don't
		// send an unexpected synthetic motion event to clients.
		wlr_seat_pointer_warp(constraint->seat, sx, sy);
	}
}

static void handle_constraint_destroy(struct wl_listener *listener,
									  void *data) {
	struct comp_pointer_constraint *comp_constraint =
		wl_container_of(listener, comp_constraint, destroy);
	struct wlr_pointer_constraint_v1 *constraint = data;
	struct comp_cursor *cursor = comp_constraint->cursor;

	wl_list_remove(&comp_constraint->set_region.link);
	wl_list_remove(&comp_constraint->destroy.link);

	if (cursor->active_constraint == constraint) {
		warp_to_constraint_cursor_hint(cursor);

		if (cursor->constraint_commit.link.next != NULL) {
			wl_list_remove(&cursor->constraint_commit.link);
		}
		wl_list_init(&cursor->constraint_commit.link);
		cursor->active_constraint = NULL;
	}

	free(comp_constraint);
}

void comp_cursor_handle_pointer_constraint(struct wl_listener *listener,
										   void *data) {
	struct wlr_pointer_constraint_v1 *constraint = data;
	struct comp_seat *seat = constraint->seat->data;

	struct comp_pointer_constraint *comp_constraint =
		calloc(1, sizeof(*comp_constraint));
	comp_constraint->cursor = seat->cursor;
	comp_constraint->constraint = constraint;

	comp_constraint->set_region.notify = handle_pointer_constraint_set_region;
	wl_signal_add(&constraint->events.set_region, &comp_constraint->set_region);

	comp_constraint->destroy.notify = handle_constraint_destroy;
	wl_signal_add(&constraint->events.destroy, &comp_constraint->destroy);

	struct wlr_surface *surface =
		seat->wlr_seat->keyboard_state.focused_surface;
	if (surface && surface == constraint->surface) {
		comp_cursor_constrain(seat->cursor, constraint);
	}
}

void comp_cursor_constrain(struct comp_cursor *cursor,
						   struct wlr_pointer_constraint_v1 *constraint) {
	if (cursor->active_constraint == constraint) {
		return;
	}

	wl_list_remove(&cursor->constraint_commit.link);
	if (cursor->active_constraint) {
		if (constraint == NULL) {
			warp_to_constraint_cursor_hint(cursor);
		}
		wlr_pointer_constraint_v1_send_deactivated(cursor->active_constraint);
	}

	cursor->active_constraint = constraint;
	if (constraint == NULL) {
		wl_list_init(&cursor->constraint_commit.link);
		return;
	}

	cursor->active_confine_requires_warp = true;

	// FIXME: Big hack, stolen from wlr_pointer_constraints_v1.c:108.
	// This is necessary because the focus may be set before the surface
	// has finished committing, which means that warping won't work properly,
	// since this code will be run *after* the focus has been set.
	// That is why we duplicate the code here.
	if (pixman_region32_not_empty(&constraint->current.region)) {
		pixman_region32_intersect(&constraint->region,
								  &constraint->surface->input_region,
								  &constraint->current.region);
	} else {
		pixman_region32_copy(&constraint->region,
							 &constraint->surface->input_region);
	}

	check_constraint_region(cursor);

	wlr_pointer_constraint_v1_send_activated(constraint);

	cursor->constraint_commit.notify = handle_constraint_commit;
	wl_signal_add(&constraint->surface->events.commit,
				  &cursor->constraint_commit);
}
