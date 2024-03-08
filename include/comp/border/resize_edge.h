#ifndef FX_COMP_BORDER_RESIZE_EDGE_H
#define FX_COMP_BORDER_RESIZE_EDGE_H

#include <xdg-shell-protocol.h>

#include "comp/widget.h"

struct comp_resize_edge {
	struct comp_toplevel *toplevel;

	enum xdg_toplevel_resize_edge edge;

	struct comp_widget widget;
};

struct comp_resize_edge *
comp_resize_edge_init(struct comp_server *server,
					  struct comp_toplevel *toplevel,
					  enum xdg_toplevel_resize_edge resize_edge);

void comp_resize_edge_get_geometry(struct comp_resize_edge *edge, int *width,
								   int *height, int *x, int *y);

#endif // !FX_COMP_BORDER_RESIZE_EDGE_H
