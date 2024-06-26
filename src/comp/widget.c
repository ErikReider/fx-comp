#include <assert.h>
#include <pixman.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wlr/util/log.h>

#include "comp/cairo_buffer.h"
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

bool comp_widget_init(struct comp_widget *widget, struct comp_server *server,
					  struct comp_object *parent_obj,
					  struct wlr_scene_tree *parent_tree,
					  const struct comp_widget_impl *impl) {
	assert(parent_obj);
	widget->object.scene_tree = alloc_tree(parent_tree);
	if (widget->object.scene_tree == NULL) {
		wlr_log(WLR_ERROR, "Failed to allocate comp_titlebar wlr_scene_tree");
		return false;
	}

	widget->scene_buffer =
		wlr_scene_buffer_create(widget->object.scene_tree, NULL);
	if (widget->scene_buffer == NULL) {
		wlr_log(WLR_ERROR, "Failed to allocate comp_titlebar wlr_scene_buffer");
		wlr_scene_node_destroy(&widget->object.scene_tree->node);
		return false;
	}

	widget->sets_cursor = false;

	widget->destroy.notify = widget_destroy;
	wl_signal_add(&widget->scene_buffer->node.events.destroy, &widget->destroy);

	widget->object.scene_tree->node.data = &widget->object;
	widget->object.type = COMP_OBJECT_TYPE_WIDGET;
	widget->object.data = widget;

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

	cairo_surface_t *surface = cairo_image_surface_create(
		CAIRO_FORMAT_ARGB32, scaled_width, scaled_height);
	if (surface == NULL) {
		wlr_log(WLR_ERROR, "Failed to create cairo image surface for titlebar");
		return;
	}
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		goto err_create_cairo;
	}

	cairo_t *cairo = cairo_create(surface);
	if (cairo == NULL) {
		goto err_create_cairo;
	}
	cairo_set_antialias(cairo, CAIRO_ANTIALIAS_BEST);

	cairo_font_options_t *font_options = cairo_font_options_create();
	if (!font_options) {
		goto err_create_font_options;
	}
	cairo_font_options_set_hint_style(font_options, CAIRO_HINT_STYLE_FULL);
	cairo_font_options_set_antialias(font_options, CAIRO_ANTIALIAS_GRAY);
	cairo_set_font_options(cairo, font_options);

	PangoContext *pango = pango_cairo_create_context(cairo);
	if (!pango) {
		goto err_create_pango;
	}

	struct cairo_buffer *buffer = calloc(1, sizeof(struct cairo_buffer));
	if (!buffer) {
		goto err;
	}

	// Draw
	widget->impl->draw(widget, cairo, scaled_width, scaled_height, scale);
	cairo_surface_flush(surface);

	// Finish post draw
	buffer->cairo = cairo;
	buffer->surface = surface;
	wlr_buffer_init(&buffer->base, &cairo_buffer_buffer_impl, scaled_width,
					scaled_height);

	wlr_scene_buffer_set_buffer_with_damage(widget->scene_buffer, &buffer->base,
											&widget->damage);
	wlr_buffer_drop(&buffer->base);

	goto clear_damage;

err:
	if (pango) {
		g_object_unref(pango);
	}
err_create_pango:
	cairo_font_options_destroy(font_options);
err_create_font_options:
	cairo_destroy(cairo);
err_create_cairo:
	cairo_surface_destroy(surface);

clear_damage:
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
