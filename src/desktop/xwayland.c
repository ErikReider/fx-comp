#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <wlr/util/log.h>

#include "desktop/xwayland.h"

/** Get the XWayland parent */
struct wlr_scene_tree *
xsurface_get_parent_tree(struct wlr_xwayland_surface *xsurface) {
	struct wlr_xwayland_surface *parent = xsurface->parent;
	if (!parent) {
		return NULL;
	}
	return parent->data;
}
