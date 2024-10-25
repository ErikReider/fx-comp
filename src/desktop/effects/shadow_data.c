#include "desktop/effects/shadow_data.h"

struct shadow_data shadow_data_get_default(void) {
	return (struct shadow_data){
		.blur_sigma = 20,
		.color = {0.0f, 0.0f, 0.0f, 0.5f},
		.offset_x = 0.0f,
		.offset_y = 0.0f,
	};
}

void shadow_data_apply_to_shadow_node(struct wlr_scene_shadow *shadow_node,
									  struct shadow_data *shadow_data) {
}
