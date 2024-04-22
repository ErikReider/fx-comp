#define _XOPEN_SOURCE 600 // for M_PI

#include <assert.h>
#include <cairo.h>
#include <math.h>
#include <scenefx/types/wlr_scene.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-util.h>
#include <wlr/util/log.h>

#include "util.h"

/*
 * Generic
 */

int wrap(int i, int max) {
	return ((i % max) + max) % max;
}

void exec(char *cmd) {
	int fd[2];
	if (pipe(fd) != 0) {
		wlr_log(WLR_ERROR, "Unable to create pipe for fork");
	}

	pid_t child = fork(), grand_child;
	if (child < 0) {
		close(fd[0]);
		close(fd[1]);
		wlr_log(WLR_ERROR, "fork() failed");
		return;
	} else if (child == 0) {
		// Fork child process again
		setsid();

		sigset_t set;
		sigemptyset(&set);
		sigprocmask(SIG_SETMASK, &set, NULL);

		signal(SIGPIPE, SIG_DFL);
		close(fd[0]);

		grand_child = fork();
		if (grand_child == 0) {
			close(fd[1]);
			execlp("sh", "sh", "-c", cmd, NULL);
			wlr_log(WLR_ERROR, "execlp failed");
			_exit(1);
		}

		write(fd[1], &grand_child, sizeof(grand_child));
		close(fd[1]);
		// Exit the child process
		_exit(0);
	}

	close(fd[1]); // close write
	read(fd[0], &grand_child, sizeof(grand_child));
	close(fd[0]);
	// cleanup child process
	waitpid(child, NULL, 0);
}

/* Wayland Helpers */

static bool listener_is_connected(struct wl_listener *listener) {
	return !wl_list_empty(&listener->link);
}

void listener_init(struct wl_listener *listener) {
	assert(listener);
	wl_list_init(&listener->link);
}

void listener_connect(struct wl_signal *signal, struct wl_listener *listener,
					  wl_notify_func_t notify) {
	assert(listener);
	if (listener_is_connected(listener)) {
		wlr_log(WLR_INFO, "Cannot connect to a listener twice");
		return;
	}

	listener->notify = notify;
	wl_signal_add(signal, listener);
}

void listener_remove(struct wl_listener *listener) {
	assert(listener);
	if (listener_is_connected(listener)) {
		wl_list_remove(&listener->link);
		listener->notify = NULL;
		// Restore state
		listener_init(listener);
	}
}

void listener_emit(struct wl_listener *listener, void *data) {
	if (listener && listener->notify) {
		listener->notify(listener, data);
	}
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

/* Animation Helpers */

double lerp(double a, double b, double t) {
	return a * (1.0 - t) + b * t;
}

double ease_in_cubic(double t) {
	return t * t * t;
}

double ease_out_cubic(double t) {
	double p = t - 1;
	return pow(p, 3) + 1;
}

double ease_in_out_cubic(double t) {
	double p = t * 2;

	if (p < 1) {
		return 0.5 * p * p * p;
	}

	p -= 2;

	return 0.5 * (p * p * p + 2);
}
