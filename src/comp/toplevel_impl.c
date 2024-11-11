#include <glib.h>

#include "comp/object.h"
#include "comp/tiling_node.h"
#include "comp/transaction.h"
#include "comp/workspace.h"
#include "desktop/toplevel.h"
#include "seat/cursor.h"

char *comp_toplevel_get_title(struct comp_toplevel *toplevel) {
	if (toplevel->object.destroying || toplevel->unmapped) {
		return NULL;
	}
	if (toplevel->impl && toplevel->impl->get_title) {
		return toplevel->impl->get_title(toplevel);
	}

	return NULL;
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
}

void comp_toplevel_toggle_fullscreen(struct comp_toplevel *toplevel) {
	comp_toplevel_set_fullscreen(toplevel, !toplevel->fullscreen);
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
