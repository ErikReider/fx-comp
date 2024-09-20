#ifndef FX_COMP_SERVER_H
#define FX_COMP_SERVER_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/util/box.h>

#include "comp/animation_mgr.h"
#include "comp/xwayland_mgr.h"

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
	struct wl_event_loop *wl_event_loop;
	struct wlr_compositor *compositor;

	struct wlr_scene *root_scene;
	struct wlr_scene_output_layout *scene_layout;

	// XDG
	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_surface;
	struct wl_listener new_xdg_decoration;
	struct wl_list xdg_decorations;

	// Layer Shell
	struct wlr_layer_shell_v1 *layer_shell;
	struct wl_listener new_layer_surface;

	// XWayland
	struct comp_xwayland_mgr xwayland_mgr;
	struct wl_listener new_xwayland_surface;
	struct wl_listener xwayland_ready;

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

	struct comp_animation_mgr *animation_mgr;
	struct comp_transaction_mgr *transaction_mgr;
};

extern struct comp_server server;

void comp_server_layout_change(struct wl_listener *listener, void *data);
void comp_server_output_manager_apply(struct wl_listener *listener, void *data);
void comp_server_output_manager_test(struct wl_listener *listener, void *data);

struct comp_output *get_active_output(struct comp_server *server);

#endif // !FX_COMP_SERVER_H
