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
	struct wlr_backend *headless_backend; // used for creating virtual outputs
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;

	struct wlr_scene *root_scene;
	struct wlr_scene_output_layout *scene_layout;

	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_surface;
	struct wl_listener new_xdg_decoration;
	struct wl_list toplevels;
	struct wl_list xdg_decorations;

	struct comp_cursor *cursor;

	struct wlr_pointer_constraints_v1 *pointer_constraints;
	struct wl_listener pointer_constraint;
	struct wlr_relative_pointer_manager_v1 *relative_pointer_manager;

	struct wlr_seat *seat;
	struct wl_listener new_input;
	struct wl_listener request_cursor;
	struct wl_listener request_set_selection;
	struct wl_list keyboards;
	struct comp_widget *hovered_widget;
	struct comp_toplevel *grabbed_toplevel;
	double grab_x, grab_y;
	struct wlr_box grab_geobox;
	uint32_t resize_edges;

	struct wlr_output_manager_v1 *output_manager;
	struct wl_listener output_manager_apply;
	struct wl_listener output_manager_test;

	struct wlr_output_layout *output_layout;
	struct wl_list outputs;
	struct comp_output *active_output;
	struct comp_output *fallback_output;
	struct wl_listener new_output;
	struct wl_listener layout_change;
};

extern struct comp_server server;


void comp_server_layout_change(struct wl_listener *listener, void *data);
void comp_server_output_manager_apply(struct wl_listener *listener, void *data);
void comp_server_output_manager_test(struct wl_listener *listener, void *data);

struct comp_output *get_active_output(struct comp_server *server);

#endif // !FX_COMP_SERVER_H
