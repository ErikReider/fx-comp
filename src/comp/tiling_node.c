#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <wayland-util.h>
#include <wlr/util/log.h>

#include "comp/output.h"
#include "comp/tiling_node.h"
#include "comp/workspace.h"
#include "constants.h"
#include "desktop/toplevel.h"

static void tiling_node_destroy(struct tiling_node *node) {

	wl_list_remove(&node->parent_link);

	free(node);
}

static inline struct tiling_node *get_root_node(struct comp_workspace *ws) {
	struct tiling_node *node;
	wl_list_for_each(node, &ws->tiling_nodes, parent_link) {
		if (!node->parent) {
			return node;
		}
	}

	return NULL;
}

static void apply_node_data_to_toplevel(struct tiling_node *node) {
	if (node->is_node) {
		return;
	}
	assert(node->toplevel);

	struct comp_toplevel *toplevel = node->toplevel;
	struct tiling_node *container = toplevel->tiling_node;

	const int WIDTH_OFFSET =
		toplevel->decorated_size.width - toplevel->state.width;
	const int HEIGHT_OFFSET =
		toplevel->decorated_size.height - toplevel->state.height;

	comp_toplevel_set_size(
		toplevel, container->box.width - WIDTH_OFFSET - TILING_GAPS * 2,
		container->box.height - HEIGHT_OFFSET - TILING_GAPS * 2);

	comp_toplevel_set_position(
		toplevel, container->box.x + BORDER_WIDTH + TILING_GAPS,
		container->box.y + toplevel->decorated_size.top_border_height +
			TILING_GAPS);

	comp_toplevel_mark_dirty(toplevel);
}

static void calc_size_pos_recursive(struct tiling_node *node) {
	if (node->children[0]) {
		if (node->box.width > node->box.height) {
			const float split_width = node->box.width * TILING_SPLIT_RATIO;
			node->children[0]->box = (struct wlr_box){
				.width = split_width,
				.height = node->box.height,
				.x = node->box.x,
				.y = node->box.y,
			};
			node->children[1]->box = (struct wlr_box){
				.width = node->box.width - split_width,
				.height = node->box.height,
				.x = node->box.x + split_width,
				.y = node->box.y,
			};
		} else {
			const float split_height = node->box.height * TILING_SPLIT_RATIO;
			node->children[0]->box = (struct wlr_box){
				.width = node->box.width,
				.height = split_height,
				.x = node->box.x,
				.y = node->box.y,
			};
			node->children[1]->box = (struct wlr_box){
				.width = node->box.width,
				.height = node->box.height - split_height,
				.x = node->box.x,
				.y = node->box.y + split_height,
			};
		}

		calc_size_pos_recursive(node->children[0]);
		calc_size_pos_recursive(node->children[1]);
	} else {
		// Toplevel
		apply_node_data_to_toplevel(node);
	}
}

void tiling_node_mark_workspace_dirty(struct comp_workspace *workspace) {
	switch (workspace->type) {
	case COMP_WORKSPACE_TYPE_REGULAR:
		break;
	case COMP_WORKSPACE_TYPE_FULLSCREEN:
		// Don't tile on fullscreen workspaces
		return;
	}

	struct tiling_node *root = get_root_node(workspace);
	if (root) {
		struct comp_output *output = workspace->output;
		root->box = output->usable_area;
		calc_size_pos_recursive(root);
	}
}

void tiling_node_add_toplevel(struct comp_toplevel *toplevel) {
	toplevel->tiling_node = tiling_node_init(toplevel->state.workspace, false);
	struct tiling_node *container = toplevel->tiling_node;
	container->toplevel = toplevel;

	// Try to get parent node
	struct comp_workspace *ws = toplevel->state.workspace;
	struct tiling_node *parent_node = NULL;
	struct comp_toplevel *focused_toplevel =
		comp_workspace_get_latest_focused(ws);
	if (focused_toplevel &&
		focused_toplevel->tiling_mode == COMP_TILING_MODE_TILED &&
		focused_toplevel != toplevel) {
		// Attach to focused tree
		assert(focused_toplevel->tiling_node);
		parent_node = focused_toplevel->tiling_node;
	} else {
		// Try focusing the latest tiled toplevel
		struct comp_toplevel *t;
		wl_list_for_each(t, &ws->toplevels, workspace_link) {
			if (t != toplevel && t->tiling_node) {
				parent_node = t->tiling_node;
				break;
			}
		}
	}

	if (!parent_node) {
		// Don't split first node
		struct comp_output *output = toplevel->state.workspace->output;
		container->box = output->usable_area;

		apply_node_data_to_toplevel(container);
		return;
	}

	struct tiling_node *new_parent =
		tiling_node_init(toplevel->state.workspace, true);
	new_parent->box = parent_node->box;
	new_parent->parent = parent_node->parent;

	new_parent->children[0] = parent_node;
	new_parent->children[1] = container;

	if (parent_node->parent) {
		if (parent_node->parent->children[0] == parent_node) {
			parent_node->parent->children[0] = new_parent;
		} else {
			parent_node->parent->children[1] = new_parent;
		}
	}

	// Update
	if (new_parent->box.width > new_parent->box.height) {
		parent_node->box = (struct wlr_box){
			.width = new_parent->box.width / 2,
			.height = new_parent->box.height,
			.x = new_parent->box.x,
			.y = new_parent->box.y,
		};
		container->box = (struct wlr_box){
			.width = new_parent->box.width / 2,
			.height = new_parent->box.height,
			.x = new_parent->box.x + new_parent->box.width / 2,
			.y = new_parent->box.y,
		};
	} else {
		parent_node->box = (struct wlr_box){
			.width = new_parent->box.width,
			.height = new_parent->box.height / 2,
			.x = new_parent->box.x,
			.y = new_parent->box.y,
		};
		container->box = (struct wlr_box){
			.width = new_parent->box.width,
			.height = new_parent->box.height / 2,
			.x = new_parent->box.x,
			.y = new_parent->box.y + new_parent->box.height / 2,
		};
	}

	parent_node->parent = new_parent;
	container->parent = new_parent;

	calc_size_pos_recursive(new_parent);

	tiling_node_mark_workspace_dirty(toplevel->state.workspace);
}

void tiling_node_remove_toplevel(struct comp_toplevel *toplevel) {
	struct tiling_node *node = toplevel->tiling_node;
	if (!node) {
		return;
	}

	struct tiling_node *parent = node->parent;
	if (!parent) {
		tiling_node_destroy(node);
		return;
	}

	struct tiling_node *sibling =
		parent->children[0] == node ? parent->children[1] : parent->children[0];
	sibling->box = parent->box;
	sibling->parent = parent->parent;

	if (parent->parent) {
		if (parent->parent->children[0] == parent) {
			parent->parent->children[0] = sibling;
		} else {
			parent->parent->children[1] = sibling;
		}
	}

	if (sibling->parent) {
		calc_size_pos_recursive(sibling->parent);
	} else {
		calc_size_pos_recursive(sibling);
	}

	tiling_node_destroy(node->parent);
	tiling_node_destroy(node);
}

struct tiling_node *tiling_node_init(struct comp_workspace *ws, bool is_node) {
	struct tiling_node *node = calloc(1, sizeof(*node));
	if (!node) {
		wlr_log(WLR_ERROR, "Failed to allocate tiling_container");
		abort();
	}

	node->parent = NULL;
	node->ws = ws;
	node->is_node = is_node;
	node->box = ws->output->usable_area;

	wl_list_insert(&ws->tiling_nodes, &node->parent_link);

	return node;
}