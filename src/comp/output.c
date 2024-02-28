#include <scenefx/types/wlr_scene.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <wlr/types/wlr_output.h>

#include "comp/output.h"

void output_frame(struct wl_listener *listener, void *data) {
	/* This function is called every time an output is ready to display a frame,
	 * generally at the output's refresh rate (e.g. 60Hz). */
	struct comp_output *output = wl_container_of(listener, output, frame);
	struct wlr_scene *scene = output->server->scene;

	struct wlr_scene_output *scene_output =
		wlr_scene_get_scene_output(scene, output->wlr_output);

	/* Render the scene if needed and commit the output */
	wlr_scene_output_commit(scene_output, NULL);

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(scene_output, &now);
}

void output_request_state(struct wl_listener *listener, void *data) {
	/* This function is called when the backend requests a new state for
	 * the output. For example, Wayland and X11 backends request a new mode
	 * when the output window is resized. */
	struct comp_output *output =
		wl_container_of(listener, output, request_state);

	const struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(output->wlr_output, event->state);

	// /* Set the background size to match the output */
	// if (output->server->background) {
	// 	float scale = output->wlr_output->scale;
	// 	int width, height;
	// 	wlr_output_transformed_resolution(output->wlr_output, &width, &height);
	// 	wlr_scene_rect_set_size(output->server->background, width / scale,
	// 							height / scale);
	// }
}

void output_destroy(struct wl_listener *listener, void *data) {
	struct comp_output *output = wl_container_of(listener, output, destroy);

	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);
	free(output);
}

static void output_get_identifier(char *identifier, size_t len,
								  struct comp_output *output) {
	struct wlr_output *wlr_output = output->wlr_output;
	snprintf(identifier, len, "%s %s %s",
			 wlr_output->make ? wlr_output->make : "Unknown",
			 wlr_output->model ? wlr_output->model : "Unknown",
			 wlr_output->serial ? wlr_output->serial : "Unknown");
}

static bool output_match_name_or_id(struct comp_output *output,
									const char *name_or_id) {
	if (strcmp(name_or_id, "*") == 0) {
		return true;
	}

	char identifier[128];
	output_get_identifier(identifier, sizeof(identifier), output);
	return strcasecmp(identifier, name_or_id) == 0 ||
		   strcasecmp(output->wlr_output->name, name_or_id) == 0;
}

struct comp_output *comp_output_by_name_or_id(const char *name_or_id) {
	struct comp_output *output;
	wl_list_for_each(output, &server.outputs, link) {
		if (output_match_name_or_id(output, name_or_id)) {
			return output;
		}
	}
	return NULL;
}
