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
	if (toplevel->workspace == dest_workspace) {
		return;
	}
	wlr_log(WLR_DEBUG, "Changing toplevel output from: '%s' to '%s'\n",
			toplevel->workspace->output->wlr_output->name,
			dest_workspace->output->wlr_output->name);
	wl_list_remove(&toplevel->workspace_link);
	toplevel->workspace = dest_workspace;
	wl_list_insert(&dest_workspace->toplevels, &toplevel->workspace_link);

	int x, y;
	wlr_scene_node_coords(&toplevel->object.scene_tree->node, &x, &y);

	// Reparent node onto the new workspace
	struct wlr_scene_tree *new_layer = comp_toplevel_get_layer(toplevel);
	wlr_scene_node_reparent(&toplevel->object.scene_tree->node, new_layer);

	// Adjust the node coordinates to be output-relative
	double lx = x;
	double ly = y;
	wlr_output_layout_output_coords(server.output_layout,
									toplevel->workspace->output->wlr_output,
									&lx, &ly);
	comp_toplevel_set_position(toplevel, lx, ly);
}

struct comp_toplevel *
comp_workspace_get_latest_focused(struct comp_workspace *ws) {
	if (wl_list_empty(&ws->toplevels)) {
		return NULL;
	}
	struct comp_toplevel *toplevel =
		wl_container_of(ws->toplevels.next, toplevel, workspace_link);
	return toplevel;
}

struct comp_toplevel *
comp_workspace_get_next_focused(struct comp_workspace *ws) {
	if (wl_list_empty(&ws->toplevels)) {
		return NULL;
	}
	struct comp_toplevel *toplevel =
		wl_container_of(ws->toplevels.prev, toplevel, workspace_link);
	return toplevel;
}
struct comp_toplevel *
comp_workspace_get_prev_focused(struct comp_workspace *ws) {
	if (wl_list_empty(&ws->toplevels)) {
		return NULL;
	}
	struct comp_toplevel *toplevel =
		wl_container_of(ws->toplevels.next->next, toplevel, workspace_link);
	return toplevel;
}

struct comp_toplevel *
comp_workspace_get_toplevel_direction(struct comp_workspace *ws,
									  enum wlr_direction direction) {
	// Get latest focused toplevel on other monitor if fullscreen
	if (ws->fullscreen_toplevel || wl_list_empty(&ws->toplevels)) {
		goto focus_adjacent_monitor;
	}

	struct comp_toplevel *focused_toplevel =
		comp_workspace_get_latest_focused(ws);
	if (focused_toplevel->tiling_mode == COMP_TILING_MODE_FLOATING) {
		// TODO: Floating direction switching?
		wlr_log(WLR_DEBUG, "Floating direction switching not supported");
		return NULL;
	}

	struct comp_toplevel *toplevel = NULL;
	int leader_value = -1;
	struct wlr_box box = focused_toplevel->tiling_node->box;

	const int MAX_DISTANCE = 2;
	struct tiling_node *node;
	wl_list_for_each(node, &ws->tiling_nodes, parent_link) {
		if (node->is_node) {
			continue;
		}
		if (!node->toplevel || node->toplevel == focused_toplevel) {
			continue;
		}

		struct wlr_box iter_box = node->box;
		int intersect_length = -1;
		switch (direction) {
		case WLR_DIRECTION_LEFT:
			if (abs(box.x - (iter_box.x + iter_box.width)) < MAX_DISTANCE) {
				intersect_length = MAX(
					0.0, MIN(box.y + box.height, iter_box.y + iter_box.height) -
							 MAX(box.y, iter_box.y));
			}
			break;
		case WLR_DIRECTION_RIGHT:
			if (abs((box.x + box.width) - iter_box.x) < MAX_DISTANCE) {
				intersect_length = MAX(
					0.0, MIN(box.y + box.height, iter_box.y + iter_box.height) -
							 MAX(box.y, iter_box.y));
			}
			break;
		case WLR_DIRECTION_UP:
			if (abs(box.y - (iter_box.y + iter_box.height)) < MAX_DISTANCE) {
				intersect_length = MAX(
					0.0, MIN(box.x + box.width, iter_box.x + iter_box.width) -
							 MAX(box.x, iter_box.x));
			}
			break;
		case WLR_DIRECTION_DOWN:
			if (abs((box.y + box.height) - iter_box.y) < MAX_DISTANCE) {
				intersect_length = MAX(
					0.0, MIN(box.x + box.width, iter_box.x + iter_box.width) -
							 MAX(box.x, iter_box.x));
			}
			break;
		}
		if (intersect_length > 0) {
			// Find the closest toplevel which was focused most recently
			int index = 0;
			struct comp_toplevel *t;
			wl_list_for_each_reverse(t, &ws->toplevels, workspace_link) {
				if (t == node->toplevel) {
					break;
				}
				index++;
			}

			if (index > leader_value) {
				leader_value = index;
				toplevel = node->toplevel;
			}
		}
	}

	if (toplevel) {
		return toplevel;
	}

focus_adjacent_monitor:;
	// Get latest focused toplevel on other monitor
	struct wlr_box output_box;
	wlr_output_layout_get_box(server.output_layout, ws->output->wlr_output,
							  &output_box);
	int lx = output_box.x + output_box.width / 2;
	int ly = output_box.y + output_box.height / 2;
	struct wlr_output *wlr_adjacent = wlr_output_layout_adjacent_output(
		server.output_layout, direction, ws->output->wlr_output, lx, ly);
	struct comp_output *output = NULL;
	if (wlr_adjacent && (output = wlr_adjacent->data)) {
		return comp_workspace_get_latest_focused(output->active_workspace);
	}

	return NULL;
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
	ws->object.content_tree = alloc_tree(ws->object.scene_tree);
	if (!ws->object.scene_tree || !ws->object.content_tree) {
		return NULL;
	}
	ws->object.scene_tree->node.data = &ws->object;
	ws->object.data = ws;
	ws->object.type = COMP_OBJECT_TYPE_WORKSPACE;
	ws->object.destroying = false;

	// Create tiled/fullscreen
	ws->layers.lower = alloc_tree(ws->object.content_tree);
	if (!ws->layers.lower) {
		return NULL;
	}
	ws->layers.lower->node.data = &ws->object;
	// Create floating
	ws->layers.floating = alloc_tree(ws->object.content_tree);
	if (!ws->layers.floating) {
		return NULL;
	}
	ws->layers.floating->node.data = &ws->object;

	wl_list_init(&ws->tiling_nodes);
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
