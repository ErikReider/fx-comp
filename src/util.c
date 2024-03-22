#define _XOPEN_SOURCE 600 // for M_PI

#include <cairo.h>
#include <math.h>
#include <scenefx/types/wlr_scene.h>
#include <stdlib.h>
#include <wayland-util.h>
#include <wlr/util/log.h>

#include "util.h"

/*
 * Generic
 */

int wrap(int i, int max) {
	return ((i % max) + max) % max;
}

/* wlroots */

static int scale_length(int length, int offset, float scale) {
	return round((offset + length) * scale) - round(offset * scale);
}

void scale_box(struct wlr_box *box, float scale) {
	box->width = scale_length(box->width, box->x, scale);
	box->height = scale_length(box->height, box->y, scale);
	box->x = round(box->x * scale);
	box->y = round(box->y * scale);
}

struct wlr_scene_tree *alloc_tree(struct wlr_scene_tree *parent) {
	struct wlr_scene_tree *tree = wlr_scene_tree_create(parent);
	if (tree == NULL) {
		wlr_log(WLR_ERROR, "Could not create scene_tree");
	}
	return tree;
}

double hex_red(const uint32_t *const col) {
	return ((const uint8_t *)(col))[3] / (double)(255);
}
double hex_green(const uint32_t *const col) {
	return ((const uint8_t *)(col))[2] / (double)(255);
}
double hex_blue(const uint32_t *const col) {
	return ((const uint8_t *)(col))[1] / (double)(255);
}
double hex_alpha(const uint32_t *const col) {
	return ((const uint8_t *)(col))[0] / (double)(255);
}

GdkRGBA gdk_rgba_from_color(const uint32_t *const c) {
	return (GdkRGBA){
		.red = hex_red(c),
		.green = hex_green(c),
		.blue = hex_blue(c),
		.alpha = hex_alpha(c),
	};
}

struct wlr_render_color wlr_render_color_from_color(const uint32_t *const c) {
	return (struct wlr_render_color){
		.r = hex_red(c),
		.g = hex_green(c),
		.b = hex_blue(c),
		.a = hex_alpha(c),
	};
}

/* cairo */

void cairo_set_rgba32(cairo_t *cr, const uint32_t *const c) {
	cairo_set_source_rgba(cr, hex_red(c), hex_green(c), hex_blue(c),
						  hex_alpha(c));
}

void cairo_draw_rounded_rect(cairo_t *cr, double width, double height, double x,
							 double y, double radius) {
	cairo_new_sub_path(cr);
	// Top right
	cairo_arc(cr, x + width - radius, y + radius, radius, -M_PI * 0.5,
			  M_PI / 180.0);
	// Bottom right
	cairo_arc(cr, x + width - radius, y + height - radius, radius, M_PI / 180.0,
			  M_PI * 0.5);
	// Bottom left
	cairo_arc(cr, x + radius, y + height - radius, radius, M_PI * 0.5, M_PI);
	// Top left
	cairo_arc(cr, x + radius, y + radius, radius, M_PI, M_PI * 1.5);
	cairo_close_path(cr);
}

void cairo_draw_icon_from_name(cairo_t *cr, const char *icon_name,
							   const uint32_t *const fg_color, int icon_size,
							   int x, int y, double scale) {
	GtkIconInfo *icon_info = gtk_icon_theme_lookup_icon_for_scale(
		gtk_icon_theme_get_default(), icon_name, icon_size, scale, 0);

	// Icon pixel buffer
	const GdkRGBA fg = gdk_rgba_from_color(fg_color);
	GdkPixbuf *icon_pixbuf = gtk_icon_info_load_symbolic(
		icon_info, &fg, NULL, NULL, NULL, NULL, NULL);
	cairo_surface_t *icon_surface =
		gdk_cairo_surface_create_from_pixbuf(icon_pixbuf, scale, NULL);

	// Render
	cairo_save(cr);

	cairo_set_source_surface(cr, icon_surface, x, y);
	cairo_paint(cr);

	cairo_restore(cr);

	cairo_surface_destroy(icon_surface);
	g_object_unref(icon_pixbuf);
}
