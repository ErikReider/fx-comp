#ifndef FX_COMP_DESKTOP_EFFECTS_SHADOW_DATA_H
#define FX_COMP_DESKTOP_EFFECTS_SHADOW_DATA_H

#include <scenefx/types/wlr_scene.h>

struct shadow_data {
	struct wlr_render_color color;
	float blur_sigma;
	float offset_x;
	float offset_y;
};

struct shadow_data shadow_data_get_default(void);

void shadow_data_apply_to_shadow_node(struct wlr_scene_shadow *shadow_node,
									  struct shadow_data *shadow_data);

bool shadow_data_should_update_color(struct wlr_scene_shadow *shadow_node,
									 struct shadow_data *shadow_data);

#endif // !FX_COMP_DESKTOP_EFFECTS_SHADOW_DATA_H
