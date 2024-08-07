#ifndef FX_COMP_TILING_CONTAINER_H
#define FX_COMP_TILING_CONTAINER_H

#include <stdbool.h>
#include <wayland-util.h>
#include <wlr/util/box.h>

struct tiling_node {
	struct wl_list parent_link;

	struct tiling_node *parent;
	struct tiling_node *children[2];

	struct comp_workspace *ws;

	bool is_node;
	// NODE: Gapless size and position
	struct wlr_box box;
	// NON-NODE: Connected toplevel
	struct comp_toplevel *toplevel;
};

struct tiling_node *tiling_node_init(struct comp_workspace *ws, bool is_node);

void tiling_node_mark_workspace_dirty(struct comp_workspace *workspace);

/** Adds, resizes, and repositions the toplevel */
void tiling_node_add_toplevel(struct comp_toplevel *toplevel);
void tiling_node_remove_toplevel(struct comp_toplevel *toplevel);

#endif // !FX_COMP_CONTAINER_H
