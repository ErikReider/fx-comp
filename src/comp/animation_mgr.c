#include <assert.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <wayland-util.h>
#include <wlr/util/log.h>

#include "comp/animation_mgr.h"
#include "comp/output.h"
#include "comp/server.h"

#define MIN_DURATION 100

static void animation_mgr_run(struct comp_animation_mgr *mgr);

/*
 * Animation Client
 */

struct comp_animation_client *
comp_animation_client_init(struct comp_animation_mgr *mgr, int duration_ms,
						   const struct comp_animation_client_impl *impl,
						   void *data) {
	struct comp_animation_client *client = calloc(1, sizeof(*client));
	if (!client) {
		wlr_log(WLR_ERROR, "Failed to allocate comp_animation_mgr");
		return NULL;
	}

	client->duration_ms = duration_ms;
	client->inited = false;
	client->progress = 0.0;
	client->state = ANIMATION_STATE_NONE;
	client->impl = impl;
	client->data = data;

	return client;
}

void comp_animation_client_remove(struct comp_animation_client *client) {
	if (client->inited) {
		if (client->state == ANIMATION_STATE_RUNNING) {
			wl_list_remove(&client->link);
		}
		client->inited = false;
	}

	client->state = ANIMATION_STATE_NONE;
}

static void done(struct comp_animation_mgr *mgr,
				 struct comp_animation_client *client, bool cancel) {
	comp_animation_client_remove(client);

	client->progress = 1.0;
	if (client->impl->done) {
		client->impl->done(mgr, client, cancel);
	}
}

void comp_animation_client_cancel(struct comp_animation_mgr *mgr,
								  struct comp_animation_client *client) {
	done(mgr, client, true);
}

void comp_animation_client_destroy(struct comp_animation_client *client) {
	comp_animation_client_remove(client);
	free(client);
}

void comp_animation_client_add(struct comp_animation_mgr *mgr,
							   struct comp_animation_client *client,
							   bool run_now) {
	comp_animation_client_remove(client);
	client->inited = true;
	client->state = ANIMATION_STATE_WAITING;

	if (run_now) {
		comp_animation_client_start(mgr, client);
	}
}

void comp_animation_client_start(struct comp_animation_mgr *mgr,
								 struct comp_animation_client *client) {
	if (!client->inited) {
		comp_animation_client_add(mgr, client, false);
	}
	client->state = ANIMATION_STATE_RUNNING;

	client->progress = 0.0;
	wl_list_insert(&mgr->clients, &client->link);

	if (client->duration_ms < MIN_DURATION) {
		client->state = ANIMATION_STATE_RUNNING;
		client->progress = 1.0;
		if (client->impl->update) {
			client->impl->update(mgr, client);
		}
		done(mgr, client, false);
		return;
	}

	// Run now
	animation_mgr_run(mgr);
}

/*
 * Animation Manager
 */

static float get_fastest_output_refresh_s(void) {
	float fastest_output_refresh_s = 0.0166667; // fallback to 60 Hz
	struct comp_output *output;
	wl_list_for_each_reverse(output, &server.outputs, link) {
		if (output != server.fallback_output && output->refresh_nsec > 0) {
			fastest_output_refresh_s =
				MIN(fastest_output_refresh_s, output->refresh_sec);
		}
	}
	return fastest_output_refresh_s;
}

static int animation_timer(void *data) {
	struct comp_animation_mgr *mgr = data;
	const float fastest_output_refresh_s =
		get_fastest_output_refresh_s() * 1000;

	struct comp_animation_client *client, *tmp;
	wl_list_for_each_reverse_safe(client, tmp, &mgr->clients, link) {
		client->progress += fastest_output_refresh_s / client->duration_ms;

		if (client->impl->update) {
			client->impl->update(mgr, client);
		}

		if (client->progress >= 1.0) {
			client->progress = 1.0;
			done(mgr, client, false);
		}
	}

	if (!wl_list_empty(&mgr->clients)) {
		wl_event_source_timer_update(mgr->tick, fastest_output_refresh_s);
	}

	return 0;
}

static void animation_mgr_run(struct comp_animation_mgr *mgr) {
	animation_timer(mgr);
}

struct comp_animation_mgr *comp_animation_mgr_init(void) {
	struct comp_animation_mgr *mgr = calloc(1, sizeof(*mgr));
	if (!mgr) {
		wlr_log(WLR_ERROR, "Failed to allocate comp_animation_mgr");
		return NULL;
	}

	mgr->tick =
		wl_event_loop_add_timer(server.wl_event_loop, animation_timer, mgr);

	wl_list_init(&mgr->clients);

	wl_event_source_timer_update(mgr->tick, 1);
	return mgr;
}

void comp_animation_mgr_destroy(struct comp_animation_mgr *mgr) {
	wl_event_source_remove(mgr->tick);

	free(mgr);
}
