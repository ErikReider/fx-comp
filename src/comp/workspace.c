#include <scenefx/types/wlr_scene.h>
#include <stdio.h>
#include <stdlib.h>
#include <wayland-util.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/log.h>

#include "comp/output.h"
#include "comp/workspace.h"
#include "desktop/toplevel.h"
#include "util.h"

void comp_workspace_move_toplevel_to(struct comp_workspace *dest_workspace,
									 struct comp_toplevel *toplevel) {
	if (toplevel->state.workspace == dest_workspace) {
		return;
	}
	wlr_log(WLR_DEBUG, "Changing toplevel output from: '%s' to '%s'\n",
			toplevel->state.workspace->output->wlr_output->name,
			dest_workspace->output->wlr_output->name);
	wl_list_remove(&toplevel->workspace_link);
	toplevel->state.workspace = dest_workspace;
	wl_list_insert(&dest_workspace->toplevels, &toplevel->workspace_link);

	int x, y;
	wlr_scene_node_coords(&toplevel->object.scene_tree->node, &x, &y);

	// Reparent node onto the new workspace
	struct wlr_scene_tree *new_layer = comp_toplevel_get_layer(toplevel);
	wlr_scene_node_reparent(&toplevel->object.scene_tree->node, new_layer);

	// Adjust the node coordinates to be output-relative
	double lx = x;
	double ly = y;
	wlr_output_layout_output_coords(
		server.output_layout, toplevel->state.workspace->output->wlr_output,
		&lx, &ly);
	comp_toplevel_set_position(toplevel, lx, ly);
}

struct comp_toplevel *
comp_workspace_get_latest_focused(struct comp_workspace *ws) {
	struct comp_toplevel *toplevel =
		wl_container_of(ws->toplevels.next, toplevel, workspace_link);
	return toplevel;
}

struct comp_toplevel *
comp_workspace_get_next_focused(struct comp_workspace *ws) {
	struct comp_toplevel *toplevel =
		wl_container_of(ws->toplevels.prev, toplevel, workspace_link);
	return toplevel;
}
struct comp_toplevel *
comp_workspace_get_prev_focused(struct comp_workspace *ws) {
	struct comp_toplevel *toplevel =
		wl_container_of(ws->toplevels.next->next, toplevel, workspace_link);
	return toplevel;
}

struct comp_workspace *comp_workspace_new(struct comp_output *output,
										  enum comp_workspace_type type) {
	struct comp_workspace *ws = calloc(1, sizeof(*ws));
	if (ws == NULL) {
		wlr_log(WLR_ERROR, "Could not allocate comp_workspace");
		return NULL;
	}

	ws->type = type;
	ws->output = output;

	// Create workspace tree
	ws->object.scene_tree = alloc_tree(output->layers.workspaces);
	if (!ws->object.scene_tree) {
		return NULL;
	}
	ws->object.scene_tree->node.data = &ws->object;
	ws->object.data = ws;
	ws->object.type = COMP_OBJECT_TYPE_WORKSPACE;

	// Create tiled/fullscreen
	ws->layers.lower = alloc_tree(ws->object.scene_tree);
	if (!ws->layers.lower) {
		return NULL;
	}
	ws->layers.lower->node.data = &ws->object;
	// Create floating
	ws->layers.floating = alloc_tree(ws->object.scene_tree);
	if (!ws->layers.floating) {
		return NULL;
	}
	ws->layers.floating->node.data = &ws->object;
	// Create unmanaged
	ws->layers.unmanaged = alloc_tree(ws->object.scene_tree);
	if (!ws->layers.unmanaged) {
		return NULL;
	}
	ws->layers.unmanaged->node.data = &ws->object;

	wl_list_init(&ws->toplevels);

	// Insert next to active workspace
	struct wl_list *pos = output->active_workspace
							  ? output->active_workspace->output_link.prev
							  : &output->workspaces;
	wl_list_insert(pos, &ws->output_link);

	comp_output_focus_workspace(output, ws);

	return ws;
}

void comp_workspace_destroy(struct comp_workspace *ws) {
	wl_list_remove(&ws->output_link);

	wlr_scene_node_destroy(&ws->object.scene_tree->node);

	free(ws);
}
