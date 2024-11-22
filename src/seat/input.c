#include <assert.h>
#include <libinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <wlr/backend/libinput.h>
#include <wlr/config.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_seat.h>

#include "constants.h"
#include "seat/input.h"
#include "seat/seat.h"
#include "wlr/util/log.h"

static struct xkb_keymap *get_keymap(struct wlr_keyboard *kb) {
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_SECURE_GETENV);
	if (!context) {
		wlr_log(WLR_ERROR, "Could not create new XKB Context");
		return NULL;
	}

	struct xkb_rule_names rules = {
		.layout = INPUT_KB_XKB_LAYOUT,
		.rules = INPUT_KB_XKB_RULES,
		.model = INPUT_KB_XKB_MODEL,
		.options = INPUT_KB_XKB_OPTIONS,
		.variant = INPUT_KB_XKB_VARIANT,
	};
	struct xkb_keymap *keymap =
		xkb_keymap_new_from_names(context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);

	xkb_context_unref(context);
	return keymap;
}

static void set_keyboard_layout(struct wlr_keyboard *kb) {
	struct xkb_keymap *keymap = get_keymap(kb);
	if (!keymap) {
		wlr_log(WLR_ERROR, "Could not compile XKB Layout");
		return;
	}

	wlr_keyboard_set_keymap(kb, keymap);

	xkb_keymap_unref(keymap);
}

static void libinput_configure(struct wlr_input_device *wlr_device) {
	if (!wlr_input_device_is_libinput(wlr_device)) {
		return;
	}

	assert(WLR_HAS_LIBINPUT_BACKEND);
	struct libinput_device *device = wlr_libinput_get_device_handle(wlr_device);

	uint32_t click_methods = libinput_device_config_click_get_methods(device);
	if (click_methods != LIBINPUT_CONFIG_CLICK_METHOD_NONE) {
		libinput_device_config_click_set_clickfinger_button_map(
			device, INPUT_POINTER_CLICK_METHOD);
	}

	if (libinput_device_config_tap_get_finger_count(device) > 0) {
		libinput_device_config_tap_set_enabled(device,
											   INPUT_POINTER_TAP_METHOD);
		libinput_device_config_tap_set_button_map(
			device, INPUT_POINTER_TAP_BUTTON_METHOD);
		libinput_device_config_tap_set_drag_enabled(device, INPUT_POINTER_DRAG);
		libinput_device_config_tap_set_drag_lock_enabled(
			device, INPUT_POINTER_DRAG_LOCK);
	}

	if (libinput_device_config_dwt_is_available(device)) {
		libinput_device_config_dwt_set_enabled(device, INPUT_POINTER_DWT);
	}

	if (libinput_device_config_dwtp_is_available(device)) {
		libinput_device_config_dwtp_set_enabled(device, INPUT_POINTER_DWTP);
	}

	// Disable pointer when external mouse is connected
	libinput_device_config_send_events_set_mode(device, INPUT_POINTER_EVENTS);

	if (libinput_device_config_left_handed_is_available(device)) {
		libinput_device_config_left_handed_set(device,
											   INPUT_POINTER_LEFT_HANDED);
	}

	if (libinput_device_config_middle_emulation_is_available(device)) {
		libinput_device_config_middle_emulation_set_enabled(
			device, INPUT_POINTER_MIDDLE_EMULATION);
	}

	if (libinput_device_config_scroll_has_natural_scroll(device)) {
		libinput_device_config_scroll_set_natural_scroll_enabled(
			device, INPUT_POINTER_NATURAL_SCROLL);
	}

	if (libinput_device_config_accel_is_available(device)) {
		libinput_device_config_accel_set_speed(device,
											   INPUT_POINTER_ACCEL_SPEED);
	}

	uint32_t scroll_methods = libinput_device_config_scroll_get_methods(device);
	if (scroll_methods != LIBINPUT_CONFIG_SCROLL_NO_SCROLL) {
		libinput_device_config_scroll_set_method(device,
												 INPUT_POINTER_SCROLL_METHOD);
	}
}

void comp_input_configure_device(struct wlr_input_device *device) {
	struct comp_seat *seat = device->data;
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD: {
		struct wlr_keyboard *kb = wlr_keyboard_from_input_device(device);
		wlr_keyboard_set_repeat_info(kb, INPUT_KB_REPEAT_RATE,
									 INPUT_KB_REPEAT_DELAY);
		set_keyboard_layout(kb);
		struct wlr_keyboard *current_keyboard =
			seat->wlr_seat->keyboard_state.keyboard;
		if (!current_keyboard) {
			wlr_seat_set_keyboard(device->data, kb);
		}

		libinput_configure(device);
		break;
	}
	case WLR_INPUT_DEVICE_POINTER: {
		libinput_configure(device);
		break;
	}
	case WLR_INPUT_DEVICE_TOUCH:
	case WLR_INPUT_DEVICE_TABLET:
	case WLR_INPUT_DEVICE_TABLET_PAD:
	case WLR_INPUT_DEVICE_SWITCH:
		wlr_log(WLR_ERROR, "Input [\"%s\"]: Skipping configure of type %i",
				device->name, device->type);
		return;
	}
}
