#include <cairo.h>
#include <stdlib.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/util/log.h>
#include <xdg-shell-protocol.h>

#include "comp/border/edge.h"
#include "comp/border/titlebar.h"
#include "comp/server.h"
#include "constants.h"
#include "desktop/toplevel.h"
#include "seat/cursor.h"

static void set_xcursor_theme(struct comp_edge *edge) {
	const char *cursor;
	switch (edge->edges) {
	case XDG_TOPLEVEL_RESIZE_EDGE_NONE:
		cursor = "default";
		break;
	case XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT:
		cursor = "top_left_corner";
		break;
	case XDG_TOPLEVEL_RESIZE_EDGE_TOP:
		cursor = "top_side";
		break;
	case XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT:
		cursor = "top_right_corner";
		break;
	case XDG_TOPLEVEL_RESIZE_EDGE_LEFT:
		cursor = "left_side";
		break;
	case XDG_TOPLEVEL_RESIZE_EDGE_RIGHT:
		cursor = "right_side";
		break;
	case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT:
		cursor = "bottom_left_corner";
		break;
	case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM:
		cursor = "bottom_side";
		break;
	case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT:
		cursor = "bottom_right_corner";
		break;
	}
	wlr_cursor_set_xcursor(server.cursor->wlr_cursor, server.cursor->cursor_mgr,
						   cursor);
}

static void edge_destroy(struct comp_widget *widget) {
	struct comp_edge *edge = wl_container_of(widget, edge, widget);

	free(edge);
}

static void edge_pointer_button(struct comp_widget *widget, double x, double y,
								struct wlr_pointer_button_event *event) {
	if (event->state != WLR_BUTTON_PRESSED) {
		return;
	}

	struct comp_edge *edge = wl_container_of(widget, edge, widget);
	struct comp_toplevel *toplevel = edge->toplevel;

	// Focus the titlebars toplevel
	comp_toplevel_focus(toplevel, toplevel->xdg_toplevel->base->surface);

	// Begin resizing
	comp_toplevel_begin_interactive(toplevel, COMP_CURSOR_RESIZE, edge->edges);
}

static void edge_pointer_motion(struct comp_widget *widget, double x,
								double y) {
	struct comp_edge *edge = wl_container_of(widget, edge, widget);

	struct comp_toplevel *toplevel = edge->toplevel;
	struct comp_titlebar *titlebar = toplevel->titlebar;

	struct wlr_box inner_box = {
		.x = BORDER_RESIZE_WIDTH,
		.y = BORDER_RESIZE_WIDTH,
		.width = titlebar->widget.object.width,
		.height = titlebar->widget.object.height,
	};

	const bool top = y < inner_box.y;
	const bool bottom = y > (inner_box.height + inner_box.y);
	const bool left = x < inner_box.x;
	const bool right = x > (inner_box.width + inner_box.x);

	if (top) {
		if (left) {
			edge->edges = XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;
		} else if (right) {
			edge->edges = XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;
		} else {
			edge->edges = XDG_TOPLEVEL_RESIZE_EDGE_TOP;
		}
	} else if (bottom) {
		if (left) {
			edge->edges = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;
		} else if (right) {
			edge->edges = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;
		} else {
			edge->edges = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;
		}
	} else if (left) {
		edge->edges = XDG_TOPLEVEL_RESIZE_EDGE_LEFT;
	} else if (right) {
		edge->edges = XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;
	} else {
		edge->edges = XDG_TOPLEVEL_RESIZE_EDGE_NONE;
	}

	set_xcursor_theme(edge);
}

static void edge_pointer_leave(struct comp_widget *widget) {
	struct comp_edge *edge = wl_container_of(widget, edge, widget);

	edge->edges = XDG_TOPLEVEL_RESIZE_EDGE_NONE;
	set_xcursor_theme(edge);
}

static const struct comp_widget_impl comp_edge_widget_impl = {
	.draw = NULL,
	.handle_pointer_enter = NULL,
	.handle_pointer_leave = edge_pointer_leave,
	.handle_pointer_motion = edge_pointer_motion,
	.handle_pointer_button = edge_pointer_button,
	.destroy = edge_destroy,
};

struct comp_edge *comp_edge_init(struct comp_server *server,
								 struct comp_toplevel *toplevel) {
	struct comp_edge *edge = calloc(1, sizeof(struct comp_edge));
	if (edge == NULL) {
		wlr_log(WLR_ERROR, "Failed to allocate comp_titlebar");
		return NULL;
	}

	if (!comp_widget_init(&edge->widget, server, &toplevel->object,
						  &comp_edge_widget_impl)) {
		free(edge);
		return NULL;
	}

	edge->edges = XDG_TOPLEVEL_RESIZE_EDGE_NONE;
	edge->widget.sets_cursor = true;

	wlr_scene_node_set_enabled(&edge->widget.scene_buffer->node, true);
	edge->toplevel = toplevel;

	return edge;
}
