#ifndef FX_COMP_WORKSPACE_H
#define FX_COMP_WORKSPACE_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wayland-util.h>

#include "server.h"

enum comp_workspace_type {
	COMP_WORKSPACE_TYPE_REGULAR,
	COMP_WORKSPACE_TYPE_FULLSCREEN,
};

struct comp_workspace {
	struct wl_list output_link;

	enum comp_workspace_type type;

	struct comp_output *output;

	struct wlr_scene_tree *workspace_tree;
	struct {
		struct wlr_scene_tree *lower; // Used for tiled / fullscreen
		struct wlr_scene_tree *floating;
	} layers;

	// Toplevels and Popups. Also contains the focus order
	struct wl_list toplevels;
};

/*
 * Util
 */

int comp_workspace_find_index(struct wl_list *list, struct comp_workspace *ws);

void comp_workspace_move_toplevel_to(struct comp_workspace *dest_workspace,
									  struct comp_toplevel *toplevel);

/*
 * Main
 */

struct comp_workspace *comp_workspace_new(struct comp_output *output,
										  enum comp_workspace_type type);

/** WARNING: Doesn't reparent workspace */
void comp_workspace_destroy(struct comp_workspace *ws);

#endif // !FX_COMP_WORKSPACE_H
