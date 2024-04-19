#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <wlr/util/log.h>

#include "desktop/toplevel.h"
#include "desktop/xwayland.h"

/** Get the XWayland parent */
struct wlr_scene_tree *get_parent_tree(struct wlr_xwayland_surface *xsurface) {
	struct wlr_xwayland_surface *parent = xsurface->parent;
	if (!parent) {
		return NULL;
	}
	return parent->data;
}

/** Move into parent tree */
void move_into_parent_tree(struct comp_toplevel *toplevel,
						   struct wlr_scene_tree *parent) {
	if (!parent) {
		return;
	}

	wlr_scene_node_reparent(&toplevel->object.scene_tree->node, parent);
}
