#ifndef FX_COMP_LOCK_H
#define FX_COMP_LOCK_H

#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_output.h>

#include "comp/object.h"

struct comp_session_lock_output {
	struct wl_list link;

	struct comp_object object;

	struct comp_output *output;
	struct wlr_scene_rect *background;

	struct wlr_session_lock_surface_v1 *surface;

	struct wl_listener destroy;
	// invalid if surface is NULL
	struct wl_listener surface_destroy;
	struct wl_listener surface_map;
};

void comp_session_lock_arrange(void);

void comp_session_lock_refocus(void);

void comp_session_lock_add_output(struct wlr_output *wlr_output);

void comp_session_lock_create(void);

#endif // !FX_COMP_LOCK_H
