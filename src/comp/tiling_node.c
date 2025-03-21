#include <assert.h>
#include <pixman.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <wayland-util.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>

#include "comp/animation_mgr.h"
#include "comp/output.h"
#include "comp/server.h"
#include "comp/tiling_node.h"
#include "comp/transaction.h"
#include "comp/workspace.h"
#include "constants.h"
#include "desktop/toplevel.h"
#include "seat/seat.h"

static void tiling_node_destroy(struct tiling_node *node) {
	if (node->toplevel) {
		node->toplevel->tiling_node = NULL;
	}

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

static struct wlr_box get_final_tiling_toplevel_size(struct tiling_node *node) {
	struct comp_toplevel *toplevel = node->toplevel;
	struct tiling_node *container = toplevel->tiling_node;

	const int WIDTH_OFFSET = BORDER_WIDTH * 2;
	const int HEIGHT_OFFSET =
		BORDER_WIDTH * 2 + toplevel->decorated_size.top_border_height;

	return (struct wlr_box){
		.width = container->box.width - WIDTH_OFFSET - TILING_GAPS_INNER * 2,
		.height = container->box.height - HEIGHT_OFFSET - TILING_GAPS_INNER * 2,
		.x = container->box.x + BORDER_WIDTH + TILING_GAPS_INNER,
		.y = container->box.y + toplevel->decorated_size.top_border_height +
			 TILING_GAPS_INNER,
	};
}

static void apply_node_data_to_toplevel(struct tiling_node *node) {
	if (node->is_node) {
		return;
	}
	assert(node->toplevel);

	struct comp_toplevel *toplevel = node->toplevel;
	struct wlr_box box = get_final_tiling_toplevel_size(node);

	comp_toplevel_set_size(toplevel, box.width, box.height);
	comp_toplevel_set_position(toplevel, box.x, box.y);

	if (toplevel->state.width == box.width &&
		toplevel->state.height == box.height && toplevel->state.x == box.x &&
		toplevel->state.y == box.y) {
		wlr_log(WLR_DEBUG,
				"No size change for toplevel (%p), skipping resize animation",
				toplevel);
		return;
	}

	if (!toplevel->unmapped &&
		server.seat->cursor->cursor_mode != COMP_CURSOR_RESIZE) {
		// Only animate if not resizing and the toplevel is mapped
		comp_toplevel_add_size_animation(toplevel, toplevel->state,
										 toplevel->pending_state);
	}

	comp_object_mark_dirty(&toplevel->object);

	if (toplevel->anim.resize.client->state != ANIMATION_STATE_NONE) {
		toplevel->anim.resize.to = (struct comp_toplevel_state){
			.width = box.width,
			.height = box.height,
			.x = box.x,
			.y = box.y,
		};
	}
}

static void calc_size_pos_recursive(struct tiling_node *node, bool update) {
	if (node->children[0]) {
		if (!node->split_vertical) {
			const float split_width = node->box.width * node->split_ratio;
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
			const float split_height = node->box.height * node->split_ratio;
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

		calc_size_pos_recursive(node->children[0], update);
		calc_size_pos_recursive(node->children[1], update);
	} else if (update) {
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
		root->box = (struct wlr_box){
			.width = output->usable_area.width - TILING_GAPS_OUTER * 2,
			.height = output->usable_area.height - TILING_GAPS_OUTER * 2,
			.x = output->usable_area.x + TILING_GAPS_OUTER,
			.y = output->usable_area.y + TILING_GAPS_OUTER,
		};
		calc_size_pos_recursive(root, true);
	}
}

/**
 * Check if the toplevel will fit into the tiling size.
 * Returns false if the tiling size exceeds the max/min size of the toplevel.
 */
static bool is_size_compatible(struct comp_toplevel *toplevel,
							   struct tiling_node *container) {
	if (comp_toplevel_get_always_floating(toplevel)) {
		return false;
	}

	struct wlr_box tiling_box = get_final_tiling_toplevel_size(container);
	int max_width, max_height, min_width, min_height;
	comp_toplevel_get_constraints(toplevel, &min_width, &max_width, &min_height,
								  &max_height);

	// If the min/max size is equal to 0, there's not limit, so don't check
	// those conditions

	if (min_width > 0 && tiling_box.width < min_width) {
		goto too_small;
	} else if (max_width > 0 && tiling_box.width > max_width) {
		goto too_small;
	}

	if (min_height > 0 && tiling_box.height < min_height) {
		goto too_small;
	} else if (max_height > 0 && tiling_box.height > max_height) {
		goto too_small;
	}

	return true;

too_small:
	wlr_log(WLR_DEBUG,
			"Toplevel %s (%p) (Max: %ix%i, Min: %ix%i) doesn't fit in tiling "
			"rect (Size: %ix%i), setting as floating",
			comp_toplevel_get_title(toplevel), toplevel, max_width, max_height,
			min_width, min_height, tiling_box.width, tiling_box.height);
	return false;
}

void tiling_node_add_toplevel(struct comp_toplevel *toplevel,
							  const bool insert_floating) {
	toplevel->tiling_node = tiling_node_init(toplevel->workspace, false);
	struct tiling_node *container = toplevel->tiling_node;
	container->toplevel = toplevel;

	bool split_first = false;

	// Try to get parent node
	struct comp_workspace *ws = toplevel->workspace;
	struct tiling_node *parent_node = NULL;
	if (insert_floating) {
		// Get the tiling node beneath the floating toplevel.
		// Check if any toplevel intersects the center point of the toplevel.
		const int center_x =
			toplevel->state.x + (toplevel->decorated_size.width * 0.5);
		const int center_y =
			toplevel->state.y + (toplevel->decorated_size.height * 0.5);

		pixman_region32_t region1; // Top/left region
		pixman_region32_init(&region1);
		pixman_region32_t region2; // Bottom/right region
		pixman_region32_init(&region2);
		struct comp_toplevel *t;
		wl_list_for_each(t, &ws->toplevels, workspace_link) {
			struct tiling_node *n = t->tiling_node;
			if (!n || t == toplevel) {
				continue;
			}

			const double SPLIT_RATIO =
				n->parent ? n->parent->split_ratio : TILING_SPLIT_RATIO;

			if (n->box.width > n->box.height) {
				pixman_region32_init_rect(&region1, n->box.x, n->box.y,
										  n->box.width * SPLIT_RATIO,
										  n->box.height);
				pixman_region32_init_rect(
					&region2, n->box.x + n->box.width * SPLIT_RATIO, n->box.y,
					n->box.width * SPLIT_RATIO, n->box.height);
			} else {
				pixman_region32_init_rect(&region1, n->box.x, n->box.y,
										  n->box.width,
										  n->box.height * SPLIT_RATIO);
				pixman_region32_init_rect(
					&region2, n->box.x, n->box.y + n->box.height * SPLIT_RATIO,
					n->box.width, n->box.height * SPLIT_RATIO);
			}

			if (pixman_region32_contains_point(&region1, center_x, center_y,
											   NULL)) {
				// Insert as parent to node
				parent_node = n;
				split_first = true;
				break;
			} else if (pixman_region32_contains_point(&region2, center_x,
													  center_y, NULL)) {
				// Insert as child to node
				parent_node = n;
				split_first = false;
				break;
			}
		}
		pixman_region32_fini(&region1);
		pixman_region32_fini(&region2);
	} else {
		struct comp_toplevel *focused_toplevel = NULL;
		if ((focused_toplevel = comp_workspace_get_latest_focused(ws)) &&
			focused_toplevel->tiling_mode == COMP_TILING_MODE_TILED &&
			focused_toplevel != toplevel && focused_toplevel->tiling_node) {
			// Attach to focused tree
			parent_node = focused_toplevel->tiling_node;
		}
	}

	if (!parent_node) {
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
		// No tiled nodes, don't split first node
		struct comp_output *output = toplevel->workspace->output;
		container->box = (struct wlr_box){
			.width = output->usable_area.width - TILING_GAPS_OUTER * 2,
			.height = output->usable_area.height - TILING_GAPS_OUTER * 2,
			.x = output->usable_area.x + TILING_GAPS_OUTER,
			.y = output->usable_area.y + TILING_GAPS_OUTER,
		};

		// Don't tile if the tiling container size exceeds the min/max toplevel
		// size
		if (is_size_compatible(toplevel, container)) {
			apply_node_data_to_toplevel(container);
		} else {
			comp_toplevel_set_tiled(toplevel, false, true);
		}
		return;
	}

	struct tiling_node *new_parent =
		tiling_node_init(toplevel->workspace, true);
	new_parent->box = parent_node->box;
	new_parent->parent = parent_node->parent;
	new_parent->split_vertical =
		new_parent->box.width <= new_parent->box.height;

	if (split_first) {
		new_parent->children[1] = parent_node;
		new_parent->children[0] = container;
	} else {
		new_parent->children[0] = parent_node;
		new_parent->children[1] = container;
	}

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
			.width = new_parent->box.width * new_parent->split_ratio,
			.height = new_parent->box.height,
			.x = new_parent->box.x,
			.y = new_parent->box.y,
		};
		container->box = (struct wlr_box){
			.width = new_parent->box.width * new_parent->split_ratio,
			.height = new_parent->box.height,
			.x = new_parent->box.x +
				 new_parent->box.width * new_parent->split_ratio,
			.y = new_parent->box.y,
		};
	} else {
		parent_node->box = (struct wlr_box){
			.width = new_parent->box.width,
			.height = new_parent->box.height * new_parent->split_ratio,
			.x = new_parent->box.x,
			.y = new_parent->box.y,
		};
		container->box = (struct wlr_box){
			.width = new_parent->box.width,
			.height = new_parent->box.height * new_parent->split_ratio,
			.x = new_parent->box.x,
			.y = new_parent->box.y +
				 new_parent->box.height * new_parent->split_ratio,
		};
	}

	parent_node->parent = new_parent;
	container->parent = new_parent;

	// Don't tile if the tiling container size exceeds the min/max toplevel size
	if (!is_size_compatible(toplevel, container)) {
		comp_toplevel_set_tiled(toplevel, false, true);
		return;
	}

	calc_size_pos_recursive(new_parent, true);

	tiling_node_mark_workspace_dirty(toplevel->workspace);
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
		calc_size_pos_recursive(sibling->parent, true);
	} else {
		calc_size_pos_recursive(sibling, true);
	}

	tiling_node_destroy(node->parent);
	tiling_node_destroy(node);
}

void tiling_node_resize_start(struct comp_toplevel *toplevel) {
	struct tiling_node *node = toplevel->tiling_node;
	clock_gettime(CLOCK_MONOTONIC, &node->time);
}

void tiling_node_resize_fini(struct comp_toplevel *toplevel) {
}

/** Only render each 16.667ms */
static bool can_update(struct comp_toplevel *toplevel) {
	struct tiling_node *node = toplevel->tiling_node;

	float ms = 16.667;
	struct comp_workspace *ws = toplevel->workspace;
	if (ws && ws->output && ws->output != server.fallback_output) {
		ms = ws->output->refresh_sec * 1000;
	}

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	struct timespec *start = &node->time;
	const float DELTA = (now.tv_sec - start->tv_sec) * 1000 +
						(now.tv_nsec - start->tv_nsec) / 1000000.0;

	if (DELTA < ms) {
		return false;
	}

	clock_gettime(CLOCK_MONOTONIC, &node->time);

	return true;
}

void tiling_node_resize(struct comp_toplevel *toplevel) {
	const int MAX_DISTANCE = 2;

	struct comp_seat *seat = server.seat;
	struct tiling_node *node = toplevel->tiling_node;

	struct wlr_box box = node->box;
	struct wlr_box usable_area = toplevel->workspace->output->usable_area;

	const double delta_x =
		(seat->cursor->wlr_cursor->x - seat->cursor->previous.x);
	const double delta_y =
		(seat->cursor->wlr_cursor->y - seat->cursor->previous.y);

	if ((ABS(delta_x) < 0.5 && ABS(delta_y) < 0.5)) {
		return;
	}

	// Thanks Hyprland :)

	const bool ON_DISPLAY_LEFT = abs(box.x - usable_area.x) < MAX_DISTANCE;
	const bool ON_DISPLAY_RIGHT =
		abs((box.x + box.width) - (usable_area.x + usable_area.width)) <
		MAX_DISTANCE;
	const bool ON_DISPLAY_TOP = abs(box.y - usable_area.y) < MAX_DISTANCE;
	const bool ON_DISPLAY_BOTTOM =
		abs((box.y + box.height) - (usable_area.y + usable_area.height)) <
		MAX_DISTANCE;

	double allow_x_movement = ON_DISPLAY_LEFT && ON_DISPLAY_RIGHT ? 0 : delta_x;
	double allow_y_movement = ON_DISPLAY_TOP && ON_DISPLAY_BOTTOM ? 0 : delta_y;

	struct tiling_node *v_outer = NULL;
	struct tiling_node *h_outer = NULL;
	const bool LEFT = seat->resize_edges & WLR_EDGE_LEFT || ON_DISPLAY_RIGHT;
	const bool TOP = seat->resize_edges & WLR_EDGE_TOP || ON_DISPLAY_BOTTOM;
	const bool RIGHT = seat->resize_edges & WLR_EDGE_RIGHT || ON_DISPLAY_LEFT;
	const bool BOTTOM = seat->resize_edges & WLR_EDGE_BOTTOM || ON_DISPLAY_TOP;
	const bool NONE = seat->resize_edges & WLR_EDGE_NONE;

	for (struct tiling_node *current = node; current && current->parent;
		 current = current->parent) {
		const struct tiling_node *parent = current->parent;

		if (!v_outer && parent->split_vertical &&
			(NONE || (TOP && parent->children[1] == current) ||
			 (BOTTOM && parent->children[0] == current))) {
			v_outer = current;
		} else if (!h_outer && !parent->split_vertical &&
				   (NONE || (LEFT && parent->children[1] == current) ||
					(RIGHT && parent->children[0] == current))) {
			h_outer = current;
		}

		if (v_outer && h_outer) {
			break;
		}
	}

	bool update = can_update(toplevel);

	if (h_outer) {
		h_outer->parent->split_ratio =
			CLAMP(h_outer->parent->split_ratio +
					  allow_x_movement / h_outer->parent->box.width,
				  0.1, 1.9);

		calc_size_pos_recursive(h_outer->parent, update);
	}

	if (v_outer) {
		v_outer->parent->split_ratio =
			CLAMP(v_outer->parent->split_ratio +
					  allow_y_movement / v_outer->parent->box.height,
				  0.1, 1.9);

		calc_size_pos_recursive(v_outer->parent, update);
	}
}

void tiling_node_move_start(struct comp_toplevel *toplevel) {
	if (!toplevel->tiling_node || toplevel->dragging_tiled) {
		return;
	}

	toplevel->dragging_tiled = true;
	comp_toplevel_refresh_titlebar_effects(toplevel);
	comp_toplevel_set_tiled(toplevel, false, false);
}

void tiling_node_move_fini(struct comp_toplevel *toplevel) {
	toplevel->dragging_tiled = false;
	comp_toplevel_refresh_titlebar_effects(toplevel);
	comp_toplevel_set_tiled(toplevel, true, false);
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
	node->split_ratio = CLAMP(TILING_SPLIT_RATIO, 0.1, 1.9);
	node->split_vertical = false;
	node->box = ws->output->usable_area;

	wl_list_insert(&ws->tiling_nodes, &node->parent_link);

	return node;
}
