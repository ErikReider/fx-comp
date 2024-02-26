#include <stdbool.h>
#include <stdlib.h>

#include "comp/cairo_buffer.h"
#include "comp/widget.h"
#include "pango/pangocairo.h"
#include "wlr/util/log.h"

static void widget_destroy(struct wl_listener *listener, void *data) {
	struct comp_widget *widget = wl_container_of(listener, widget, destroy);

	wl_list_remove(&widget->destroy.link);

	if (server.hovered_widget == widget) {
		server.hovered_widget = NULL;
	}

	if (widget->impl->destroy) {
		widget->impl->destroy(widget);
	}
}

bool comp_widget_init(struct comp_widget *widget, struct comp_server *server,
					  struct comp_object *obj,
					  const struct comp_widget_impl *impl) {
	widget->object.scene_tree = wlr_scene_tree_create(obj->scene_tree);
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

	widget->impl = impl;

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

void comp_widget_draw_resize(struct comp_widget *widget, int width,
							 int height) {
	widget->object.width = width;
	widget->object.height = height;
	wlr_scene_buffer_set_dest_size(widget->scene_buffer, width, height);

	if (widget->impl->draw == NULL) {
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

	wlr_scene_buffer_set_buffer(widget->scene_buffer, &buffer->base);
	wlr_buffer_drop(&buffer->base);

	return;

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
	return;
}

void comp_widget_draw(struct comp_widget *widget) {
	comp_widget_draw_resize(widget, widget->object.width,
							widget->object.height);
}
