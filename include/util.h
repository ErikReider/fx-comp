#ifndef FX_COMP_UTILR_H
#define FX_COMP_UTILR_H

#include <cairo.h>
#include <wlr/util/box.h>

/*
 * Generic
 */

int wrap(int i, int max);

/* wlroots */

void scale_box(struct wlr_box *box, float scale);

struct wlr_scene_tree *alloc_tree(struct wlr_scene_tree *parent);

/* cairo */

/** Convert HEX colors to decimal */
void cairo_set_rgba32(cairo_t *cr, const uint32_t *const c);

void cairo_draw_rounded_rect(cairo_t *cr, double width, double height, double x,
							 double y, double radius);

#endif // !FX_COMP_UTIL_H
