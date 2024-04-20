#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/util/log.h>

#include "comp/output.h"
#include "desktop/xwayland.h"
#include "seat/seat.h"
#include "util.h"

// Credits goes to Sway for most of the implementation :D

/**
 * Sets the position relative to the parent node.
 * XWayland unmanaged surfaces have their x and y coordinates relative to the
 * output, not the parent.
 */
static void unmanaged_set_position(struct comp_xwayland_unmanaged *unmanaged,
								   int16_t x, int16_t y) {
	struct wlr_scene_tree *parent_tree = unmanaged->parent_tree;

	wlr_scene_node_set_position(&unmanaged->surface_scene->buffer->node,
								x - parent_tree->node.x,
								y - parent_tree->node.y);
}

/*
 * XWayland Unmanaged
 */

static void unmanaged_set_geometry(struct wl_listener *listener, void *data) {
	struct comp_xwayland_unmanaged *unmanaged =
		wl_container_of(listener, unmanaged, set_geometry);
	struct wlr_xwayland_surface *xsurface = unmanaged->xwayland_surface;

	unmanaged_set_position(unmanaged, xsurface->x, xsurface->y);
}

static void unmanaged_request_activate(struct wl_listener *listener,
									   void *data) {
	struct comp_xwayland_unmanaged *unmanaged =
		wl_container_of(listener, unmanaged, request_activate);
	struct wlr_xwayland_surface *xsurface = unmanaged->xwayland_surface;
	if (xsurface->surface == NULL || !xsurface->surface->mapped) {
		return;
	}

	// Don't focus if PID of the focused surface isn't the same as the unmanaged
	// xsurface PID
	struct comp_toplevel *focused_toplevel = server.seat->focused_toplevel;
	if (focused_toplevel && focused_toplevel->pid != xsurface->pid) {
		return;
	}

	comp_seat_surface_focus(&unmanaged->object, xsurface->surface);
}

static void unmanaged_request_configure(struct wl_listener *listener,
										void *data) {
	struct comp_xwayland_unmanaged *unmanaged =
		wl_container_of(listener, unmanaged, request_configure);
	struct wlr_xwayland_surface *xsurface = unmanaged->xwayland_surface;
	struct wlr_xwayland_surface_configure_event *event = data;
	wlr_xwayland_surface_configure(xsurface, event->x, event->y, event->width,
								   event->height);
}

static void unmanaged_map(struct wl_listener *listener, void *data) {
	struct comp_xwayland_unmanaged *unmanaged =
		wl_container_of(listener, unmanaged, map);
	struct wlr_xwayland_surface *xsurface = unmanaged->xwayland_surface;

	unmanaged->parent_tree = get_parent_tree(xsurface);

	// Tries to attach to the parent
	if (!unmanaged->parent_tree) {
		struct comp_output *output = get_active_output(&server);
		struct comp_workspace *workspace = output->active_workspace;
		unmanaged->parent_tree = workspace->layers.unmanaged;
	}

	unmanaged->object.scene_tree = alloc_tree(unmanaged->parent_tree);
	unmanaged->object.scene_tree->node.data = &unmanaged->object;
	xsurface->data = unmanaged->object.scene_tree;

	unmanaged->surface_scene = wlr_scene_surface_create(
		unmanaged->object.scene_tree, xsurface->surface);
	if (unmanaged->surface_scene) {
		unmanaged->surface_scene->buffer->node.data = &unmanaged->object;

		unmanaged_set_position(unmanaged, xsurface->x, xsurface->y);

		listener_connect(&xsurface->events.set_geometry,
						 &unmanaged->set_geometry, unmanaged_set_geometry);
	}

	if (wlr_xwayland_or_surface_wants_focus(xsurface)) {
		struct wlr_xwayland *xwayland = server.xwayland_mgr.wlr_xwayland;
		wlr_xwayland_set_seat(xwayland, server.seat->wlr_seat);
		comp_seat_surface_focus(&unmanaged->object, xsurface->surface);
	}
}

static void unmanaged_unmap(struct wl_listener *listener, void *data) {
	struct comp_xwayland_unmanaged *unmanaged =
		wl_container_of(listener, unmanaged, unmap);
	struct wlr_xwayland_surface *xsurface = unmanaged->xwayland_surface;

	if (unmanaged->surface_scene) {
		listener_remove(&unmanaged->set_geometry);

		wlr_scene_node_destroy(&unmanaged->surface_scene->buffer->node);
		unmanaged->surface_scene = NULL;
	}

	struct comp_seat *seat = server.seat;
	if (seat->wlr_seat->keyboard_state.focused_surface == xsurface->surface) {
		// This simply returns focus to the parent surface if there's one
		// available. This seems to handle JetBrains issues.
		if (xsurface->parent && xsurface->parent->surface &&
			wlr_xwayland_or_surface_wants_focus(xsurface->parent)) {
			comp_seat_surface_focus(&unmanaged->object,
									xsurface->parent->surface);
			return;
		}

		// Restore focus
		comp_seat_surface_unfocus(xsurface->surface, true);
	}
}

static void unmanaged_associate(struct wl_listener *listener, void *data) {
	struct comp_xwayland_unmanaged *unmanaged =
		wl_container_of(listener, unmanaged, associate);
	struct wlr_xwayland_surface *xsurface = unmanaged->xwayland_surface;

	listener_connect(&xsurface->surface->events.map, &unmanaged->map,
					 unmanaged_map);
	listener_connect(&xsurface->surface->events.unmap, &unmanaged->unmap,
					 unmanaged_unmap);
}

static void unmanaged_dissociate(struct wl_listener *listener, void *data) {
	struct comp_xwayland_unmanaged *unmanaged =
		wl_container_of(listener, unmanaged, dissociate);

	listener_remove(&unmanaged->map);
	listener_remove(&unmanaged->unmap);
}

static void unmanaged_destroy(struct wl_listener *listener, void *data) {
	struct comp_xwayland_unmanaged *unmanaged =
		wl_container_of(listener, unmanaged, destroy);

	// Only destroy when the surface doesn't have a parent
	struct comp_output *output = get_active_output(&server);
	struct comp_workspace *workspace = output->active_workspace;
	if (unmanaged->parent_tree == workspace->layers.unmanaged) {
		wlr_scene_node_destroy(&unmanaged->object.scene_tree->node);
	}

	listener_remove(&unmanaged->request_configure);
	listener_remove(&unmanaged->associate);
	listener_remove(&unmanaged->dissociate);
	listener_remove(&unmanaged->destroy);
	listener_remove(&unmanaged->override_redirect);
	listener_remove(&unmanaged->request_activate);

	free(unmanaged);
}

static void unmanaged_override_redirect(struct wl_listener *listener,
										void *data) {
	struct comp_xwayland_unmanaged *unmanaged =
		wl_container_of(listener, unmanaged, override_redirect);
	struct wlr_xwayland_surface *xsurface = unmanaged->xwayland_surface;

	bool associated = xsurface->surface != NULL;
	bool mapped = associated && xsurface->surface->mapped;
	if (mapped) {
		unmanaged_unmap(&unmanaged->unmap, NULL);
	}
	if (associated) {
		unmanaged_dissociate(&unmanaged->dissociate, NULL);
	}

	unmanaged_destroy(&unmanaged->destroy, NULL);
	xsurface->data = NULL;

	struct comp_xwayland_toplevel *toplevel_xway =
		xway_create_toplevel(xsurface);
	if (associated) {
		listener_emit(&toplevel_xway->associate, NULL);
	}
	if (mapped) {
		listener_emit(&toplevel_xway->map, NULL);
	}
}

struct comp_xwayland_unmanaged *
xway_create_unmanaged(struct wlr_xwayland_surface *xsurface) {
	struct comp_xwayland_unmanaged *unmanaged = calloc(1, sizeof(*unmanaged));
	unmanaged->xwayland_surface = xsurface;
	unmanaged->parent_tree = get_parent_tree(xsurface);

	unmanaged->object.scene_tree = NULL;
	unmanaged->object.data = unmanaged;
	unmanaged->object.type = COMP_OBJECT_TYPE_UNMANAGED;

	/*
	 * Initialize listeners
	 */

	listener_init(&unmanaged->request_activate);
	listener_init(&unmanaged->request_configure);
	listener_init(&unmanaged->request_fullscreen);
	listener_init(&unmanaged->set_geometry);
	listener_init(&unmanaged->associate);
	listener_init(&unmanaged->dissociate);
	listener_init(&unmanaged->map);
	listener_init(&unmanaged->unmap);
	listener_init(&unmanaged->destroy);
	listener_init(&unmanaged->override_redirect);

	/*
	 * Events
	 */

	listener_connect(&xsurface->events.request_configure,
					 &unmanaged->request_configure,
					 unmanaged_request_configure);

	listener_connect(&xsurface->events.associate, &unmanaged->associate,
					 unmanaged_associate);

	listener_connect(&xsurface->events.dissociate, &unmanaged->dissociate,
					 unmanaged_dissociate);

	listener_connect(&xsurface->events.destroy, &unmanaged->destroy,
					 unmanaged_destroy);

	listener_connect(&xsurface->events.set_override_redirect,
					 &unmanaged->override_redirect,
					 unmanaged_override_redirect);

	listener_connect(&xsurface->events.request_activate,
					 &unmanaged->request_activate, unmanaged_request_activate);

	return unmanaged;
}
