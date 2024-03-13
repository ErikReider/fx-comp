#ifndef FX_COMP_TOPLEVEL_H
#define FX_COMP_TOPLEVEL_H

#include <scenefx/types/fx/shadow_data.h>
#include <stdbool.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_compositor.h>

#include "comp/object.h"
#include "comp/server.h"
#include "desktop/xdg_decoration.h"
#include "seat/cursor.h"

#define NUMBER_OF_RESIZE_TARGETS 8

enum comp_tiling_mode {
	COMP_TILING_MODE_FLOATING, // Only floating
	COMP_TILING_MODE_TILED,	   // Tiled / Fullscreen
};

struct comp_toplevel {
	struct wl_list workspace_link;
	struct wl_list focus_link;

	struct comp_server *server;
	struct comp_workspace *workspace;

	struct wlr_xdg_toplevel *xdg_toplevel;

	struct wlr_scene_tree *xdg_scene_tree;

	// Borders
	struct comp_titlebar *titlebar;
	struct comp_resize_edge *edges[NUMBER_OF_RESIZE_TARGETS];
	struct comp_xdg_decoration *xdg_decoration;
	bool using_csd;

	// Signals
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;

	struct wl_listener new_popup;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	struct wl_listener set_title;

	enum comp_tiling_mode tiling_mode;
	int initial_width;
	int initial_height;
	bool fullscreen;

	struct comp_object object;

	// Effects
	float opacity;
	int corner_radius;
	struct shadow_data shadow_data;
};

void comp_toplevel_process_cursor_move(struct comp_server *server,
									   uint32_t time);

void comp_toplevel_process_cursor_resize(struct comp_server *server,
										 uint32_t time);

uint32_t
comp_toplevel_get_edge_from_cursor_coords(struct comp_toplevel *toplevel,
										  struct comp_cursor *cursor);

void comp_toplevel_begin_interactive(struct comp_toplevel *toplevel,
									 enum comp_cursor_mode mode,
									 uint32_t edges);

struct wlr_scene_tree *comp_toplevel_get_layer(struct comp_toplevel *toplevel);

#endif // !FX_COMP_TOPLEVEL_H
