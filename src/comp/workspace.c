#include <scenefx/types/wlr_scene.h>
#include <stdio.h>
#include <stdlib.h>
#include <wayland-util.h>
#include <wlr/util/log.h>

#include "comp/output.h"
#include "comp/workspace.h"
#include "desktop/toplevel.h"
#include "util.h"

int comp_workspace_find_index(struct wl_list *list, struct comp_workspace *ws) {
	int pos = 0;
	struct comp_workspace *pos_ws;
	wl_list_for_each_reverse(pos_ws, list, link) {
		if (pos_ws == ws) {
			return pos;
		}
		pos++;
	}
	return -1;
}

void comp_workspace_move_toplevel_to(struct comp_workspace *dest_workspace,
									 struct comp_toplevel *toplevel) {
	if (toplevel->workspace == dest_workspace) {
		return;
	}
	wlr_log(WLR_DEBUG, "Changing toplevel output from: '%s' to '%s'\n",
			toplevel->workspace->output->wlr_output->name,
			dest_workspace->output->wlr_output->name);
	wl_list_remove(&toplevel->workspace_link);
	toplevel->workspace = dest_workspace;
	wl_list_insert(&dest_workspace->toplevels, &toplevel->workspace_link);

	// Reparent node onto the new workspace
	struct wlr_scene_tree *new_layer = comp_toplevel_get_layer(toplevel);
	wlr_scene_node_reparent(&toplevel->object.scene_tree->node, new_layer);
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
	ws->workspace_tree = alloc_tree(output->layers.workspaces);
	if (!ws->workspace_tree) {
		return NULL;
	}
	// Create tiled/fullscreen
	ws->layers.lower = alloc_tree(ws->workspace_tree);
	if (!ws->layers.lower) {
		return NULL;
	}
	ws->layers.lower->node.data = ws;
	// Create floating
	ws->layers.floating = alloc_tree(ws->workspace_tree);
	if (!ws->layers.floating) {
		return NULL;
	}
	ws->layers.floating->node.data = ws;

	wl_list_init(&ws->toplevels);

	wl_list_insert(&output->workspaces, &ws->link);

	comp_output_focus_workspace(output, ws);

	return ws;
}

void comp_workspace_destroy(struct comp_workspace *ws) {
	wl_list_remove(&ws->link);

	wlr_scene_node_destroy(&ws->workspace_tree->node);

	free(ws);
}
