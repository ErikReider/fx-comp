#include <cairo.h>
#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <wayland-server-protocol.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/util/log.h>
#include <xdg-shell-protocol.h>

#include "comp/server.h"
#include "constants.h"
#include "desktop/effects/shadow_data.h"
#include "desktop/toplevel.h"
#include "desktop/widgets/resize_edge.h"
#include "desktop/widgets/titlebar.h"
#include "seat/cursor.h"
#include "seat/seat.h"

static void set_xcursor_theme(enum xdg_toplevel_resize_edge edge) {
	const char *cursor;
	switch (edge) {
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
	wlr_cursor_set_xcursor(server.seat->cursor->wlr_cursor,
						   server.seat->cursor->cursor_mgr, cursor);
}

static void edge_destroy(struct comp_widget *widget) {
	struct comp_resize_edge *edge = wl_container_of(widget, edge, widget);

	free(edge);
}

static bool edge_handle_accepts_input(struct comp_widget *widget,
									  struct wlr_scene_buffer *buffer,
									  double *x, double *y) {
	struct comp_resize_edge *edge = wl_container_of(widget, edge, widget);
	struct comp_toplevel *toplevel = edge->toplevel;

	// Disable input if the toplevel requires cursor constraint
	struct wlr_pointer_constraint_v1 *constraint =
		server.seat->cursor->active_constraint;
	if (constraint) {
		struct comp_toplevel *constraint_toplevel =
			comp_toplevel_from_wlr_surface(constraint->surface);
		if (constraint_toplevel == toplevel) {
			return false;
		}
	}

	return true;
}

static void edge_pointer_button(struct comp_widget *widget, double x, double y,
								struct wlr_pointer_button_event *event) {
	if (event->state != WL_POINTER_BUTTON_STATE_PRESSED ||
		event->button != BTN_LEFT) {
		return;
	}

	struct comp_resize_edge *edge = wl_container_of(widget, edge, widget);
	struct comp_toplevel *toplevel = edge->toplevel;

	// Focus the titlebars toplevel
	comp_seat_surface_focus(&toplevel->object,
							comp_toplevel_get_wlr_surface(toplevel));

	// Begin resizing
	comp_toplevel_begin_interactive(toplevel, COMP_CURSOR_RESIZE, edge->edge);
}

static void edge_pointer_motion(struct comp_widget *widget, double x,
								double y) {
	struct comp_resize_edge *edge = wl_container_of(widget, edge, widget);
	set_xcursor_theme(edge->edge);
}

static void edge_pointer_leave(struct comp_widget *widget) {
	struct comp_resize_edge *edge = wl_container_of(widget, edge, widget);

	set_xcursor_theme(XDG_TOPLEVEL_RESIZE_EDGE_NONE);
}

static const struct comp_widget_impl comp_resize_edge_widget_impl = {
	.draw = NULL,
	.handle_pointer_enter = NULL,
	.handle_pointer_leave = edge_pointer_leave,
	.handle_pointer_motion = edge_pointer_motion,
	.handle_pointer_button = edge_pointer_button,
	.handle_point_accepts_input = edge_handle_accepts_input,
	.destroy = edge_destroy,
};

struct comp_resize_edge *
comp_resize_edge_init(struct comp_server *server,
					  struct comp_toplevel *toplevel,
					  enum xdg_toplevel_resize_edge resize_edge) {
	struct comp_resize_edge *edge = calloc(1, sizeof(struct comp_resize_edge));
	if (edge == NULL) {
		wlr_log(WLR_ERROR, "Failed to allocate comp_titlebar");
		return NULL;
	}

	if (!comp_widget_init(&edge->widget, server, &toplevel->object,
						  toplevel->decoration_scene_tree,
						  shadow_data_get_default(),
						  &comp_resize_edge_widget_impl)) {
		free(edge);
		return NULL;
	}

	edge->edge = resize_edge;
	edge->widget.sets_cursor = true;

	wlr_scene_node_set_enabled(&edge->widget.scene_buffer->node, true);
	edge->toplevel = toplevel;

	return edge;
}

void comp_resize_edge_get_geometry(struct comp_resize_edge *edge, int *width,
								   int *height, int *x, int *y) {
	struct comp_titlebar *titlebar = edge->toplevel->titlebar;

	const int RESIZE_WIDTH = BORDER_RESIZE_WIDTH + BORDER_WIDTH;
	const int CORNER_SIZE = titlebar->widget.corner_radius / 4 + RESIZE_WIDTH;
	const int CORNER_SIZE_DELTA = CORNER_SIZE - RESIZE_WIDTH;

	const int FULL_WIDTH = titlebar->widget.width + BORDER_RESIZE_WIDTH * 2;
	const int FULL_HEIGHT = titlebar->widget.height + BORDER_RESIZE_WIDTH * 2;

	const int ORIGIN_X = -RESIZE_WIDTH;
	const int ORIGIN_Y =
		(edge->toplevel->using_csd ? 0
								   : -edge->toplevel->titlebar->bar_height) -
		BORDER_WIDTH - BORDER_RESIZE_WIDTH;

	// NOTE: Maybe find a better way of doing this...
	switch (edge->edge) {
	// Edges
	case XDG_TOPLEVEL_RESIZE_EDGE_TOP:
		*width = FULL_WIDTH;
		*height = RESIZE_WIDTH;
		*x = ORIGIN_X;
		*y = ORIGIN_Y;
		break;
	case XDG_TOPLEVEL_RESIZE_EDGE_LEFT:
		*width = RESIZE_WIDTH;
		*height = FULL_HEIGHT;
		*x = ORIGIN_X;
		*y = ORIGIN_Y;
		break;
	case XDG_TOPLEVEL_RESIZE_EDGE_RIGHT:
		*width = RESIZE_WIDTH;
		*height = FULL_HEIGHT;
		*x = ORIGIN_X + FULL_WIDTH - RESIZE_WIDTH;
		*y = ORIGIN_Y;
		break;
	case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM:
		*width = FULL_WIDTH;
		*height = RESIZE_WIDTH;
		*x = ORIGIN_X;
		*y = ORIGIN_Y + FULL_HEIGHT - RESIZE_WIDTH;
		break;
	// Corners
	case XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT:
		*width = CORNER_SIZE;
		*height = CORNER_SIZE;
		*x = ORIGIN_X;
		*y = ORIGIN_Y;
		break;
	case XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT:
		*width = CORNER_SIZE;
		*height = CORNER_SIZE;
		*x = ORIGIN_X + FULL_WIDTH - RESIZE_WIDTH - CORNER_SIZE_DELTA;
		*y = ORIGIN_Y;
		break;
	case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT:
		*width = CORNER_SIZE;
		*height = CORNER_SIZE;
		*x = ORIGIN_X;
		*y = ORIGIN_Y + FULL_HEIGHT - RESIZE_WIDTH - CORNER_SIZE_DELTA;
		break;
	case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT:
		*width = CORNER_SIZE;
		*height = CORNER_SIZE;
		*x = ORIGIN_X + FULL_WIDTH - RESIZE_WIDTH - CORNER_SIZE_DELTA;
		*y = ORIGIN_Y + FULL_HEIGHT - RESIZE_WIDTH - CORNER_SIZE_DELTA;
		break;
	default:
		*width = 1;
		*height = 1;
		*x = 0;
		*y = 0;
		break;
	case XDG_TOPLEVEL_RESIZE_EDGE_NONE:
		wlr_log(WLR_ERROR, "Border edge can't be set to NONE");
		abort();
	}
}
