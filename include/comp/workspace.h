#ifndef FX_COMP_WORKSPACE_H
#define FX_COMP_WORKSPACE_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wayland-util.h>

#include "comp/object.h"
#include "desktop/toplevel.h"
#include "server.h"

enum comp_workspace_type {
	COMP_WORKSPACE_TYPE_REGULAR,
	COMP_WORKSPACE_TYPE_FULLSCREEN,
};

struct comp_workspace {
	struct wl_list output_link;

	enum comp_workspace_type type;

	struct comp_output *output;

	// Geometry never set
	struct comp_object object;

	// struct wlr_scene_tree *workspace_tree;
	struct {
		// Used for tiled / fullscreen
		struct wlr_scene_tree *lower;
		// Floating toplevels
		struct wlr_scene_tree *floating;
		// for unmanaged toplevels without a parent
		struct wlr_scene_tree *unmanaged;
	} layers;

	// Toplevels and Popups. Also contains the focus order
	struct wl_list toplevels;

	struct comp_toplevel *fullscreen_toplevel;
};

/*
 * Util
 */

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
