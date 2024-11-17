#include <scenefx/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "comp/object.h"
#include "util.h"

struct comp_object *comp_object_at(struct comp_server *server, double lx,
								   double ly, double *sx, double *sy,
								   struct wlr_scene_buffer **scene_buffer,
								   struct wlr_surface **surface) {
	/* This returns the topmost node in the scene at the given layout coords.
	 * We only care about surface nodes as we are specifically looking for a
	 * surface in the surface tree of a comp_toplevel. */
	struct wlr_scene_node *node =
		wlr_scene_node_at(&server->root_scene->tree.node, lx, ly, sx, sy);
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
	struct wlr_scene_node *current = node;
	while (current != NULL && current->data == NULL) {
		current = &current->parent->node;
	}
	return current->data;
}

void comp_object_save_buffer(struct comp_object *object) {
	// Thanks Sway for the simple implementation! :)
	if (object->saved_tree) {
		wlr_log(WLR_INFO, "Trying to save already saved buffer...");
		comp_object_remove_buffer(object);
	}

	wlr_scene_node_set_enabled(&object->content_tree->node, true);
	object->saved_tree = wlr_scene_tree_snapshot(&object->content_tree->node,
												 object->scene_tree);

	wlr_scene_node_set_enabled(&object->content_tree->node, false);
	wlr_scene_node_set_enabled(&object->saved_tree->node, true);
}

void comp_object_remove_buffer(struct comp_object *object) {
	wlr_scene_node_destroy(&object->saved_tree->node);
	object->saved_tree = NULL;
	wlr_scene_node_set_enabled(&object->content_tree->node, true);
}

void comp_object_mark_dirty(struct comp_object *object) {
	if (object->dirty) {
		return;
	}
	object->dirty = true;
	wl_list_insert(&server.dirty_objects, &object->dirty_link);
}
