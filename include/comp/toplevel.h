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

#define TOPLEVEL_NUM_BORDERS 4

enum comp_tiling_mode {
	COMP_TILING_MODE_FLOATING, // Only floating
	COMP_TILING_MODE_TILED, // Tiled / Fullscreen
};

struct comp_toplevel {
	struct wl_list link;

	struct comp_server *server;
	struct comp_output *output;

	struct wlr_xdg_toplevel *xdg_toplevel;
	struct wlr_xdg_popup *xdg_popup;

	struct wlr_scene_tree *xdg_scene_tree;

	// Borders
	struct comp_titlebar *titlebar;
	struct comp_edge *edge;
	struct comp_xdg_decoration *xdg_decoration;
	bool using_csd;

	// Signals
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;

	enum comp_tiling_mode tiling_mode;
	int initial_width;
	int initial_height;
	bool focused;
	bool fullscreen;

	struct comp_object object;

	// Effects
	float opacity;
	int corner_radius;
	struct shadow_data shadow_data;
};

void comp_toplevel_focus(struct comp_toplevel *view,
						 struct wlr_surface *surface);

void comp_toplevel_begin_interactive(struct comp_toplevel *toplevel,
									 enum comp_cursor_mode mode,
									 uint32_t edges);

#endif // !FX_COMP_TOPLEVEL_H
