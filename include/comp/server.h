#ifndef FX_COMP_SERVER_H
#define FX_COMP_SERVER_H

#include <stdbool.h>
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
	struct wl_list xdg_decorations;

	struct wlr_layer_shell_v1 *layer_shell;
	struct wl_listener new_layer_surface;

	struct wlr_pointer_constraints_v1 *pointer_constraints;
	struct wl_listener pointer_constraint;
	struct wlr_relative_pointer_manager_v1 *relative_pointer_manager;

	struct comp_seat *seat;

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
