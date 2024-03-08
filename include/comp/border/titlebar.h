#ifndef FX_COMP_BORDER_TITLEBAR_H
#define FX_COMP_BORDER_TITLEBAR_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_pointer.h>

#include "comp/widget.h"

#define TITLEBAR_NUM_BUTTONS 3

struct comp_titlebar {
	struct comp_toplevel *toplevel;

	struct comp_widget widget;

	struct {
		struct comp_widget_click_region close;
		struct comp_widget_click_region fullscreen;
		struct comp_widget_click_region minimize;

		bool on_right;
		struct comp_widget_click_region *order[TITLEBAR_NUM_BUTTONS];
	} buttons;
};

struct comp_titlebar *comp_titlebar_init(struct comp_server *server,
										 struct comp_toplevel *toplevel);

bool comp_titlebar_should_be_shown(struct comp_toplevel *toplevel);

#endif // !FX_COMP_BORDER_TITLEBAR_H
