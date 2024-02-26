#ifndef FX_COMP_BORDER_EDGE_H
#define FX_COMP_BORDER_EDGE_H

#include <xdg-shell-protocol.h>

#include "comp/widget.h"

struct comp_edge {
	struct comp_toplevel *toplevel;

	enum xdg_toplevel_resize_edge edges;

	struct comp_widget widget;
};

struct comp_edge *comp_edge_init(struct comp_server *server,
								 struct comp_toplevel *toplevel);

#endif // !FX_COMP_BORDER_EDGE_H
