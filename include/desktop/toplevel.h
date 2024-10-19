#ifndef FX_COMP_TOPLEVEL_H
#define FX_COMP_TOPLEVEL_H

#include <scenefx/types/fx/shadow_data.h>
#include <stdbool.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_compositor.h>

#include "comp/animation_mgr.h"
#include "comp/object.h"
#include "comp/server.h"
#include "comp/tiling_node.h"
#include "comp/transaction.h"
#include "seat/cursor.h"

#define NUMBER_OF_RESIZE_TARGETS 8
#define TOPLEVEL_MIN_WIDTH 75
#define TOPLEVEL_MIN_HEIGHT 50
#define TOPLEVEL_TILED_DRAG_SIZE 1.1

enum comp_tiling_mode {
	COMP_TILING_MODE_FLOATING, // Only floating
	COMP_TILING_MODE_TILED,	   // Tiled / Fullscreen
};

enum comp_toplevel_type {
	COMP_TOPLEVEL_TYPE_XDG,
	COMP_TOPLEVEL_TYPE_XWAYLAND,
};

// TODO: Make more generic for XWayland and others...
struct comp_toplevel {
	struct wl_list workspace_link;
	struct wl_list focus_link;

	struct comp_server *server;

	struct comp_object object;

	struct wlr_scene_tree *toplevel_scene_tree;
	struct wlr_scene_tree *decoration_scene_tree;
	// The saved buffer tree used for animations
	struct wlr_scene_tree *saved_scene_tree;

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

	// The current workspace
	struct comp_workspace *workspace;
	// The previous workspace where the non-fullscreen state resided.
	// Might be NULL
	struct comp_workspace *saved_workspace;
	struct tiling_node *tiling_node;
	enum comp_tiling_mode tiling_mode;
	bool dragging_tiled;
	bool fullscreen;
	pid_t pid;

	// The decorated size of the toplevel, if no decorations are visible, the
	// size will be the same as the state.
	struct {
		// Always state size + border width. Height includes titlebar height if
		// SSD are used
		int width, height;

		int top_border_height;
	} decorated_size;
	// Size when mapped
	int natural_width, natural_height;
	// Geometry
	struct wlr_box geometry;
	// The current state
	struct comp_toplevel_state state;
	// The pending state for the transaction
	struct comp_toplevel_state pending_state;
	// Used to restore the state when exiting fullscreen
	struct comp_toplevel_state saved_state;

	/**
	 * Whether the toplevel is mapped and visible (waiting for size change) or
	 * unmapped
	 */
	bool unmapped;

	struct {
		struct {
			struct comp_animation_client *client;
			float to;
			float from;
		} fade;
		struct {
			struct comp_animation_client *client;
			struct comp_toplevel_state to;
			struct comp_toplevel_state from;
			float crossfade_opacity;
		} resize;
	} anim;

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
	struct wlr_scene_tree *(*get_parent_tree)(struct comp_toplevel *toplevel);
	uint32_t (*configure)(struct comp_toplevel *toplevel, int width, int height,
						  int x, int y);
	void (*set_resizing)(struct comp_toplevel *toplevel, bool state);
	void (*set_activated)(struct comp_toplevel *toplevel, bool state);
	void (*set_fullscreen)(struct comp_toplevel *toplevel, bool state);
	bool (*get_is_fullscreen)(struct comp_toplevel *toplevel);
	void (*set_tiled)(struct comp_toplevel *toplevel, bool state);
	void (*set_pid)(struct comp_toplevel *toplevel);
	void (*marked_dirty_cb)(struct comp_toplevel *toplevel);
	void (*close)(struct comp_toplevel *toplevel);
	bool (*should_run_transaction)(struct comp_toplevel *toplevel);
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

/** Re-apply the effects to each child buffer */
void comp_toplevel_mark_effects_dirty(struct comp_toplevel *toplevel);
/**
 * Moves the toplevel into it's parent tree if it exists. Otherwise, move it
 * into the correct layer.
 */
void comp_toplevel_move_into_parent_tree(struct comp_toplevel *toplevel,
										 struct wlr_scene_tree *parent);

void comp_toplevel_center(struct comp_toplevel *toplevel, int width, int height,
						  bool center_on_cursor);

void comp_toplevel_save_buffer(struct comp_toplevel *toplevel);
void comp_toplevel_remove_buffer(struct comp_toplevel *toplevel);

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
struct wlr_scene_tree *
comp_toplevel_get_parent_tree(struct comp_toplevel *toplevel);
struct wlr_surface *
comp_toplevel_get_wlr_surface(struct comp_toplevel *toplevel);
uint32_t comp_toplevel_configure(struct comp_toplevel *toplevel, int width,
								 int height, int x, int y);
void comp_toplevel_set_activated(struct comp_toplevel *toplevel, bool state);
void comp_toplevel_set_fullscreen(struct comp_toplevel *toplevel, bool state);
void comp_toplevel_toggle_fullscreen(struct comp_toplevel *toplevel);
bool comp_toplevel_can_fullscreen(struct comp_toplevel *toplevel);
bool comp_toplevel_get_is_fullscreen(struct comp_toplevel *toplevel);
void comp_toplevel_toggle_tiled(struct comp_toplevel *toplevel);
void comp_toplevel_set_tiled(struct comp_toplevel *toplevel, bool state,
							 bool skip_remove_animation);
void comp_toplevel_set_pid(struct comp_toplevel *toplevel);
void comp_toplevel_set_size(struct comp_toplevel *toplevel, int width,
							int height);
void comp_toplevel_set_resizing(struct comp_toplevel *toplevel, bool state);

void comp_toplevel_refresh_titlebar(struct comp_toplevel *toplevel);

void comp_toplevel_send_frame_done(struct comp_toplevel *toplevel);

// void comp_toplevel_commit_transaction(struct comp_toplevel *toplevel,
// 									  bool run_now);

void comp_toplevel_set_position(struct comp_toplevel *toplevel, int x, int y);

void comp_toplevel_close(struct comp_toplevel *toplevel);

void comp_toplevel_destroy(struct comp_toplevel *toplevel);

void comp_toplevel_transaction_timed_out(struct comp_toplevel *toplevel);

void comp_toplevel_refresh(struct comp_toplevel *toplevel, bool is_instruction);

/*
 * Implementation generic functions
 */

void comp_toplevel_generic_map(struct comp_toplevel *toplevel);
void comp_toplevel_generic_unmap(struct comp_toplevel *toplevel);
void comp_toplevel_generic_commit(struct comp_toplevel *toplevel);

/*
 * Animation
 */

void comp_toplevel_add_fade_animation(struct comp_toplevel *toplevel,
									  float from, float to);

void comp_toplevel_add_size_animation(struct comp_toplevel *toplevel,
									  struct comp_toplevel_state from,
									  struct comp_toplevel_state to);

#endif // !FX_COMP_TOPLEVEL_H
