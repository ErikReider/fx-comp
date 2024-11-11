#include <cairo/cairo.h>
#include <drm_fourcc.h>
#include <pango/pangocairo.h>
#include <stdlib.h>
#include <wlr/util/log.h>

#include "comp/cairo_buffer.h"

struct cairo_buffer *cairo_buffer_init(int width, int height) {
	cairo_surface_t *surface =
		cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
	if (surface == NULL) {
		wlr_log(WLR_ERROR, "Failed to create cairo image surface for widget");
		return NULL;
	}
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		goto err_create_cairo;
	}

	cairo_t *cairo = cairo_create(surface);
	if (cairo == NULL) {
		goto err_create_cairo;
	}
	cairo_set_antialias(cairo, CAIRO_ANTIALIAS_DEFAULT);

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

	buffer->cairo = cairo;
	buffer->surface = surface;

	wlr_buffer_init(&buffer->base, &cairo_buffer_buffer_impl, width, height);

	return buffer;

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

	return NULL;
}

static void cairo_buffer_handle_destroy(struct wlr_buffer *wlr_buffer) {
	struct cairo_buffer *buffer = wl_container_of(wlr_buffer, buffer, base);

	cairo_surface_destroy(buffer->surface);
	cairo_destroy(buffer->cairo);
	free(buffer);
}

static bool
cairo_buffer_handle_begin_data_ptr_access(struct wlr_buffer *wlr_buffer,
										  uint32_t flags, void **data,
										  uint32_t *format, size_t *stride) {
	struct cairo_buffer *buffer = wl_container_of(wlr_buffer, buffer, base);
	*data = cairo_image_surface_get_data(buffer->surface);
	*stride = cairo_image_surface_get_stride(buffer->surface);
	*format = DRM_FORMAT_ARGB8888;
	return true;
}

static void
cairo_buffer_handle_end_data_ptr_access(struct wlr_buffer *wlr_buffer) {
	// This space is intentionally left blank
}

const struct wlr_buffer_impl cairo_buffer_buffer_impl = {
	.destroy = cairo_buffer_handle_destroy,
	.begin_data_ptr_access = cairo_buffer_handle_begin_data_ptr_access,
	.end_data_ptr_access = cairo_buffer_handle_end_data_ptr_access,
};
