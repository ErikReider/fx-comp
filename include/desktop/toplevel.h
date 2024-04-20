#ifndef FX_COMP_TOPLEVEL_H
#define FX_COMP_TOPLEVEL_H

#include <scenefx/types/fx/shadow_data.h>
#include <stdbool.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_compositor.h>

#include "comp/object.h"
#include "comp/server.h"
#include "seat/cursor.h"

#define NUMBER_OF_RESIZE_TARGETS 8

enum comp_tiling_mode {
	COMP_TILING_MODE_FLOATING, // Only floating
	COMP_TILING_MODE_TILED,	   // Tiled / Fullscreen
};

enum comp_toplevel_type {
	COMP_TOPLEVEL_TYPE_XDG,
	COMP_TOPLEVEL_TYPE_XWAYLAND,
};

struct comp_toplevel_state {
	int x;
	int y;
	int width;
	int height;
	struct comp_workspace *workspace;
};

// TODO: Make more generic for XWayland and others...
struct comp_toplevel {
	struct wl_list workspace_link;
	struct wl_list focus_link;

	struct comp_server *server;

	struct comp_object object;

	struct wlr_scene_tree *decoration_scene_tree;
	struct wlr_scene_tree *toplevel_scene_tree;

	// Type
	enum comp_toplevel_type type;
	union {
		struct comp_xdg_toplevel *toplevel_xdg;
		struct comp_xwayland_toplevel *toplevel_xway;
	};
	const struct comp_toplevel_impl *impl;

	// Borders
	struct comp_titlebar *titlebar;
	struct comp_resize_edge *edges[NUMBER_OF_RESIZE_TARGETS];
	bool using_csd;

	enum comp_tiling_mode tiling_mode;
	bool fullscreen;
	pid_t pid;

	// The decorated size of the toplevel, if no decorations are visible, the
	// size will be the same as the state.
	struct {
		int width;
		int height;

		int top_border_height;
	} decorated_size;
	// The current state
	struct comp_toplevel_state state;
	// Used to restore the state when exiting fullscreen
	struct comp_toplevel_state saved_state;

	// Effects
	float opacity;
	int corner_radius;
	struct shadow_data shadow_data;
};

struct comp_toplevel_impl {
	struct wlr_box (*get_geometry)(struct comp_toplevel *toplevel);
	void (*get_constraints)(struct comp_toplevel *toplevel, int *min_width,
							int *max_width, int *min_height, int *max_height);
	struct wlr_surface *(*get_wlr_surface)(struct comp_toplevel *toplevel);
	char *(*get_title)(struct comp_toplevel *toplevel);
	bool (*get_always_floating)(struct comp_toplevel *toplevel);
	void (*configure)(struct comp_toplevel *toplevel, int width, int height,
					  int x, int y);
	void (*set_size)(struct comp_toplevel *toplevel, int width, int height);
	void (*set_activated)(struct comp_toplevel *toplevel, bool state);
	void (*set_fullscreen)(struct comp_toplevel *toplevel, bool state);
	void (*set_tiled)(struct comp_toplevel *toplevel, bool state);
	void (*set_pid)(struct comp_toplevel *toplevel);
	void (*marked_dirty_cb)(struct comp_toplevel *toplevel);
	void (*close)(struct comp_toplevel *toplevel);
};

struct comp_toplevel *comp_toplevel_init(struct comp_output *output,
										 struct comp_workspace *workspace,
										 enum comp_toplevel_type type,
										 enum comp_tiling_mode tiling_mode,
										 bool fullscreen,
										 const struct comp_toplevel_impl *impl);

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

/** Set the effects for each scene_buffer */
void comp_toplevel_apply_effects(struct wlr_scene_tree *tree,
								 struct comp_toplevel *toplevel);

/*
 * Implementation functions
 */

struct wlr_box comp_toplevel_get_geometry(struct comp_toplevel *toplevel);
void comp_toplevel_get_constraints(struct comp_toplevel *toplevel,
								   int *min_width, int *max_width,
								   int *min_height, int *max_height);
char *comp_toplevel_get_title(struct comp_toplevel *toplevel);
/**
 * Checks if the toplevel always wants to be floating,
 * i.e don't allow tiling
 */
bool comp_toplevel_get_always_floating(struct comp_toplevel *toplevel);
struct wlr_surface *
comp_toplevel_get_wlr_surface(struct comp_toplevel *toplevel);
void comp_toplevel_configure(struct comp_toplevel *toplevel, int width,
							 int height, int x, int y);
void comp_toplevel_set_activated(struct comp_toplevel *toplevel, bool state);
void comp_toplevel_set_fullscreen(struct comp_toplevel *toplevel, bool state);
void comp_toplevel_toggle_fullscreen(struct comp_toplevel *toplevel);
void comp_toplevel_set_tiled(struct comp_toplevel *toplevel, bool state);
void comp_toplevel_set_pid(struct comp_toplevel *toplevel);
void comp_toplevel_set_size(struct comp_toplevel *toplevel, int width,
							int height);

void comp_toplevel_mark_dirty(struct comp_toplevel *toplevel);

void comp_toplevel_set_position(struct comp_toplevel *toplevel, int x, int y);

void comp_toplevel_close(struct comp_toplevel *toplevel);

void comp_toplevel_destroy(struct comp_toplevel *toplevel);

/*
 * Implementation generic functions
 */

void comp_toplevel_generic_map(struct comp_toplevel *toplevel);
void comp_toplevel_generic_unmap(struct comp_toplevel *toplevel);
void comp_toplevel_generic_commit(struct comp_toplevel *toplevel);

#endif // !FX_COMP_TOPLEVEL_H
