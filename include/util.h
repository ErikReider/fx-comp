#ifndef FX_COMP_UTILR_H
#define FX_COMP_UTILR_H

#include <cairo.h>
#include <wlr/util/box.h>

void scale_box(struct wlr_box *box, float scale);

void cairo_draw_rounded_rect(cairo_t *cr, double width, double height, double x,
							 double y, double radius);

#endif // !FX_COMP_UTIL_H
