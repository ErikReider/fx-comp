#ifndef FX_COMP_UTILR_H
#define FX_COMP_UTILR_H

#include <cairo.h>
#include <gtk-3.0/gtk/gtk.h>
#include <wlr/util/box.h>

/*
 * Generic
 */

int wrap(int i, int max);

void exec(char *cmd);

/* Wayland Helpers */

void listener_init(struct wl_listener *listener);
void listener_connect(struct wl_signal *signal, struct wl_listener *listener,
					  wl_notify_func_t notify);
void listener_remove(struct wl_listener *listener);
void listener_emit(struct wl_listener *listener, void *data);

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

/** Get `GdkRGBA` from HEX color */
GdkRGBA gdk_rgba_from_color(const uint32_t *const c);
/** Get `struct wlr_render_color` from HEX color */
struct wlr_render_color wlr_render_color_from_color(const uint32_t *const c);

/* cairo */

/** Convert HEX colors to decimal */
void cairo_set_rgba32(cairo_t *cr, const uint32_t *const c);

void cairo_draw_rounded_rect(cairo_t *cr, double width, double height, double x,
							 double y, double radius);

void cairo_draw_icon_from_name(cairo_t *cr, const char *icon_name,
							   const uint32_t *const fg_color, int icon_size,
							   int x, int y, double scale);

/* Animation helpers */

double lerp(double a, double b, double t);

double ease_in_cubic(double t);

double ease_out_cubic(double t);

double ease_in_out_cubic(double t);

#endif // !FX_COMP_UTIL_H
