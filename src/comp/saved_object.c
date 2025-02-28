#include <stdlib.h>
#include <wlr/util/log.h>

#include "comp/saved_object.h"

struct comp_saved_object *
comp_saved_object_init(struct comp_object *save_object) {
	struct comp_saved_object *saved = calloc(1, sizeof(*saved));
	if (!saved) {
		wlr_log(WLR_ERROR, "Could not allocate comp_saved_object");
		return NULL;
	}
	saved->object.scene_tree = NULL;
	saved->object.content_tree = NULL;

	// TODO:
	// saved->object.scene_tree->node.data = &saved->object;
	saved->object.data = saved;
	saved->object.type = COMP_OBJECT_TYPE_SAVED_OBJECT;
	saved->object.destroying = false;

	saved->saved_object = save_object;

	return saved;
}

void comp_saved_object_destroy(struct comp_saved_object *saved_object) {
	free(saved_object);
}
