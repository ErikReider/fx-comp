#ifndef FX_COMP_DESKTOP_LAYER_SHELL_H
#define FX_COMP_DESKTOP_LAYER_SHELL_H

#include <wayland-server-core.h>

#include "comp/object.h"
#include "desktop/effects/shadow_data.h"

/*
 * Layer surface
 */

struct comp_layer_surface {
	struct wl_list link;

	struct comp_server *server;
	struct comp_output *output;

	// Child of Object->scene_tree
	struct wlr_scene_layer_surface_v1 *scene_layer;
	struct wlr_layer_surface_v1 *wlr_layer_surface;

	// Signals
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener new_popup;
	struct wl_listener output_destroy;
	struct wl_listener node_destroy;

	bool mapped;

	int initial_width;
	int initial_height;

	struct comp_object object;

	// Effects
	float opacity;
	int corner_radius;
	struct shadow_data shadow_data;
};

void layer_shell_new_surface(struct wl_listener *listener, void *data);

#endif // !FX_COMP_DESKTOP_LAYER_SHELL_H
