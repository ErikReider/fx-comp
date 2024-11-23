#include <stdlib.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include "comp/output.h"
#include "comp/server.h"
#include "comp/workspace.h"
#include "constants.h"
#include "desktop/toplevel.h"
#include "seat/cursor.h"
#include "seat/keyboard.h"
#include "seat/seat.h"
#include "util.h"

static void keyboard_handle_modifiers(struct wl_listener *listener,
									  void *data) {
	/* This event is raised when a modifier key, such as shift or alt, is
	 * pressed. We simply communicate this to the client. */
	struct comp_keyboard *keyboard =
		wl_container_of(listener, keyboard, modifiers);
	/*
	 * A seat can only have one keyboard, but this is a limitation of the
	 * Wayland protocol - not wlroots. We assign all connected keyboards to the
	 * same seat. You can swap out the underlying wlr_keyboard like this and
	 * wlr_seat handles this transparently.
	 */
	wlr_seat_set_keyboard(keyboard->seat->wlr_seat, keyboard->wlr_keyboard);
	/* Send modifiers to the client. */
	wlr_seat_keyboard_notify_modifiers(keyboard->seat->wlr_seat,
									   &keyboard->wlr_keyboard->modifiers);
}

static bool handle_keybinding(struct comp_server *server, int modifier,
							  xkb_keysym_t sym) {
	/*
	 * Here we handle compositor keybindings. This is when the compositor is
	 * processing keys, rather than passing them on to the client for its own
	 * processing.
	 *
	 * This function assumes Alt/Super is held down.
	 */
	struct comp_output *output = get_active_output(server);
	struct comp_workspace *workspace = output->active_workspace;

	struct comp_toplevel *focused_toplevel = server->seat->focused_toplevel;
	switch (modifier) {
	case WLR_MODIFIER_ALT:
		switch (sym) {
		case XKB_KEY_Escape:
			wl_display_terminate(server->wl_display);
			break;
		case XKB_KEY_F1:;
			/* Cycle to the next view */
			if (wl_list_length(&workspace->toplevels) < 2) {
				break;
			}
			struct comp_toplevel *next_toplevel =
				comp_workspace_get_next_focused(workspace);
			comp_seat_surface_focus(
				&next_toplevel->object,
				comp_toplevel_get_wlr_surface(next_toplevel));
			break;

		case XKB_KEY_Q:;
			if (focused_toplevel) {
				comp_toplevel_close(focused_toplevel);
			}
			return true;

		case XKB_KEY_Return:
			exec(TERM);
			return true;

		case XKB_KEY_O:
			comp_create_extra_output();
			return true;

		case XKB_KEY_f:
			// Toggle between tiling and floating
			if (focused_toplevel) {
				comp_toplevel_toggle_tiled(focused_toplevel);
			}
			return true;

		case XKB_KEY_h:
			// Set minimized
			if (focused_toplevel) {
				comp_toplevel_set_minimized(focused_toplevel, true);
			}
			return true;

		case XKB_KEY_F:;
			// Toggle fullscreen
			if (focused_toplevel) {
				comp_toplevel_toggle_fullscreen(focused_toplevel);
			}
			return true;

		case XKB_KEY_Left: {
			struct comp_toplevel *toplevel =
				comp_workspace_get_toplevel_direction(workspace,
													  WLR_DIRECTION_LEFT);
			if (toplevel) {
				comp_seat_surface_focus(
					&toplevel->object, comp_toplevel_get_wlr_surface(toplevel));
			}
			// struct comp_workspace *ws =
			// 	comp_output_prev_workspace(output, true);
			// comp_output_focus_workspace(output, ws);
			break;
		}
		case XKB_KEY_Right: {
			struct comp_toplevel *toplevel =
				comp_workspace_get_toplevel_direction(workspace,
													  WLR_DIRECTION_RIGHT);
			if (toplevel) {
				comp_seat_surface_focus(
					&toplevel->object, comp_toplevel_get_wlr_surface(toplevel));
			}
			// struct comp_workspace *ws =
			// 	comp_output_next_workspace(output, true);
			// comp_output_focus_workspace(output, ws);
			break;
		}
		case XKB_KEY_Up: {
			struct comp_toplevel *toplevel =
				comp_workspace_get_toplevel_direction(workspace,
													  WLR_DIRECTION_UP);
			if (toplevel) {
				comp_seat_surface_focus(
					&toplevel->object, comp_toplevel_get_wlr_surface(toplevel));
			}
			break;
		}
		case XKB_KEY_Down: {
			struct comp_toplevel *toplevel =
				comp_workspace_get_toplevel_direction(workspace,
													  WLR_DIRECTION_DOWN);
			if (toplevel) {
				comp_seat_surface_focus(
					&toplevel->object, comp_toplevel_get_wlr_surface(toplevel));
			}
			break;
		}

		case XKB_KEY_N:
		case XKB_KEY_n:
			comp_output_new_workspace(output, COMP_WORKSPACE_TYPE_REGULAR);
			return true;

		case XKB_KEY_M:
		case XKB_KEY_m:
			comp_output_remove_workspace(output, output->active_workspace);
			return true;

		default:
			return false;
		}
		break;
	case WLR_MODIFIER_LOGO:
		switch (sym) {
		case XKB_KEY_Tab:;
			struct comp_workspace *ws =
				comp_output_next_workspace(output, true);
			comp_output_focus_workspace(output, ws);
			break;

		default:
			return false;
		}
		break;
	default:
		break;
	}

	return true;
}

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
	/* This event is raised when a key is pressed or released. */
	struct comp_keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct comp_server *server = keyboard->server;
	struct wlr_keyboard_key_event *event = data;
	struct comp_seat *seat = server->seat;
	struct wlr_seat *wlr_seat = seat->wlr_seat;

	/* Translate libinput keycode -> xkbcommon */
	uint32_t keycode = event->keycode + 8;
	/* Get a list of keysyms based on the keymap for this keyboard */
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state,
									   keycode, &syms);

	bool handled = false;
	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);

	if (!server->comp_session_lock.locked &&
		event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		int bitmask = 1 << (WLR_MODIFIER_COUNT - 1);
		int mask = 1;
		while (bitmask) {
			switch (modifiers & mask) {
			case WLR_MODIFIER_ALT:
				/* If alt is held down and this button was _pressed_, we attempt
				 * to process it as a compositor keybinding. */
				for (int i = 0; i < nsyms; i++) {
					handled =
						handle_keybinding(server, WLR_MODIFIER_ALT, syms[i]);
				}
				break;
			case WLR_MODIFIER_SHIFT:
				break;
			case WLR_MODIFIER_CAPS:
				break;
			case WLR_MODIFIER_CTRL:
				break;
			case WLR_MODIFIER_MOD2:
				break;
			case WLR_MODIFIER_MOD3:
				break;
			case WLR_MODIFIER_LOGO:
				/* If the LOGO key (super/win) is held down and this button was
				 * _pressed_, we attempt to process it as a compositor
				 * keybinding. */
				for (int i = 0; i < nsyms; i++) {
					handled =
						handle_keybinding(server, WLR_MODIFIER_LOGO, syms[i]);
				}
				break;
			case WLR_MODIFIER_MOD5:
				break;
			}
			bitmask &= ~mask;
			mask <<= 1;
		}
	}

	// TODO: Handling for wlr_session_change_vt

	if (!handled && seat->wlr_seat->keyboard_state.focused_surface) {
		/* Otherwise, we pass it along to the client. */
		wlr_seat_set_keyboard(wlr_seat, keyboard->wlr_keyboard);
		wlr_seat_keyboard_notify_key(wlr_seat, event->time_msec, event->keycode,
									 event->state);
	}
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
	/* This event is raised by the keyboard base wlr_input_device to signal
	 * the destruction of the wlr_keyboard. It will no longer receive events
	 * and should be destroyed.
	 */
	struct comp_keyboard *keyboard =
		wl_container_of(listener, keyboard, destroy);
	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);
	free(keyboard);
}

void comp_keyboard_create(struct comp_seat *seat,
						  struct wlr_input_device *device) {
	struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

	struct comp_keyboard *keyboard = calloc(1, sizeof(*keyboard));
	if (!keyboard) {
		wlr_log(WLR_ERROR, "Could not allocate comp_keyboard");
		return;
	}
	keyboard->server = seat->server;
	keyboard->seat = seat;
	keyboard->wlr_keyboard = wlr_keyboard;

	/* We need to prepare an XKB keymap and assign it to the keyboard. This
	 * assumes the defaults (e.g. layout = "us"). */
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap =
		xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);

	wlr_keyboard_set_keymap(wlr_keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);

	/* Here we set up listeners for keyboard events. */
	keyboard->modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = keyboard_handle_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
	keyboard->destroy.notify = keyboard_handle_destroy;
	wl_signal_add(&device->events.destroy, &keyboard->destroy);

	wlr_seat_set_keyboard(seat->wlr_seat, keyboard->wlr_keyboard);

	/* And add the keyboard to our list of keyboards */
	wl_list_insert(&seat->keyboards, &keyboard->link);
}
