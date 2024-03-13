#ifndef FX_COMP_UTILR_H
#define FX_COMP_UTILR_H

#include <cairo.h>
#include <gtk-3.0/gtk/gtk.h>
#include <wlr/util/box.h>

/*
 * Generic
 */

int wrap(int i, int max);

/* wlroots */

void scale_box(struct wlr_box *box, float scale);

struct wlr_scene_tree *alloc_tree(struct wlr_scene_tree *parent);

/** Get red component from HEX color */
double hex_red(const uint32_t *const col);
/** Get green component from HEX color */
double hex_green(const uint32_t *const col);
/** Get blue component from HEX color */
double hex_blue(const uint32_t *const col);
/** Get alpha component from HEX color */
double hex_alpha(const uint32_t *const col);

/** Get GdkRGBA from HEX color */
GdkRGBA gdk_rgba_from_color(const uint32_t *const c);

/* cairo */

/** Convert HEX colors to decimal */
void cairo_set_rgba32(cairo_t *cr, const uint32_t *const c);

void cairo_draw_rounded_rect(cairo_t *cr, double width, double height, double x,
							 double y, double radius);

void cairo_draw_icon_from_name(cairo_t *cr, const char *icon_name,
							   const uint32_t *const fg_color, int icon_size,
							   int x, int y, double scale);

#endif // !FX_COMP_UTIL_H
