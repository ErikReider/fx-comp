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

bool shadow_data_should_update_color(struct wlr_scene_shadow *shadow_node,
									 struct shadow_data *shadow_data) {
	return shadow_data->color.r != shadow_node->color[0] ||
		   shadow_data->color.g != shadow_node->color[1] ||
		   shadow_data->color.b != shadow_node->color[2] ||
		   shadow_data->color.a != shadow_node->color[3];
}
