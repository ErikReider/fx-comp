#ifndef FX_COMP_ANIMATION_MGR_H
#define FX_COMP_ANIMATION_MGR_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wayland-util.h>

// TODO: Move into SceneFX
struct comp_animation_mgr {
	struct wl_event_source *tick;

	struct wl_list clients;
};

struct comp_animation_mgr *comp_animation_mgr_init(void);

void comp_animation_mgr_destroy(struct comp_animation_mgr *mgr);

struct comp_animation_client {
	struct wl_list link;

	// 0.0f -> 1.0f
	double progress;

	bool animating;

	// Duration in ms
	int duration_ms;

	bool inited;

	void *data;

	const struct comp_animation_client_impl *impl;
};

struct comp_animation_client_impl {
	void (*update)(struct comp_animation_mgr *mgr,
				   struct comp_animation_client *client);
	void (*done)(struct comp_animation_mgr *mgr,
				 struct comp_animation_client *client);
};

struct comp_animation_client *
comp_animation_client_init(struct comp_animation_mgr *mgr, int duration_ms,
						   const struct comp_animation_client_impl *impl,
						   void *data);

void comp_animation_client_remove(struct comp_animation_client *client);

void comp_animation_client_destroy(struct comp_animation_client *client);

void comp_animation_client_add(struct comp_animation_mgr *mgr,
							   struct comp_animation_client *client);

#endif // !FX_COMP_ANIMATION_MGR_H
