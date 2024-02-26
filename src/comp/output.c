#include <scenefx/types/wlr_scene.h>
#include <stdlib.h>
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
