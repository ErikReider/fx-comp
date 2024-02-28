#ifndef FX_COMP_OUTPUT_H
#define FX_COMP_OUTPUT_H

#include <wayland-server-core.h>
#include <wayland-util.h>

#include "server.h"

struct comp_output {
	struct wl_list link;
	struct comp_server *server;
	struct wlr_output *wlr_output;
	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;
};

void output_frame(struct wl_listener *listener, void *data);

void output_request_state(struct wl_listener *listener, void *data);

void output_destroy(struct wl_listener *listener, void *data);

struct comp_output *comp_output_by_name_or_id(const char *name_or_id);

#endif // !FX_COMP_OUTPUT_H
