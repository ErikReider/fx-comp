#ifndef FX_COMP_WIDGETS_WORKSPACE_INDICATOR_H
#define FX_COMP_WIDGETS_WORKSPACE_INDICATOR_H

#include <pango/pango-font.h>
#include <stdbool.h>
#include <wayland-server-core.h>

#include "comp/animation_mgr.h"
#include "comp/widget.h"

#define TITLEBAR_NUM_BUTTONS 3

struct comp_ws_indicator {
	struct comp_widget widget;
	struct comp_output *output;

	struct comp_animation_client *animation_client;

	PangoFontDescription *font;

	bool force_update;
	bool visible;
	int active_ws_index;
	int num_workspaces;

	enum {
		COMP_WS_INDICATOR_STATE_OPENING,
		COMP_WS_INDICATOR_STATE_OPEN,
		COMP_WS_INDICATOR_STATE_CLOSING
	} state;

	struct wl_listener ws_change;
};

struct comp_ws_indicator *comp_ws_indicator_init(struct comp_server *server,
												 struct comp_output *output);

#endif // !FX_COMP_WIDGETS_WORKSPACE_INDICATOR_H
