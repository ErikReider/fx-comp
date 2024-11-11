#ifndef FX_COMP_WIDGET_H
#define FX_COMP_WIDGET_H

#include <cairo.h>
#include <scenefx/types/wlr_scene.h>
#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_subcompositor.h>

#include "comp/object.h"
#include "comp/server.h"
#include "desktop/effects/shadow_data.h"

struct comp_widget;

struct comp_widget_click_region {
	struct wlr_box region;
	bool cursor_hovering;

	void (*handle_click)(struct comp_widget *widget,
						 struct comp_widget_click_region *region);

	void *data;
};

struct comp_widget {
	struct wlr_scene_shadow *shadow_node;
	struct wlr_scene_buffer *scene_buffer;

	struct comp_object object;
	struct comp_object *parent_object;

	int width;
	int height;

	bool sets_cursor; // If the widget sets it's own cursor or not

	// Signals
	struct wl_listener destroy;

	const struct comp_widget_impl *impl;

	struct cairo_buffer *buffer;
	pixman_region32_t damage;

	// Effects
	float opacity;
	int corner_radius;
	struct shadow_data shadow_data;
};

struct comp_widget_impl {
	void (*draw)(struct comp_widget *widget, cairo_t *cairo, int surface_width,
				 int surface_height, float scale);
	void (*handle_pointer_motion)(struct comp_widget *widget, double x,
								  double y);
	void (*handle_pointer_enter)(struct comp_widget *widget);
	void (*handle_pointer_leave)(struct comp_widget *widget);
	void (*handle_pointer_button)(struct comp_widget *widget, double x,
								  double y,
								  struct wlr_pointer_button_event *event);
	bool (*handle_point_accepts_input)(struct comp_widget *widget,
									   struct wlr_scene_buffer *buffer,
									   double *x, double *y);
	void (*destroy)(struct comp_widget *widget);
	// Return true to override the default centering logic
	bool (*center)(struct comp_widget *widget);
};

bool comp_widget_init(struct comp_widget *widget, struct comp_server *server,
					  struct comp_object *parent_obj,
					  struct wlr_scene_tree *parent_tree,
					  struct shadow_data shadow_data,
					  const struct comp_widget_impl *impl);

void comp_widget_pointer_button(struct comp_widget *widget, double x, double y,
								struct wlr_pointer_button_event *event);
void comp_widget_pointer_motion(struct comp_widget *widget, double x, double y);
void comp_widget_pointer_enter(struct comp_widget *widget);
void comp_widget_pointer_leave(struct comp_widget *widget);
void comp_widget_draw_resize(struct comp_widget *widget, int width, int height);
/** Only draws the damaged areas */
void comp_widget_draw_damaged(struct comp_widget *widget);
void comp_widget_damage_full(struct comp_widget *widget);
/** Redraws the full widget (fully damaged) */
void comp_widget_draw_full(struct comp_widget *widget);

void comp_widget_center_on_output(struct comp_widget *widget,
								  struct comp_output *output);

void comp_widget_refresh_shadow(struct comp_widget *widget);

#endif // !FX_COMP_WIDGET_H
