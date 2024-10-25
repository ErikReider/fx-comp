#include <assert.h>
#include <pixman.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wlr/util/log.h>

#include "cairo.h"
#include "comp/cairo_buffer.h"
#include "comp/object.h"
#include "comp/output.h"
#include "comp/widget.h"
#include "seat/seat.h"
#include "util.h"

static void widget_destroy(struct wl_listener *listener, void *data) {
	struct comp_widget *widget = wl_container_of(listener, widget, destroy);

	wl_list_remove(&widget->destroy.link);

	pixman_region32_fini(&widget->damage);

	if (server.seat->hovered_widget == widget) {
		server.seat->hovered_widget = NULL;
	}

	if (widget->impl->destroy) {
		widget->impl->destroy(widget);
	}
}

static bool handle_point_accepts_input(struct wlr_scene_buffer *buffer,
									   double *x, double *y) {
	struct comp_object *object = buffer->node.data;
	assert(object && object->type == COMP_OBJECT_TYPE_WIDGET && object->data);
	struct comp_widget *widget = object->data;
	if (widget->impl->handle_point_accepts_input) {
		return widget->impl->handle_point_accepts_input(widget, buffer, x, y);
	}
	return true;
}

void comp_widget_refresh_shadow(struct comp_widget *widget) {
	struct shadow_data *shadow_data = &widget->shadow_data;

	wlr_scene_node_set_enabled(&widget->shadow_node->node, true);

	wlr_scene_shadow_set_corner_radius(widget->shadow_node,
									   widget->corner_radius);
	wlr_scene_shadow_set_blur_sigma(widget->shadow_node,
									shadow_data->blur_sigma);
	wlr_scene_shadow_set_color(
		widget->shadow_node,
		(float[4]){shadow_data->color.r, shadow_data->color.g,
				   shadow_data->color.b, shadow_data->color.a});

	wlr_scene_node_set_position(
		&widget->shadow_node->node,
		-shadow_data->blur_sigma + shadow_data->offset_x,
		-shadow_data->blur_sigma + shadow_data->offset_y);
	wlr_scene_shadow_set_size(widget->shadow_node,
							  widget->width + shadow_data->blur_sigma * 2,
							  widget->height + shadow_data->blur_sigma * 2);
}

bool comp_widget_init(struct comp_widget *widget, struct comp_server *server,
					  struct comp_object *parent_obj,
					  struct wlr_scene_tree *parent_tree,
					  struct shadow_data shadow_data,
					  const struct comp_widget_impl *impl) {
	assert(parent_obj);
	widget->object.scene_tree = alloc_tree(parent_tree);
	widget->object.content_tree = alloc_tree(widget->object.scene_tree);
	if (widget->object.scene_tree == NULL ||
		widget->object.content_tree == NULL) {
		wlr_log(WLR_ERROR, "Failed to allocate comp_titlebar wlr_scene_tree");
		return false;
	}

	// Shadow
	widget->shadow_node = wlr_scene_shadow_create(
		widget->object.content_tree, 0, 0, 0, shadow_data.blur_sigma,
		(float[4]){shadow_data.color.r, shadow_data.color.g,
				   shadow_data.color.b, shadow_data.color.a});
	widget->shadow_data = shadow_data;
	wlr_scene_node_set_enabled(&widget->shadow_node->node, false);

	widget->scene_buffer =
		wlr_scene_buffer_create(widget->object.content_tree, NULL);
	if (widget->scene_buffer == NULL) {
		wlr_log(WLR_ERROR, "Failed to allocate comp_titlebar wlr_scene_buffer");
		wlr_scene_node_destroy(&widget->object.scene_tree->node);
		return false;
	}
	widget->scene_buffer->node.data = &widget->object;

	widget->scene_buffer->point_accepts_input = handle_point_accepts_input;

	widget->sets_cursor = false;

	widget->destroy.notify = widget_destroy;
	wl_signal_add(&widget->scene_buffer->node.events.destroy, &widget->destroy);

	widget->object.scene_tree->node.data = &widget->object;
	widget->object.type = COMP_OBJECT_TYPE_WIDGET;
	widget->object.data = widget;
	widget->object.destroying = false;

	widget->parent_object = parent_obj;

	widget->impl = impl;

	pixman_region32_init(&widget->damage);

	return true;
}

void comp_widget_pointer_button(struct comp_widget *widget, double x, double y,
								struct wlr_pointer_button_event *event) {
	if (widget && widget->impl && widget->impl->handle_pointer_button) {
		// TODO: Get scale
		// float scale = widget->output->scale;
		float scale = 1.0f;
		double scale_x = scale * x;
		double scale_y = scale * y;

		widget->impl->handle_pointer_button(widget, scale_x, scale_y, event);
	}
}

void comp_widget_pointer_motion(struct comp_widget *widget, double x,
								double y) {
	if (widget && widget->impl && widget->impl->handle_pointer_motion) {
		// TODO: Get scale
		// float scale = widget->output->scale;
		float scale = 1.0f;
		double scale_x = scale * x;
		double scale_y = scale * y;

		widget->impl->handle_pointer_motion(widget, scale_x, scale_y);
	}
}

void comp_widget_pointer_enter(struct comp_widget *widget) {
	if (widget && widget->impl && widget->impl->handle_pointer_enter) {
		widget->impl->handle_pointer_enter(widget);
	}
}

void comp_widget_pointer_leave(struct comp_widget *widget) {
	if (widget && widget->impl && widget->impl->handle_pointer_leave) {
		widget->impl->handle_pointer_leave(widget);
	}
}

static void comp_widget_draw(struct comp_widget *widget, int width,
							 int height) {
	widget->width = width;
	widget->height = height;
	wlr_scene_buffer_set_dest_size(widget->scene_buffer, width, height);

	if (widget->impl->draw == NULL || width <= 0 || height <= 0) {
		return;
	}

	// TODO: Get scale from wlr_output
	float scale = 1.0f;
	int scaled_width = ceil(width * scale);
	int scaled_height = ceil(height * scale);

	// Only re-create the buffer if the size changes
	if (!widget->buffer || widget->buffer->base.width != scaled_width ||
		widget->buffer->base.height != scaled_height) {
		// Free the old buffer
		wlr_buffer_drop(&widget->buffer->base);

		widget->buffer = cairo_buffer_init(scaled_width, scaled_height);
	} else {
		// Clear the previous buffer
		cairo_save(widget->buffer->cairo);
		cairo_set_source_rgba(widget->buffer->cairo, 0.0, 0.0, 0.0, 0.0);
		cairo_set_operator(widget->buffer->cairo, CAIRO_OPERATOR_CLEAR);
		cairo_rectangle(widget->buffer->cairo, 0, 0, scaled_width,
						scaled_height);
		cairo_paint_with_alpha(widget->buffer->cairo, 1.0);
		cairo_restore(widget->buffer->cairo);
	}

	// Draw
	widget->impl->draw(widget, widget->buffer->cairo, scaled_width,
					   scaled_height, scale);

	wlr_scene_buffer_set_buffer_with_damage(
		widget->scene_buffer, &widget->buffer->base, &widget->damage);

	pixman_region32_clear(&widget->damage);
}

void comp_widget_draw_damaged(struct comp_widget *widget) {
	comp_widget_draw(widget, widget->width, widget->height);
}

void comp_widget_draw_full(struct comp_widget *widget) {
	pixman_region32_fini(&widget->damage);
	pixman_region32_init_rect(&widget->damage, 0, 0, widget->width,
							  widget->height);
	comp_widget_draw(widget, widget->width, widget->height);
}

void comp_widget_draw_resize(struct comp_widget *widget, int width,
							 int height) {
	if (widget->width != width || widget->height != height) {
		pixman_region32_fini(&widget->damage);
		pixman_region32_init_rect(&widget->damage, 0, 0, width, height);
	}
	comp_widget_draw(widget, width, height);
}

void comp_widget_damage_full(struct comp_widget *widget) {
	pixman_region32_fini(&widget->damage);
	pixman_region32_init_rect(&widget->damage, 0, 0, widget->width,
							  widget->height);
}

void comp_widget_center_on_output(struct comp_widget *widget,
								  struct comp_output *output) {
	// Override the default centering logic if needed
	if (widget && widget->impl && widget->impl->center &&
		widget->impl->center(widget)) {
		return;
	}

	struct wlr_scene_node *node = &widget->object.scene_tree->node;
	int width = (output->geometry.width - widget->width) / 2;
	int height = (output->geometry.height - widget->height) / 2;
	wlr_scene_node_set_position(node, width, height);
}
