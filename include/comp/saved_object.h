#ifndef FX_COMP_SAVED_OBJECT_H
#define FX_COMP_SAVED_OBJECT_H

#include "comp/object.h"
#include "seat/cursor.h"

#define comp_saved_object_try_extract(object)                                  \
	if (object->type == COMP_OBJECT_TYPE_SAVED_OBJECT && object->data) {       \
		struct comp_saved_object *saved = object->data;                        \
		if (saved->saved_object) {                                             \
			object = saved->saved_object;                                      \
		}                                                                      \
	}

struct comp_saved_object {
	// Trees will be unused
	struct comp_object object;

	struct comp_object *saved_object;
};

struct comp_saved_object *
comp_saved_object_init(struct comp_object *save_object);

void comp_saved_object_destroy(struct comp_saved_object *saved_object);

#endif // !FX_COMP_SAVED_OBJECT_H
