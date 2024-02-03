#include <assert.h>
#include <scenefx/types/fx/shadow_data.h>
#include <scenefx/types/wlr_scene.h>
#include <stdlib.h>
#include <wayland-util.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>

#include "comp/server.h"
#include "comp/toplevel.h"
#include "desktop/xdg.h"

static void iter_xdg_scene_buffers(struct wlr_scene_buffer *buffer, int sx,
								   int sy, void *user_data) {
	struct comp_toplevel *toplevel = user_data;

	struct wlr_scene_surface *scene_surface =
		wlr_scene_surface_try_from_buffer(buffer);
	if (!scene_surface) {
		return;
	}

	struct wlr_xdg_surface *xdg_surface =
		wlr_xdg_surface_try_from_wlr_surface(scene_surface->surface);
	if (toplevel && xdg_surface) {
		// TODO: Be able to set whole decoration_data instead of calling
		// each individually?
		wlr_scene_buffer_set_opacity(buffer, toplevel->opacity);

		wlr_scene_buffer_set_corner_radius(buffer, toplevel->corner_radius);
		wlr_scene_buffer_set_shadow_data(buffer, toplevel->shadow_data);

		wlr_scene_buffer_set_backdrop_blur(buffer, true);
		wlr_scene_buffer_set_backdrop_blur_optimized(buffer, false);
		wlr_scene_buffer_set_backdrop_blur_ignore_transparent(buffer, true);
	}
}

static void xdg_popup_map(struct wl_listener *listener, void *data) {
	/* Called when the surface is mapped, or ready to display on-screen. */
	struct comp_toplevel *toplevel = wl_container_of(listener, toplevel, map);

	wlr_scene_node_for_each_buffer(&toplevel->scene_tree->node,
								   iter_xdg_scene_buffers, toplevel);
}

static void xdg_popup_destroy(struct wl_listener *listener, void *data) {
	/* Called when the xdg_toplevel is destroyed. */
	struct comp_toplevel *toplevel = wl_container_of(listener, toplevel, destroy);

	wl_list_remove(&toplevel->map.link);
	wl_list_remove(&toplevel->destroy.link);

	free(toplevel);
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
	/* Called when the surface is mapped, or ready to display on-screen. */
	struct comp_toplevel *toplevel = wl_container_of(listener, toplevel, map);

	wlr_scene_node_for_each_buffer(&toplevel->scene_tree->node,
								   iter_xdg_scene_buffers, toplevel);

	wl_list_insert(&toplevel->server->toplevels, &toplevel->link);

	comp_toplevel_focus(toplevel, toplevel->xdg_toplevel->base->surface);
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
	/* Called when the surface is unmapped, and should no longer be shown. */
	struct comp_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);

	/* Reset the cursor mode if the grabbed toplevel was unmapped. */
	if (toplevel == toplevel->server->grabbed_toplevel) {
		comp_server_reset_cursor_mode(toplevel->server);
	}

	wl_list_remove(&toplevel->link);
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
	/* Called when the xdg_toplevel is destroyed. */
	struct comp_toplevel *toplevel =
		wl_container_of(listener, toplevel, destroy);

	wl_list_remove(&toplevel->map.link);
	wl_list_remove(&toplevel->unmap.link);
	wl_list_remove(&toplevel->destroy.link);
	wl_list_remove(&toplevel->request_move.link);
	wl_list_remove(&toplevel->request_resize.link);
	wl_list_remove(&toplevel->request_maximize.link);
	wl_list_remove(&toplevel->request_fullscreen.link);

	free(toplevel);
}

static void begin_interactive(struct comp_toplevel *toplevel,
							  enum comp_cursor_mode mode, uint32_t edges) {
	/* This function sets up an interactive move or resize operation, where the
	 * compositor stops propegating pointer events to clients and instead
	 * consumes them itself, to move or resize windows. */
	struct comp_server *server = toplevel->server;
	struct wlr_surface *focused_surface =
		server->seat->pointer_state.focused_surface;
	if (toplevel->xdg_toplevel->base->surface !=
		wlr_surface_get_root_surface(focused_surface)) {
		/* Deny move/resize requests from unfocused clients. */
		return;
	}
	server->grabbed_toplevel = toplevel;
	server->cursor_mode = mode;

	if (mode == COMP_CURSOR_MOVE) {
		server->grab_x = server->cursor->x - toplevel->scene_tree->node.x;
		server->grab_y = server->cursor->y - toplevel->scene_tree->node.y;
	} else {
		struct wlr_box geo_box;
		wlr_xdg_surface_get_geometry(toplevel->xdg_toplevel->base, &geo_box);

		double border_x = (toplevel->scene_tree->node.x + geo_box.x) +
						  ((edges & WLR_EDGE_RIGHT) ? geo_box.width : 0);
		double border_y = (toplevel->scene_tree->node.y + geo_box.y) +
						  ((edges & WLR_EDGE_BOTTOM) ? geo_box.height : 0);
		server->grab_x = server->cursor->x - border_x;
		server->grab_y = server->cursor->y - border_y;

		server->grab_geobox = geo_box;
		server->grab_geobox.x += toplevel->scene_tree->node.x;
		server->grab_geobox.y += toplevel->scene_tree->node.y;

		server->resize_edges = edges;
	}
}

static void xdg_toplevel_request_move(struct wl_listener *listener,
									  void *data) {
	/* This event is raised when a client would like to begin an interactive
	 * move, typically because the user clicked on their client-side
	 * decorations. Note that a more sophisticated compositor should check the
	 * provided serial against a list of button press serials sent to this
	 * client, to prevent the client from requesting this whenever they want. */
	struct comp_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_move);
	begin_interactive(toplevel, COMP_CURSOR_MOVE, 0);
}

static void xdg_toplevel_request_resize(struct wl_listener *listener,
										void *data) {
	/* This event is raised when a client would like to begin an interactive
	 * resize, typically because the user clicked on their client-side
	 * decorations. Note that a more sophisticated compositor should check the
	 * provided serial against a list of button press serials sent to this
	 * client, to prevent the client from requesting this whenever they want. */
	struct wlr_xdg_toplevel_resize_event *event = data;
	struct comp_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_resize);
	begin_interactive(toplevel, COMP_CURSOR_RESIZE, event->edges);
}

static void xdg_toplevel_request_maximize(struct wl_listener *listener,
										  void *data) {
	/* This event is raised when a client would like to maximize itself,
	 * typically because the user clicked on the maximize button on
	 * client-side decorations. tinywl doesn't support maximization, but
	 * to conform to xdg-shell protocol we still must send a configure.
	 * wlr_xdg_surface_schedule_configure() is used to send an empty reply. */
	struct comp_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_maximize);
	wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
}

static void xdg_toplevel_request_fullscreen(struct wl_listener *listener,
											void *data) {
	/* Just as with request_maximize, we must send a configure here. */
	struct comp_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_fullscreen);
	wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
}

void xdg_new_xdg_surface(struct wl_listener *listener, void *data) {
	/* This event is raised when wlr_xdg_shell receives a new xdg surface from a
	 * client, either a toplevel (application window) or popup. */
	struct comp_server *server =
		wl_container_of(listener, server, new_xdg_surface);
	struct wlr_xdg_surface *xdg_surface = data;

	if (xdg_surface->role == WLR_XDG_SURFACE_ROLE_NONE) {
		return;
	}

	/* Allocate a tinywl_toplevel for this surface */
	struct comp_toplevel *toplevel = calloc(1, sizeof(*toplevel));
	toplevel->server = server;
	/* Set the scene_nodes decoration data */
	toplevel->opacity = 1;
	toplevel->corner_radius = 20;
	toplevel->shadow_data = shadow_data_get_default();
	toplevel->shadow_data.enabled = true;
	toplevel->shadow_data.color =
		(struct wlr_render_color){0.0f, 1.0f, 0.0f, 1.0f};

	/* We must add xdg popups to the scene graph so they get rendered. The
	 * wlroots scene graph provides a helper for this, but to use it we must
	 * provide the proper parent scene node of the xdg popup. To enable this,
	 * we always set the user data field of xdg_surfaces to the corresponding
	 * scene node. */
	if (xdg_surface->role == WLR_XDG_SURFACE_ROLE_POPUP) {
		struct wlr_xdg_surface *parent =
			wlr_xdg_surface_try_from_wlr_surface(xdg_surface->popup->parent);
		assert(parent != NULL);
		struct wlr_scene_tree *parent_tree = parent->data;

		toplevel->xdg_popup = xdg_surface->popup;
		toplevel->scene_tree = wlr_scene_xdg_surface_create(
			parent_tree, toplevel->xdg_popup->base);
		toplevel->scene_tree->node.data = toplevel;
		xdg_surface->data = toplevel->scene_tree;

		/* Listen to the various events it can emit */
		toplevel->map.notify = xdg_popup_map;
		wl_signal_add(&xdg_surface->surface->events.map, &toplevel->map);
		toplevel->destroy.notify = xdg_popup_destroy;
		wl_signal_add(&xdg_surface->events.destroy, &toplevel->destroy);
		return;
	}

	/* Allocate a tinywl_toplevel for this surface */
	toplevel->xdg_toplevel = xdg_surface->toplevel;
	toplevel->scene_tree = wlr_scene_xdg_surface_create(
		&toplevel->server->scene->tree, toplevel->xdg_toplevel->base);
	toplevel->scene_tree->node.data = toplevel;
	xdg_surface->data = toplevel->scene_tree;

	/* Listen to the various events it can emit */
	toplevel->map.notify = xdg_toplevel_map;
	wl_signal_add(&xdg_surface->surface->events.map, &toplevel->map);
	toplevel->unmap.notify = xdg_toplevel_unmap;
	wl_signal_add(&xdg_surface->surface->events.unmap, &toplevel->unmap);
	toplevel->destroy.notify = xdg_toplevel_destroy;
	wl_signal_add(&xdg_surface->events.destroy, &toplevel->destroy);

	/* cotd */
	struct wlr_xdg_toplevel *xdg_toplevel = xdg_surface->toplevel;
	toplevel->request_move.notify = xdg_toplevel_request_move;
	wl_signal_add(&xdg_toplevel->events.request_move, &toplevel->request_move);
	toplevel->request_resize.notify = xdg_toplevel_request_resize;
	wl_signal_add(&xdg_toplevel->events.request_resize,
				  &toplevel->request_resize);
	toplevel->request_maximize.notify = xdg_toplevel_request_maximize;
	wl_signal_add(&xdg_toplevel->events.request_maximize,
				  &toplevel->request_maximize);
	toplevel->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
	wl_signal_add(&xdg_toplevel->events.request_fullscreen,
				  &toplevel->request_fullscreen);
}
