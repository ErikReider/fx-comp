#include <scenefx/types/wlr_scene.h>

#include "comp/object.h"

struct comp_object *comp_object_at(struct comp_server *server, double lx,
								   double ly, double *sx, double *sy,
								   struct wlr_scene_buffer **scene_buffer,
								   struct wlr_surface **surface) {
	/* This returns the topmost node in the scene at the given layout coords.
	 * We only care about surface nodes as we are specifically looking for a
	 * surface in the surface tree of a comp_toplevel. */
	struct wlr_scene_node *node =
		wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
	if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
		return NULL;
	}

	if (node->type == WLR_SCENE_NODE_BUFFER && scene_buffer) {
		*scene_buffer = wlr_scene_buffer_from_node(node);

		struct wlr_scene_surface *scene_surface =
			wlr_scene_surface_try_from_buffer(*scene_buffer);
		if (scene_surface) {
			*surface = scene_surface->surface;
		}
	}

	/* Find the node corresponding to the comp_toplevel at the root of this
	 * surface tree, it is the only one for which we set the data field. */
	struct wlr_scene_tree *tree = node->parent;
	while (tree != NULL && tree->node.data == NULL) {
		tree = tree->node.parent;
	}
	return tree->node.data;
}
