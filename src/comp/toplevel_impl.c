#include <glib.h>
#include <string.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/util/log.h>

#include "comp/object.h"
#include "comp/tiling_node.h"
#include "comp/transaction.h"
#include "comp/workspace.h"
#include "desktop/toplevel.h"
#include "seat/cursor.h"

char *comp_toplevel_get_foreign_id(struct comp_toplevel *toplevel) {
	if (toplevel->object.destroying) {
		return NULL;
	}
	if (toplevel->impl && toplevel->impl->get_foreign_id) {
		return toplevel->impl->get_foreign_id(toplevel);
	}

	return NULL;
}

char *comp_toplevel_get_class(struct comp_toplevel *toplevel) {
	if (toplevel->object.destroying || toplevel->unmapped) {
		return NULL;
	}
	if (toplevel->impl && toplevel->impl->get_class) {
		return toplevel->impl->get_class(toplevel);
	}

	return NULL;
}

char *comp_toplevel_get_app_id(struct comp_toplevel *toplevel) {
	if (toplevel->object.destroying || toplevel->unmapped) {
		return NULL;
	}
	if (toplevel->impl && toplevel->impl->get_app_id) {
		return toplevel->impl->get_app_id(toplevel);
	}

	return NULL;
}

char *comp_toplevel_get_title(struct comp_toplevel *toplevel) {
	if (!toplevel->object.destroying &&
		(toplevel->impl && toplevel->impl->get_title)) {
		char *title = toplevel->impl->get_title(toplevel);
		if (title) {
			strncpy(toplevel->title, title, sizeof(toplevel->title));
		} else {
			memset(toplevel->title, 0, sizeof(toplevel->title));
		}
	}

	return toplevel->title;
}

bool comp_toplevel_get_always_floating(struct comp_toplevel *toplevel) {
	if (toplevel->impl && toplevel->impl->get_always_floating) {
		return toplevel->impl->get_always_floating(toplevel);
	}

	return false;
}

struct wlr_scene_tree *
comp_toplevel_get_parent_tree(struct comp_toplevel *toplevel) {
	if (toplevel->impl && toplevel->impl->get_parent_tree) {
		return toplevel->impl->get_parent_tree(toplevel);
	}

	return NULL;
}

struct wlr_surface *
comp_toplevel_get_wlr_surface(struct comp_toplevel *toplevel) {
	if (toplevel->impl && toplevel->impl->get_wlr_surface) {
		return toplevel->impl->get_wlr_surface(toplevel);
	}

	return NULL;
}

struct comp_toplevel *
comp_toplevel_from_wlr_surface(struct wlr_surface *surface) {
	struct wlr_scene_tree *scene_tree = NULL;

	struct wlr_xdg_surface *xdg_surface;
	if ((xdg_surface = wlr_xdg_surface_try_from_wlr_surface(surface)) &&
		xdg_surface->toplevel && xdg_surface->data) {
		scene_tree = xdg_surface->data;
		goto done;
	}

	struct wlr_xwayland_surface *xsurface;
	if ((xsurface = wlr_xwayland_surface_try_from_wlr_surface(surface))) {
		scene_tree = xsurface->data;
		goto done;
	}

	struct wlr_subsurface *subsurface;
	if ((subsurface = wlr_subsurface_try_from_wlr_surface(surface))) {
		return comp_toplevel_from_wlr_surface(subsurface->parent);
	}

	const char *role = surface->role ? surface->role->name : NULL;
	wlr_log(WLR_DEBUG, "Trying to get Toplevel from surface (%p) with role: %s",
			surface, role);

done:
	if (!scene_tree) {
		return NULL;
	}

	struct comp_object *object = scene_tree->node.data;
	if (object && object->type == COMP_OBJECT_TYPE_TOPLEVEL && object->data) {
		return object->data;
	}

	return NULL;
}

struct wlr_box comp_toplevel_get_geometry(struct comp_toplevel *toplevel) {
	struct wlr_box box = {0};
	if (toplevel->impl && toplevel->impl->get_geometry) {
		box = toplevel->impl->get_geometry(toplevel);
	}

	return box;
}

void comp_toplevel_get_constraints(struct comp_toplevel *toplevel,
								   int *min_width, int *max_width,
								   int *min_height, int *max_height) {
	if (toplevel->impl && toplevel->impl->get_constraints) {
		toplevel->impl->get_constraints(toplevel, min_width, max_width,
										min_height, max_height);
	}
}

uint32_t comp_toplevel_configure(struct comp_toplevel *toplevel, int width,
								 int height, int x, int y) {
	if (toplevel->impl && toplevel->impl->configure) {
		return toplevel->impl->configure(toplevel, width, height, x, y);
	}
	return 0;
}

void comp_toplevel_set_activated(struct comp_toplevel *toplevel, bool state) {
	if (toplevel->impl && toplevel->impl->set_activated) {
		toplevel->impl->set_activated(toplevel, state);
	}

	if (toplevel->wlr_foreign_toplevel) {
		wlr_foreign_toplevel_handle_v1_set_activated(
			toplevel->wlr_foreign_toplevel, state);
	}
}

void comp_toplevel_toggle_minimized(struct comp_toplevel *toplevel) {
	comp_toplevel_set_minimized(toplevel, !toplevel->minimized);
}

void comp_toplevel_toggle_fullscreen(struct comp_toplevel *toplevel) {
	comp_toplevel_set_fullscreen(toplevel, !toplevel->fullscreen, false);
}

bool comp_toplevel_can_fullscreen(struct comp_toplevel *toplevel) {
	// Don't allow resizing fixed sized toplevels
	int max_width, max_height, min_width, min_height;
	comp_toplevel_get_constraints(toplevel, &min_width, &max_width, &min_height,
								  &max_height);
	if (min_width != 0 && min_height != 0 &&
		(min_width == max_width || min_height == max_height)) {
		return false;
	}

	return true;
}

bool comp_toplevel_get_is_fullscreen(struct comp_toplevel *toplevel) {
	if (toplevel->impl && toplevel->impl->get_is_fullscreen) {
		return toplevel->impl->get_is_fullscreen(toplevel);
	}

	return false;
}

void comp_toplevel_toggle_tiled(struct comp_toplevel *toplevel) {
	comp_toplevel_set_tiled(
		toplevel, toplevel->tiling_mode == COMP_TILING_MODE_FLOATING, false);
	// NOTE: Let the resize animation commit the transaction
}

void comp_toplevel_set_pid(struct comp_toplevel *toplevel) {
	if (toplevel->impl && toplevel->impl->set_pid) {
		toplevel->impl->set_pid(toplevel);
	}
}

void comp_toplevel_set_size(struct comp_toplevel *toplevel, int width,
							int height) {
	// Fixes the size sometimes being negative when resizing tiled toplevels
	toplevel->pending_state.width = MAX(0, width);
	toplevel->pending_state.height = MAX(0, height);
}

void comp_toplevel_set_position(struct comp_toplevel *toplevel, int x, int y) {
	toplevel->pending_state.x = x;
	toplevel->pending_state.y = y;
}

void comp_toplevel_set_resizing(struct comp_toplevel *toplevel, bool state) {
	if (toplevel && toplevel->impl && toplevel->impl->set_resizing) {
		toplevel->impl->set_resizing(toplevel, state);
	}
}

void comp_toplevel_close(struct comp_toplevel *toplevel) {
	if (toplevel->impl && toplevel->impl->close) {
		toplevel->impl->close(toplevel);
	}
}

void comp_toplevel_refresh_ext_foreign_toplevel(
	struct comp_toplevel *toplevel) {
	if (!toplevel->ext_foreign_toplevel) {
		return;
	}

	struct wlr_ext_foreign_toplevel_handle_v1_state toplevel_state = {
		.app_id = comp_toplevel_get_foreign_id(toplevel),
		.title = comp_toplevel_get_title(toplevel),
	};
	wlr_ext_foreign_toplevel_handle_v1_update_state(
		toplevel->ext_foreign_toplevel, &toplevel_state);
}
