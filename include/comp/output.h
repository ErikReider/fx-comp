#ifndef FX_COMP_OUTPUT_H
#define FX_COMP_OUTPUT_H

#include <wayland-server-core.h>
#include <wayland-util.h>

#include "comp/object.h"
#include "comp/workspace.h"
#include "desktop/widgets/workspace_indicator.h"
#include "server.h"

struct comp_output {
	struct wl_list link;

	struct comp_server *server;
	struct wlr_output *wlr_output;
	struct wlr_scene_output *scene_output;

	// Geometry never set
	struct comp_object object;
	struct {
		struct wlr_scene_tree *shell_background; // TODO: Layershell
		struct wlr_scene_tree *shell_bottom;	 // TODO: Layershell
		struct wlr_scene_tree *workspaces;
		struct wlr_scene_tree *shell_top;	  // TODO: Layershell
		struct wlr_scene_tree *shell_overlay; // TODO: Layershell
		struct wlr_scene_tree *seat;		  // TODO: Drag and drop
		struct wlr_scene_tree *session_lock;  // TODO: session_lock
	} layers;

	struct comp_ws_indicator *ws_indicator;

	struct wl_list workspaces;
	struct comp_workspace *active_workspace;
	struct comp_workspace *prev_workspace;

	struct wlr_box usable_area;
	struct wlr_box geometry;

	uint32_t refresh_nsec;
	float refresh_sec;

	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener present;
	struct wl_listener destroy;

	// Custom output signals
	struct {
		struct wl_signal disable;
		struct wl_signal ws_change;
	} events;
};

/*
 * Util
 */

struct comp_workspace *comp_output_ws_from_index(struct comp_output *output,
												 size_t index);

int comp_output_find_ws_index(struct wl_list *list, struct comp_workspace *ws);

struct comp_workspace *comp_output_new_workspace(struct comp_output *output,
												 enum comp_workspace_type type);
void comp_output_remove_workspace(struct comp_output *output,
								  struct comp_workspace *ws);

struct comp_workspace *comp_output_get_active_ws(struct comp_output *output,
												 bool fullscreen);

struct comp_output *comp_output_by_name_or_id(const char *name_or_id);

/*
 * Main
 */

struct comp_output *comp_output_create(struct comp_server *server,
									   struct wlr_output *wlr_output);

void comp_new_output(struct wl_listener *listener, void *data);

void comp_output_disable(struct comp_output *output);

void comp_output_update_sizes(struct comp_output *output);

/**
 * Adds a already created comp_workspace. Moves it from another output if
 * needed
 */
void comp_output_move_workspace_to(struct comp_output *dest_output,
								   struct comp_workspace *ws);
void comp_output_focus_workspace(struct comp_output *output,
								 struct comp_workspace *ws);
struct comp_workspace *comp_output_prev_workspace(struct comp_output *output,
												  bool should_wrap);
struct comp_workspace *comp_output_next_workspace(struct comp_output *output,
												  bool should_wrap);

/*
 * Arrange functions
 */
void comp_output_arrange_output(struct comp_output *output);

void comp_output_arrange_layers(struct comp_output *output);

#endif // !FX_COMP_OUTPUT_H
