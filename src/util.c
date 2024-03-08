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

/* cairo */

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
