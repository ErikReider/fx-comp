#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include "comp/object.h"
#include "comp/output.h"
#include "comp/server.h"
#include "constants.h"
#include "desktop/layer_shell.h"
#include "desktop/xdg.h"
#include "seat/seat.h"
#include "util.h"
#include "wlr-layer-shell-unstable-v1-protocol.h"

static struct wlr_scene_tree *
layer_get_scene_tree(struct comp_output *output,
					 enum zwlr_layer_shell_v1_layer type) {
	switch (type) {
	case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
		return output->layers.shell_background;
	case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
		return output->layers.shell_bottom;
	case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
		return output->layers.shell_top;
	case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
		return output->layers.shell_overlay;
	}

	assert(false);
	return NULL;
}

/*
 * Layer surface logic
 */

static void layer_surface_map(struct wl_listener *listener, void *data) {
	/* Called when the surface is mapped, or ready to display on-screen. */
	struct comp_layer_surface *layer_surface =
		wl_container_of(listener, layer_surface, map);
	struct comp_seat *seat = server.seat;

	struct wlr_layer_surface_v1 *wlr_layer_surface =
		layer_surface->scene_layer->layer_surface;

	// TODO: Don't grab focus away from toplevel if not exclusive

	// focus on new surface
	if (wlr_layer_surface->current.keyboard_interactive &&
		(wlr_layer_surface->current.layer ==
			 ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY ||
		 wlr_layer_surface->current.layer == ZWLR_LAYER_SHELL_V1_LAYER_TOP) &&
		// Only steal focus if it's exclusive or if there's already a focused
		// layer. Prevents ON_DEMAND layers from stealing focus from already
		// focused toplevels
		(seat->focused_layer_surface ||
		 wlr_layer_surface->current.keyboard_interactive ==
			 ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE)) {

		// but only if the currently focused layer has a lower precedence
		if (!seat->focused_layer_surface ||
			seat->focused_layer_surface->wlr_layer_surface->current.layer >=
				wlr_layer_surface->current.layer) {
			comp_seat_surface_focus(&layer_surface->object,
									wlr_layer_surface->surface);
		}
		comp_output_arrange_layers(layer_surface->output);
	}

	// TODO: Change cursor
}

static void layer_surface_commit(struct wl_listener *listener, void *data) {
	/* Called when the surface is mapped, or ready to display on-screen. */
	struct comp_layer_surface *layer_surface =
		wl_container_of(listener, layer_surface, commit);

	struct wlr_layer_surface_v1 *wlr_layer_surface =
		layer_surface->wlr_layer_surface;
	if (!wlr_layer_surface->initialized) {
		return;
	}

	if (wlr_layer_surface->current.layer ==
			ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND ||
		wlr_layer_surface->current.layer == ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM) {
		wlr_scene_optimized_blur_mark_dirty(
			layer_surface->output->layers.optimized_blur_node);
	}

	uint32_t committed = wlr_layer_surface->current.committed;
	// Layer change, switch scene_tree
	if (committed & WLR_LAYER_SURFACE_V1_STATE_LAYER) {
		enum zwlr_layer_shell_v1_layer layer_type =
			wlr_layer_surface->current.layer;
		struct wlr_scene_tree *output_layer =
			layer_get_scene_tree(layer_surface->output, layer_type);
		wlr_scene_node_reparent(&layer_surface->scene_layer->tree->node,
								output_layer);
	}

	// Update focus on interactivity change, but only if the layer is exclusive
	// or if it's already focused
	if (committed & WLR_LAYER_SURFACE_V1_STATE_KEYBOARD_INTERACTIVITY &&
		(wlr_layer_surface->current.keyboard_interactive ==
			 ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE ||
		 server.seat->focused_layer_surface == layer_surface)) {
		comp_seat_surface_focus(&layer_surface->object,
								wlr_layer_surface->surface);
	}

	if (wlr_layer_surface->initial_commit || committed ||
		wlr_layer_surface->surface->mapped != layer_surface->mapped) {
		layer_surface->mapped = wlr_layer_surface->surface->mapped;
		comp_output_arrange_layers(layer_surface->output);
	}
}

static void layer_surface_unmap(struct wl_listener *listener, void *data) {
	/* Called when the surface is unmapped, and should no longer be shown. */
	struct comp_layer_surface *surface =
		wl_container_of(listener, surface, unmap);

	// Focus previous surface
	comp_seat_surface_unfocus(surface->wlr_layer_surface->surface, true);

	// TODO: Change cursor
}

static void layer_output_destroy(struct wl_listener *listener, void *data) {
	struct comp_layer_surface *layer_surface =
		wl_container_of(listener, layer_surface, output_destroy);

	layer_surface->output = NULL;

	wlr_scene_node_destroy(&layer_surface->object.scene_tree->node);
}

static void layer_surface_node_destroy(struct wl_listener *listener,
									   void *data) {
	struct comp_layer_surface *layer_surface =
		wl_container_of(listener, layer_surface, node_destroy);

	struct wlr_layer_surface_v1 *wlr_layer_surface =
		layer_surface->wlr_layer_surface;
	if (wlr_layer_surface->current.layer ==
			ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND ||
		wlr_layer_surface->current.layer == ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM) {
		if (layer_surface->output) {
			wlr_scene_optimized_blur_mark_dirty(
				layer_surface->output->layers.optimized_blur_node);
		}
	}

	// Don't iterate through this tree
	layer_surface->object.scene_tree->node.data = NULL;
	if (layer_surface->output && layer_surface->output->wlr_output) {
		comp_output_arrange_layers(layer_surface->output);
	}

	wl_list_remove(&layer_surface->map.link);
	wl_list_remove(&layer_surface->unmap.link);
	wl_list_remove(&layer_surface->commit.link);
	wl_list_remove(&layer_surface->output_destroy.link);
	wl_list_remove(&layer_surface->node_destroy.link);

	layer_surface->wlr_layer_surface->data = NULL;

	free(layer_surface);
}

/*
 * Layer popup logic
 */

static void handle_new_popup(struct wl_listener *listener, void *data) {
	struct comp_layer_surface *layer_surface =
		wl_container_of(listener, layer_surface, new_popup);
	struct wlr_xdg_popup *wlr_popup = data;
	// Use XDG popup to avoid duplicating code
	xdg_new_xdg_popup(wlr_popup, &layer_surface->object,
					  layer_surface->scene_layer->tree);
}

/*
 * Layer shell logic
 */

void layer_shell_new_surface(struct wl_listener *listener, void *data) {
	struct comp_server *server =
		wl_container_of(listener, server, new_layer_surface);
	struct wlr_layer_surface_v1 *wlr_layer_surface = data;

	struct comp_layer_surface *layer_surface =
		calloc(1, sizeof(*layer_surface));
	if (!layer_surface) {
		wlr_log(WLR_ERROR, "Could not allocate comp_layer_surface");
		return;
	}
	layer_surface->server = server;
	/* Set the scene_nodes decoration data */
	layer_surface->opacity = 1;
	layer_surface->corner_radius = EFFECTS_CORNER_RADII;
	layer_surface->shadow_data = shadow_data_get_default();

	layer_surface->wlr_layer_surface = wlr_layer_surface;
	wlr_layer_surface->data = layer_surface->object.scene_tree;

	/*
	 * Output
	 */

	if (!wlr_layer_surface->output) {
		// Assign last active output
		struct comp_output *output = get_active_output(server);
		if (!output) {
			goto error;
		} else if (output == server->fallback_output) {
			wlr_log(WLR_INFO,
					"no output to auto-assign layer surface '%s' to, using "
					"fallback_output",
					wlr_layer_surface->namespace);
		}
		wlr_layer_surface->output = output->wlr_output;
	}
	struct comp_output *output = wlr_layer_surface->output->data;
	layer_surface->output = output;

	struct wlr_scene_tree *layer =
		layer_get_scene_tree(output, wlr_layer_surface->pending.layer);
	layer_surface->object.scene_tree = alloc_tree(layer);
	layer_surface->object.content_tree =
		alloc_tree(layer_surface->object.scene_tree);
	if (layer_surface->object.scene_tree == NULL ||
		layer_surface->object.content_tree == NULL) {
		goto error;
	}

	/*
	 * Layer Surface
	 */

	layer_surface->scene_layer = wlr_scene_layer_surface_v1_create(
		layer_surface->object.content_tree, wlr_layer_surface);
	if (layer_surface->scene_layer == NULL) {
		wlr_log(WLR_ERROR, "Could not create wlr_scene_layer_surface");
		goto error;
	}
	layer_surface->scene_layer->tree->node.data = &layer_surface->object;

	// layer_surface->object.scene_tree = layer_surface->scene_layer->tree;
	layer_surface->object.scene_tree->node.data = &layer_surface->object;
	layer_surface->object.data = layer_surface;
	layer_surface->object.type = COMP_OBJECT_TYPE_LAYER_SURFACE;
	layer_surface->object.destroying = false;
	wlr_layer_surface->data = layer_surface->object.scene_tree;

	/*
	 * Events
	 */

	/* Listen to the various events it can emit */
	layer_surface->map.notify = layer_surface_map;
	wl_signal_add(&wlr_layer_surface->surface->events.map, &layer_surface->map);
	layer_surface->unmap.notify = layer_surface_unmap;
	wl_signal_add(&wlr_layer_surface->surface->events.unmap,
				  &layer_surface->unmap);
	layer_surface->commit.notify = layer_surface_commit;
	wl_signal_add(&wlr_layer_surface->surface->events.commit,
				  &layer_surface->commit);
	layer_surface->new_popup.notify = handle_new_popup;
	wl_signal_add(&wlr_layer_surface->events.new_popup,
				  &layer_surface->new_popup);

	layer_surface->output_destroy.notify = layer_output_destroy;
	wl_signal_add(&output->events.disable, &layer_surface->output_destroy);
	layer_surface->node_destroy.notify = layer_surface_node_destroy;
	wl_signal_add(&layer_surface->scene_layer->tree->node.events.destroy,
				  &layer_surface->node_destroy);
	return;

error:
	free(layer_surface);
	wlr_layer_surface_v1_destroy(wlr_layer_surface);
}
