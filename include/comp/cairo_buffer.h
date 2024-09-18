#ifndef FX_COMP_CAIRO_BUFFER_H
#define FX_COMP_CAIRO_BUFFER_H

#include <cairo.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_buffer.h>

struct cairo_buffer {
	struct wlr_buffer base;
	cairo_surface_t *surface;
	cairo_t *cairo;
};

struct cairo_buffer *cairo_buffer_init(int width, int height);

extern const struct wlr_buffer_impl cairo_buffer_buffer_impl;

#endif // !FX_COMP_CAIRO_BUFFER_H
