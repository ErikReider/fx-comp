#ifndef FX_COMP_BORDER_TITLEBAR_H
#define FX_COMP_BORDER_TITLEBAR_H

#include <pango/pango-font.h>
#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_pointer.h>

#include "comp/widget.h"

#define TITLEBAR_NUM_BUTTONS 3

enum comp_titlebar_button_type {
	COMP_TITLEBAR_BUTTON_CLOSE,
	COMP_TITLEBAR_BUTTON_FULLSCREEN,
	COMP_TITLEBAR_BUTTON_MINIMIZE,
};

struct comp_titlebar {
	struct comp_toplevel *toplevel;

	struct comp_widget widget;

	int bar_height;

	struct {
		struct comp_widget_click_region close;
		struct comp_widget_click_region fullscreen;
		struct comp_widget_click_region minimize;

		bool on_right;
		struct comp_widget_click_region *order[TITLEBAR_NUM_BUTTONS];
	} buttons;

	PangoFontDescription *font;
};

struct comp_titlebar *comp_titlebar_init(struct comp_server *server,
										 struct comp_toplevel *toplevel);

void comp_titlebar_refresh_corner_radii(struct comp_titlebar *titlebar);

void comp_titlebar_calculate_bar_height(struct comp_titlebar *titlebar);

bool comp_titlebar_should_be_shown(struct comp_toplevel *toplevel);

void comp_titlebar_change_title(struct comp_titlebar *titlebar);

#endif // !FX_COMP_BORDER_TITLEBAR_H
