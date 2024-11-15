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
#include <wlr/types/wlr_compositor.h>
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

static bool scene_node_snapshot(struct wlr_scene_node *node, int lx, int ly,
								struct wlr_scene_tree *snapshot_tree) {
	if (!node->enabled && node->type != WLR_SCENE_NODE_TREE) {
		return true;
	}

	lx += node->x;
	ly += node->y;

	struct wlr_scene_node *snapshot_node = NULL;
	switch (node->type) {
	case WLR_SCENE_NODE_TREE:;
		struct wlr_scene_tree *scene_tree = wlr_scene_tree_from_node(node);

		struct wlr_scene_node *child;
		wl_list_for_each(child, &scene_tree->children, link) {
			scene_node_snapshot(child, lx, ly, snapshot_tree);
		}
		break;
	case WLR_SCENE_NODE_RECT:;
		struct wlr_scene_rect *scene_rect = wlr_scene_rect_from_node(node);

		struct wlr_scene_rect *snapshot_rect =
			wlr_scene_rect_create(snapshot_tree, scene_rect->width,
								  scene_rect->height, scene_rect->color);
		snapshot_rect->node.data = scene_rect->node.data;
		if (snapshot_rect == NULL) {
			return false;
		}
		snapshot_node = &snapshot_rect->node;
		break;
	case WLR_SCENE_NODE_BUFFER:;
		struct wlr_scene_buffer *scene_buffer =
			wlr_scene_buffer_from_node(node);

		struct wlr_scene_buffer *snapshot_buffer =
			wlr_scene_buffer_create(snapshot_tree, NULL);
		if (snapshot_buffer == NULL) {
			return false;
		}
		snapshot_node = &snapshot_buffer->node;
		snapshot_buffer->node.data = scene_buffer->node.data;

		wlr_scene_buffer_set_dest_size(snapshot_buffer, scene_buffer->dst_width,
									   scene_buffer->dst_height);
		wlr_scene_buffer_set_opaque_region(snapshot_buffer,
										   &scene_buffer->opaque_region);
		wlr_scene_buffer_set_source_box(snapshot_buffer,
										&scene_buffer->src_box);
		wlr_scene_buffer_set_transform(snapshot_buffer,
									   scene_buffer->transform);
		wlr_scene_buffer_set_filter_mode(snapshot_buffer,
										 scene_buffer->filter_mode);

		// Effects
		wlr_scene_buffer_set_opacity(snapshot_buffer, scene_buffer->opacity);
		wlr_scene_buffer_set_corner_radius(snapshot_buffer,
										   scene_buffer->corner_radius,
										   scene_buffer->corners);
		wlr_scene_buffer_set_opacity(snapshot_buffer, scene_buffer->opacity);

		wlr_scene_buffer_set_backdrop_blur_optimized(
			snapshot_buffer, scene_buffer->backdrop_blur_optimized);
		wlr_scene_buffer_set_backdrop_blur_ignore_transparent(
			snapshot_buffer, scene_buffer->backdrop_blur_ignore_transparent);
		wlr_scene_buffer_set_backdrop_blur(snapshot_buffer,
										   scene_buffer->backdrop_blur);

		snapshot_buffer->node.data = scene_buffer->node.data;

		struct wlr_scene_surface *scene_surface =
			wlr_scene_surface_try_from_buffer(scene_buffer);
		if (scene_surface != NULL && scene_surface->surface->buffer != NULL) {
			wlr_scene_buffer_set_buffer(snapshot_buffer,
										&scene_surface->surface->buffer->base);
		} else {
			wlr_scene_buffer_set_buffer(snapshot_buffer, scene_buffer->buffer);
		}
		break;

	case WLR_SCENE_NODE_OPTIMIZED_BLUR:
		break;

	case WLR_SCENE_NODE_SHADOW:;
		struct wlr_scene_shadow *scene_shadow =
			wlr_scene_shadow_from_node(node);

		struct wlr_scene_shadow *snapshot_shadow = wlr_scene_shadow_create(
			snapshot_tree, scene_shadow->width, scene_shadow->height,
			scene_shadow->corner_radius, scene_shadow->blur_sigma,
			scene_shadow->color);
		if (snapshot_shadow == NULL) {
			return false;
		}
		snapshot_node = &snapshot_shadow->node;

		snapshot_shadow->node.data = scene_shadow->node.data;

		break;
	}

	if (snapshot_node != NULL) {
		wlr_scene_node_set_position(snapshot_node, lx, ly);
	}

	return true;
}

struct wlr_scene_tree *wlr_scene_tree_snapshot(struct wlr_scene_node *node,
											   struct wlr_scene_tree *parent) {
	struct wlr_scene_tree *snapshot = wlr_scene_tree_create(parent);
	if (snapshot == NULL) {
		return NULL;
	}

	// Disable and enable the snapshot tree like so to atomically update
	// the scene-graph. This will prevent over-damaging or other weirdness.
	wlr_scene_node_set_enabled(&snapshot->node, false);

	if (!scene_node_snapshot(node, 0, 0, snapshot)) {
		wlr_scene_node_destroy(&snapshot->node);
		return NULL;
	}

	wlr_scene_node_set_enabled(&snapshot->node, true);

	return snapshot;
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
