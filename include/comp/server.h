#ifndef FX_COMP_SERVER_H
#define FX_COMP_SERVER_H

#include <wayland-server-core.h>
#include <wlr/util/box.h>

/* For brevity's sake, struct members are annotated where they are used. */
enum comp_cursor_mode {
	COMP_CURSOR_PASSTHROUGH,
	COMP_CURSOR_MOVE,
	COMP_CURSOR_RESIZE,
};

struct comp_server {
	struct wl_display *wl_display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_scene *scene;

	struct wlr_scene_tree *layer_tree;
	struct wlr_scene_tree *xdg_tree;

	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_surface;
	struct wl_list views;

	struct wlr_layer_shell_v1 *layer_shell;
	struct wl_listener new_layer_surface;
	// struct wl_list views;

	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;

	struct wlr_seat *seat;
	struct wl_listener new_input;
	struct wl_listener request_cursor;
	struct wl_listener request_set_selection;
	struct wl_list keyboards;
	enum comp_cursor_mode cursor_mode;
	struct comp_view *grabbed_view;
	double grab_x, grab_y;
	struct wlr_box grab_geobox;
	uint32_t resize_edges;

	struct wlr_output_layout *output_layout;
	struct wl_list outputs;
	struct wl_listener new_output;
};

void comp_server_reset_cursor_mode(struct comp_server *server);

void comp_server_cursor_motion(struct wl_listener *listener, void *data);

void comp_server_cursor_motion_absolute(struct wl_listener *listener,
										void *data);

void comp_server_cursor_button(struct wl_listener *listener, void *data);

void comp_server_cursor_axis(struct wl_listener *listener, void *data);

void comp_server_cursor_frame(struct wl_listener *listener, void *data);

void comp_server_new_output(struct wl_listener *listener, void *data);

#endif // !FX_COMP_SERVER_H
