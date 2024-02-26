#include <assert.h>
#include <scenefx/types/wlr_scene.h>
#include <stdio.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_xdg_shell.h>

#include "comp/border/titlebar.h"
#include "comp/server.h"
#include "comp/toplevel.h"
#include "comp/widget.h"

static void comp_wlr_surface_unfocus(struct wlr_surface *surface) {
	struct wlr_xdg_surface *xdg_surface =
		wlr_xdg_surface_try_from_wlr_surface(surface);
	assert(xdg_surface && xdg_surface->toplevel);

	wlr_xdg_toplevel_set_activated(xdg_surface->toplevel, false);

	struct wlr_scene_tree *scene_tree = xdg_surface->data;
	struct comp_toplevel *toplevel = scene_tree->node.data;
	if (toplevel) {
		toplevel->focused = false;

		/*
		 * Redraw
		 */

		comp_widget_draw(&toplevel->titlebar->widget);
	}
}

void comp_toplevel_focus(struct comp_toplevel *toplevel,
						 struct wlr_surface *surface) {
	/* Note: this function only deals with keyboard focus. */
	if (toplevel == NULL) {
		return;
	}
	struct comp_server *server = toplevel->server;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
	if (prev_surface == surface) {
		/* Don't re-focus an already focused surface. */
		return;
	}
	if (prev_surface) {
		/*
		 * Deactivate the previously focused surface. This lets the client know
		 * it no longer has focus and the client will repaint accordingly, e.g.
		 * stop displaying a caret.
		 */
		comp_wlr_surface_unfocus(prev_surface);
	}
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	toplevel->focused = true;
	/* Move the toplevel to the front */
	wlr_scene_node_raise_to_top(&toplevel->object.scene_tree->node);
	wl_list_remove(&toplevel->link);
	wl_list_insert(&server->toplevels, &toplevel->link);
	/* Activate the new surface */
	wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);
	/*
	 * Tell the seat to have the keyboard enter this surface. wlroots will keep
	 * track of this and automatically send key events to the appropriate
	 * clients without additional work on your part.
	 */
	if (keyboard != NULL) {
		wlr_seat_keyboard_notify_enter(
			seat, toplevel->xdg_toplevel->base->surface, keyboard->keycodes,
			keyboard->num_keycodes, &keyboard->modifiers);
	}

	/*
	 * Redraw
	 */

	comp_widget_draw(&toplevel->titlebar->widget);
}
