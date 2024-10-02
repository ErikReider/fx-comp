#include <stdio.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/backend.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include "comp/lock.h"
#include "comp/object.h"
#include "comp/output.h"
#include "comp/server.h"
#include "comp/widget.h"
#include "comp/workspace.h"
#include "desktop/layer_shell.h"
#include "desktop/toplevel.h"
#include "desktop/widgets/titlebar.h"
#include "desktop/xdg.h"
#include "desktop/xwayland.h"
#include "seat/cursor.h"
#include "seat/keyboard.h"
#include "seat/seat.h"
#include "wlr-layer-shell-unstable-v1-protocol.h"

bool comp_seat_object_is_focus(struct comp_seat *seat,
							   struct comp_object *object) {
	switch (object->type) {
	case COMP_OBJECT_TYPE_WORKSPACE:
		if (server.active_output == NULL ||
			server.active_output->active_workspace == NULL) {
			return false;
		}
		return object == &server.active_output->active_workspace->object;
	case COMP_OBJECT_TYPE_OUTPUT:
		return object == &server.active_output->object;
	case COMP_OBJECT_TYPE_TOPLEVEL:;
		// Only focused if no layer_surface is focused
		struct comp_toplevel *toplevel = object->data;
		return !seat->focused_layer_surface &&
			   toplevel == seat->focused_toplevel;
	case COMP_OBJECT_TYPE_LAYER_SURFACE:;
		return object->data == seat->focused_layer_surface;
	case COMP_OBJECT_TYPE_UNMANAGED:;
		struct comp_xwayland_unmanaged *unmanaged = object->data;
		if (unmanaged && unmanaged->xwayland_surface &&
			unmanaged->xwayland_surface->surface) {
			struct wlr_surface *surface = unmanaged->xwayland_surface->surface;
			return surface == seat->wlr_seat->keyboard_state.focused_surface ||
				   surface == seat->wlr_seat->pointer_state.focused_surface;
		}
		break;
	case COMP_OBJECT_TYPE_WIDGET:
		break;
	case COMP_OBJECT_TYPE_XDG_POPUP:;
		struct comp_xdg_popup *popup = object->data;
		if (popup->parent_object == NULL) {
			break;
		}
		return comp_seat_object_is_focus(seat, popup->parent_object);
	case COMP_OBJECT_TYPE_LOCK_OUTPUT:
		// TODO: Handle this?
		break;
	}
	return false;
}

static void server_new_pointer(struct comp_seat *seat,
							   struct wlr_input_device *device) {
	/* We don't do anything special with pointers. All of our pointer handling
	 * is proxied through wlr_cursor. On another compositor, you might take this
	 * opportunity to do libinput configuration on the device to set
	 * acceleration, etc. */
	wlr_cursor_attach_input_device(seat->cursor->wlr_cursor, device);
}

static void seat_new_input(struct wl_listener *listener, void *data) {
	/* This event is raised by the backend when a new input device becomes
	 * available. */
	struct comp_seat *seat = wl_container_of(listener, seat, new_input);
	struct wlr_input_device *device = data;

	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		comp_keyboard_create(seat, device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		server_new_pointer(seat, device);

		// Map the cursor to the output
		const char *mapped_to_output =
			wlr_pointer_from_input_device(device)->output_name;
		if (mapped_to_output == NULL) {
			break;
		}
		wlr_log(WLR_DEBUG, "Mapping input device %s to output %s", device->name,
				mapped_to_output);
		if (strcmp("*", mapped_to_output) == 0) {
			wlr_cursor_map_input_to_output(seat->cursor->wlr_cursor, device,
										   NULL);
			wlr_cursor_map_input_to_region(seat->cursor->wlr_cursor, device,
										   NULL);
			wlr_log(WLR_DEBUG, "Reset output mapping");
			return;
		}
		struct comp_output *output =
			comp_output_by_name_or_id(mapped_to_output);
		if (!output) {
			wlr_log(WLR_DEBUG,
					"Requested output %s for device %s isn't present",
					mapped_to_output, device->name);
			return;
		}
		wlr_cursor_map_input_to_output(seat->cursor->wlr_cursor, device,
									   output->wlr_output);
		wlr_cursor_map_input_to_region(seat->cursor->wlr_cursor, device, NULL);
		wlr_log(WLR_DEBUG, "Mapped to output %s", output->wlr_output->name);
		break;
	default:
		break;
	}
	/* We need to let the wlr_seat know what our capabilities are, which is
	 * communiciated to the client. In TinyWL we always have a cursor, even if
	 * there are no pointer devices, so we always include that capability. */
	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&seat->keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(seat->wlr_seat, caps);
}

static void seat_request_cursor(struct wl_listener *listener, void *data) {
	struct comp_seat *seat = wl_container_of(listener, seat, request_cursor);
	/* This event is raised by the seat when a client provides a cursor image */
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	struct wlr_seat_client *focused_client =
		seat->wlr_seat->pointer_state.focused_client;
	/* This can be sent by any client, so we check to make sure this one is
	 * actually has pointer focus first. */
	if (focused_client == event->seat_client) {
		/* Once we've vetted the client, we can tell the cursor to use the
		 * provided surface as the cursor image. It will set the hardware cursor
		 * on the output that it's currently on and continue to do so as the
		 * cursor moves between outputs. */
		wlr_cursor_set_surface(seat->cursor->wlr_cursor, event->surface,
							   event->hotspot_x, event->hotspot_y);
	}
}

static void seat_request_set_selection(struct wl_listener *listener,
									   void *data) {
	/* This event is raised by the seat when a client wants to set the
	 * selection, usually when the user copies something. wlroots allows
	 * compositors to ignore such requests if they so choose, but in tinywl we
	 * always honor
	 */
	struct comp_seat *seat =
		wl_container_of(listener, seat, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(seat->wlr_seat, event->source, event->serial);
}

static void seat_focus_previous_toplevel(struct comp_workspace *ws,
										 struct wlr_surface *surface) {
	if (!ws) {
		wlr_log(WLR_ERROR,
				"Tried to focus previous toplevel on NULL workspace!");
		return;
	}
	struct comp_toplevel *toplevels[2] = {};
	if (!wl_list_empty(&ws->toplevels)) {
		// Workspace focus order
		struct comp_toplevel *toplevel;
		wl_list_for_each(toplevel, &ws->toplevels, workspace_link) {
			struct wlr_surface *toplevel_surface =
				comp_toplevel_get_wlr_surface(toplevel);
			if (toplevel_surface && toplevel_surface != surface) {
				toplevels[0] = toplevel;
				break;
			}
		}
	}
	if (!wl_list_empty(&ws->toplevels)) {
		// Use seat focus order as fallback
		struct comp_toplevel *seat_toplevel = wl_container_of(
			server.seat->focus_order.next, seat_toplevel, focus_link);
		toplevels[1] = seat_toplevel;
	}

	// Focus previous node
	for (size_t i = 0; i < 2; i++) {
		struct comp_toplevel *toplevel = toplevels[i];
		if (toplevel) {
			struct wlr_surface *toplevel_surface =
				comp_toplevel_get_wlr_surface(toplevel);
			if (toplevel_surface && toplevel_surface != surface) {
				comp_seat_surface_focus(&toplevel->object, toplevel_surface);
				return;
			}
		}
	}
}

void comp_seat_unfocus_unless_client(struct wl_client *client) {
	struct comp_layer_surface *focused_layer =
		server.seat->focused_layer_surface;
	if (focused_layer && focused_layer->wlr_layer_surface) {
		if (wl_resource_get_client(
				focused_layer->wlr_layer_surface->resource) != client) {
			comp_seat_surface_unfocus(focused_layer->wlr_layer_surface->surface,
									  false);
		}
	}

	struct comp_toplevel *focused_toplevel = server.seat->focused_toplevel;
	if (focused_toplevel) {
		struct wlr_surface *surface =
			comp_toplevel_get_wlr_surface(focused_toplevel);
		if (surface && wl_resource_get_client(surface->resource) != client) {
			comp_seat_surface_unfocus(surface, false);
		}
	}

	if (server.seat->wlr_seat->pointer_state.focused_client) {
		if (server.seat->wlr_seat->pointer_state.focused_client->client !=
			client) {
			wlr_seat_pointer_notify_clear_focus(server.seat->wlr_seat);
		}
	}
}

void comp_seat_surface_unfocus(struct wlr_surface *surface,
							   bool focus_previous) {
	// Lock:
	if (server.comp_session_lock.locked) {
		// Trying to unfocus non locked surface, refocusing locked
		// surface...
		comp_session_lock_refocus();
		return;
	}
	// Unlock:
	// Refocus previous focus on unlock
	if (surface == server.comp_session_lock.focused) {
		wlr_seat_keyboard_notify_clear_focus(server.seat->wlr_seat);
		wlr_seat_pointer_notify_clear_focus(server.seat->wlr_seat);

		// Focus previous toplevel in focus order
		struct comp_object *object = surface->data;
		struct comp_session_lock_output *lock_output = object->data;
		struct comp_output *focused_output = NULL;
		if (lock_output) {
			focused_output = lock_output->output;
		}
		if (!focused_output) {
			focused_output = get_active_output(&server);
		}
		if (focused_output) {
			seat_focus_previous_toplevel(focused_output->active_workspace,
										 surface);
		}
		return;
	}

	if (surface == NULL) {
		wlr_log(WLR_ERROR, "Tried to unfocus NULL surface");
		return;
	}
	// XDG Toplevel
	struct wlr_xdg_surface *xdg_surface;
	if ((xdg_surface = wlr_xdg_surface_try_from_wlr_surface(surface)) &&
		xdg_surface->toplevel) {
		wlr_xdg_toplevel_set_activated(xdg_surface->toplevel, false);

		struct wlr_scene_tree *scene_tree = xdg_surface->data;
		struct comp_object *object = scene_tree->node.data;
		struct comp_toplevel *toplevel;
		if (object && object->type == COMP_OBJECT_TYPE_TOPLEVEL &&
			(toplevel = object->data)) {
			if (toplevel == server.seat->focused_toplevel) {
				server.seat->focused_toplevel = NULL;
			}

			if (focus_previous) {
				seat_focus_previous_toplevel(toplevel->state.workspace,
											 surface);
			}

			/*
			 * Redraw
			 */
			if (toplevel->titlebar) {
				comp_widget_draw_full(&toplevel->titlebar->widget);
			}
		}
		return;
	}

	// XWayland Toplevel
	struct wlr_xwayland_surface *xsurface;
	if ((xsurface = wlr_xwayland_surface_try_from_wlr_surface(surface))) {
		wlr_xwayland_surface_activate(xsurface, false);

		struct wlr_scene_tree *scene_tree = xsurface->data;
		struct comp_object *object = scene_tree->node.data;
		struct comp_toplevel *toplevel;
		if (object && object->type == COMP_OBJECT_TYPE_TOPLEVEL &&
			(toplevel = object->data)) {
			if (toplevel == server.seat->focused_toplevel) {
				server.seat->focused_toplevel = NULL;
			}

			if (focus_previous) {
				seat_focus_previous_toplevel(toplevel->state.workspace,
											 surface);
			}

			/*
			 * Redraw
			 */
			if (toplevel->titlebar) {
				comp_widget_draw_full(&toplevel->titlebar->widget);
			}
		}
		return;
	}

	// Layer Shell
	struct wlr_layer_surface_v1 *wlr_layer_surface;
	if ((wlr_layer_surface =
			 wlr_layer_surface_v1_try_from_wlr_surface(surface))) {
		struct wlr_scene_tree *scene_tree = wlr_layer_surface->data;

		struct comp_object *object = scene_tree->node.data;
		struct comp_layer_surface *layer_surface;
		if (object && object->type == COMP_OBJECT_TYPE_LAYER_SURFACE &&
			(layer_surface = object->data) &&
			layer_surface == server.seat->focused_layer_surface) {
			server.seat->focused_layer_surface = NULL;
		}

		if (focus_previous) {
			seat_focus_previous_toplevel(
				layer_surface->output->active_workspace, surface);
		}
		return;
	}
}

/*
 * Tell the seat to have the keyboard enter this surface. wlroots will keep
 * track of this and automatically send key events to the appropriate
 * clients without additional work on your part.
 */
static void seat_focus_surface(struct wlr_surface *surface) {
	struct wlr_seat *wlr_seat = server.seat->wlr_seat;
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(wlr_seat);
	if (keyboard != NULL) {
		wlr_seat_keyboard_notify_enter(wlr_seat, surface, keyboard->keycodes,
									   keyboard->num_keycodes,
									   &keyboard->modifiers);
	} else {
		wlr_seat_keyboard_notify_enter(wlr_seat, surface, NULL, 0, NULL);
	}
}

void comp_seat_surface_focus(struct comp_object *object,
							 struct wlr_surface *surface) {
	/* Note: this function only deals with keyboard focus. */
	if (object == NULL || !object->data) {
		return;
	}

	// Refocus the locked output focus if locked
	if (object->type != COMP_OBJECT_TYPE_LOCK_OUTPUT &&
		server.comp_session_lock.locked) {
		comp_session_lock_refocus();
		return;
	}

	struct comp_seat *seat = server.seat;
	struct comp_toplevel *toplevel = object->data;
	struct comp_layer_surface *layer_surface = object->data;
	struct comp_layer_surface *focused_layer = seat->focused_layer_surface;

	switch (object->type) {
	case COMP_OBJECT_TYPE_TOPLEVEL:;
		// Check for locked is checked above, so not needed here
		if (seat->exclusive_layer && focused_layer) {
			// Hacky... Some toplevels like kitty needs to be focused then
			// unfocused
			seat_focus_surface(surface);
			wlr_seat_keyboard_notify_clear_focus(seat->wlr_seat);
			comp_seat_surface_unfocus(surface, false);
			comp_seat_surface_focus(&focused_layer->object,
									focused_layer->wlr_layer_surface->surface);
			return;
		}
		break;
	case COMP_OBJECT_TYPE_LAYER_SURFACE:;
		if (focused_layer == layer_surface) {
			seat->exclusive_layer = false;
		}
		switch (
			layer_surface->wlr_layer_surface->current.keyboard_interactive) {
		case ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE:
			comp_seat_surface_unfocus(surface, true);
			return;
		case ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE:
			seat->exclusive_layer = true;
			break;
		case ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND:
			break;
		}
		break;
	case COMP_OBJECT_TYPE_LOCK_OUTPUT:
		break;
	case COMP_OBJECT_TYPE_WIDGET:
	case COMP_OBJECT_TYPE_XDG_POPUP:
	case COMP_OBJECT_TYPE_OUTPUT:
	case COMP_OBJECT_TYPE_WORKSPACE:
	case COMP_OBJECT_TYPE_UNMANAGED:
		return;
	}

	struct wlr_seat *wlr_seat = seat->wlr_seat;
	struct wlr_surface *prev_surface = wlr_seat->keyboard_state.focused_surface;
	if (prev_surface == surface) {
		/* Don't re-focus an already focused surface. */
		return;
	}
	if (prev_surface && !server.comp_session_lock.locked) {
		/*
		 * Deactivate the previously focused surface. This lets the client know
		 * it no longer has focus and the client will repaint accordingly, e.g.
		 * stop displaying a caret.
		 */
		comp_seat_surface_unfocus(prev_surface, false);
	}

	switch (object->type) {
	case COMP_OBJECT_TYPE_TOPLEVEL:;
		seat->focused_toplevel = toplevel;
		/* Activate the new surface */
		comp_toplevel_set_activated(toplevel, true);

		/* Move the node to the front */
		// Workspace
		wl_list_remove(&toplevel->workspace_link);
		wl_list_insert(&toplevel->state.workspace->toplevels,
					   &toplevel->workspace_link);
		// Seat
		wl_list_remove(&toplevel->focus_link);
		wl_list_insert(&server.seat->focus_order, &toplevel->focus_link);

		// Set XWayland seat
		if (toplevel->type == COMP_TOPLEVEL_TYPE_XWAYLAND) {
			struct wlr_xwayland *xwayland = server.xwayland_mgr.wlr_xwayland;
			wlr_xwayland_set_seat(xwayland, seat->wlr_seat);
		}
		break;
	case COMP_OBJECT_TYPE_LAYER_SURFACE:;
		seat->focused_layer_surface = layer_surface;
		break;
	case COMP_OBJECT_TYPE_LOCK_OUTPUT:
		break;
	case COMP_OBJECT_TYPE_UNMANAGED:
	case COMP_OBJECT_TYPE_WIDGET:
	case COMP_OBJECT_TYPE_XDG_POPUP:
	case COMP_OBJECT_TYPE_OUTPUT:
	case COMP_OBJECT_TYPE_WORKSPACE:
		return;
	}

	/* Move the node to the front */
	wlr_scene_node_raise_to_top(&object->scene_tree->node);

	seat_focus_surface(surface);

	switch (object->type) {
	case COMP_OBJECT_TYPE_TOPLEVEL:;
		struct comp_toplevel *toplevel = object->data;
		/*
		 * Redraw
		 */
		comp_widget_draw_full(&toplevel->titlebar->widget);
		break;
	case COMP_OBJECT_TYPE_UNMANAGED:
	case COMP_OBJECT_TYPE_XDG_POPUP:
	case COMP_OBJECT_TYPE_LAYER_SURFACE:
	case COMP_OBJECT_TYPE_WIDGET:
	case COMP_OBJECT_TYPE_OUTPUT:
	case COMP_OBJECT_TYPE_WORKSPACE:
	case COMP_OBJECT_TYPE_LOCK_OUTPUT:
		return;
	}
}

struct comp_seat *comp_seat_create(struct comp_server *server) {
	struct comp_seat *seat = calloc(1, sizeof(*seat));
	if (!seat) {
		wlr_log(WLR_ERROR, "Could not allocate comp_seat");
		return NULL;
	}
	seat->server = server;

	wl_list_init(&seat->focus_order);

	/*
	 * Keyboard
	 */

	/*
	 * Configures a seat, which is a single "seat" at which a user sits and
	 * operates the computer. This conceptually includes up to one keyboard,
	 * pointer, touch, and drawing tablet device. We also rig up a listener to
	 * let us know when new input devices are available on the backend.
	 */
	wl_list_init(&seat->keyboards);
	seat->new_input.notify = seat_new_input;
	wl_signal_add(&server->backend->events.new_input, &seat->new_input);
	seat->wlr_seat = wlr_seat_create(server->wl_display, "seat0");
	seat->request_cursor.notify = seat_request_cursor;
	wl_signal_add(&seat->wlr_seat->events.request_set_cursor,
				  &seat->request_cursor);
	seat->request_set_selection.notify = seat_request_set_selection;
	wl_signal_add(&seat->wlr_seat->events.request_set_selection,
				  &seat->request_set_selection);

	/*
	 * Cursor
	 */

	/*
	 * Creates a cursor, which is a wlroots utility for tracking the cursor
	 * image shown on screen.
	 */
	seat->cursor = comp_cursor_create(seat);

	return seat;
}
