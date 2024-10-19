#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <wayland-util.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/util/log.h>

#include "comp/animation_mgr.h"
#include "comp/lock.h"
#include "comp/object.h"
#include "comp/output.h"
#include "comp/server.h"
#include "constants.h"
#include "seat/seat.h"
#include "util.h"

static void reset_state(struct comp_session_lock *lock) {
	if (!wl_list_empty(&lock->outputs)) {
		struct comp_session_lock_output *lock_output, *tmp;
		wl_list_for_each_safe(lock_output, tmp, &lock->outputs, link) {
			// Destroying the node will in turn destroy the
			// comp_session_lock_output object
			wlr_scene_node_destroy(&lock_output->object.scene_tree->node);
		}
	}

	if (!lock->abandoned) {
		listener_remove(&lock->destroy);
		listener_remove(&lock->unlock);
		listener_remove(&lock->new_surface);
	}

	// lock->abandoned = false;
	lock->locked = false;
}

static void iter_scene_buffers_apply_effects(struct wlr_scene_buffer *buffer,
											 int sx, int sy, void *user_data) {
	struct comp_session_lock_output *l_output = user_data;

	wlr_scene_buffer_set_opacity(buffer, l_output->opacity);
}

static void mark_effects_dirty(struct comp_session_lock_output *l_output) {
	if (l_output->object.saved_tree) {
		wlr_scene_node_for_each_buffer(&l_output->object.saved_tree->node,
									   iter_scene_buffers_apply_effects,
									   l_output);
		return;
	}
	if (l_output->object.destroying) {
		wlr_log(WLR_DEBUG,
				"Skipping setting effects due to toplevel being destroyed");
		return;
	}

	wlr_scene_node_for_each_buffer(&l_output->object.content_tree->node,
								   iter_scene_buffers_apply_effects, l_output);
}

/* Fade Animation */

static void add_fade_animation(struct comp_session_lock_output *l_output,
							   float from, float to) {
	comp_animation_client_cancel(server.animation_mgr,
								 l_output->fade_animation.client);

	l_output->opacity = from;
	mark_effects_dirty(l_output);

	l_output->fade_animation.from = from;
	l_output->fade_animation.to = to;
	comp_animation_client_add(server.animation_mgr,
							  l_output->fade_animation.client, true);
}

static void fade_animation_update(struct comp_animation_mgr *mgr,
								  struct comp_animation_client *client) {
	struct comp_session_lock_output *l_output = client->data;

	l_output->opacity =
		lerp(l_output->fade_animation.from, l_output->fade_animation.to,
			 ease_out_cubic(client->progress));
	mark_effects_dirty(l_output);
}

static void fade_animation_done(struct comp_animation_mgr *mgr,
								struct comp_animation_client *client,
								bool cancelled) {
	struct comp_session_lock_output *l_output = client->data;
	// Continue destroying the toplevel
	if (l_output->object.destroying && !cancelled) {
		// Destroying the node will in turn destroy the
		// comp_session_lock_output object
		wlr_scene_node_destroy(&l_output->object.scene_tree->node);
		return;
	}

	// Fade in
	l_output->opacity = l_output->fade_animation.to;
	mark_effects_dirty(l_output);

	if (!cancelled) {
		wlr_scene_node_set_enabled(&l_output->background->node, true);
	}

	comp_output_arrange_output(l_output->output);
}

static const struct comp_animation_client_impl fade_animation_impl = {
	.done = fade_animation_done,
	.update = fade_animation_update,
};

/* Lock Output */

static void focus_surface(struct wlr_surface *focused) {
	struct comp_session_lock *lock = &server.comp_session_lock;
	lock->focused = focused;

	if (focused) {
		comp_seat_surface_focus(focused->data, focused);
	}
}

static void refocus_output(struct comp_session_lock_output *output) {
	struct comp_session_lock *lock = &server.comp_session_lock;

	// Move the seat focus to another surface if one is available
	if (lock->focused == output->surface->surface) {
		struct comp_session_lock_output *candidate;
		wl_list_for_each(candidate, &lock->outputs, link) {
			if (candidate == output || !candidate->surface) {
				continue;
			}

			if (candidate->surface->surface->mapped) {
				focus_surface(candidate->surface->surface);
				return;
			}
		}
	}
}

void comp_session_lock_refocus(void) {
	struct comp_session_lock *lock = &server.comp_session_lock;

	if (lock->focused && lock->focused->mapped) {
		focus_surface(lock->focused);
		return;
	}

	if (wl_list_empty(&lock->outputs)) {
		return;
	}

	struct comp_session_lock_output *candidate;
	wl_list_for_each(candidate, &lock->outputs, link) {
		if (!candidate->surface) {
			continue;
		}

		if (candidate->surface->surface->mapped) {
			focus_surface(candidate->surface->surface);
			return;
		}
	}
}

static void lock_output_reconfigure(struct comp_session_lock_output *output) {
	struct wlr_box output_box;
	wlr_output_layout_get_box(server.output_layout, output->output->wlr_output,
							  &output_box);
	wlr_scene_rect_set_size(output->background, output_box.width,
							output_box.height);

	if (output->surface) {
		wlr_session_lock_surface_v1_configure(output->surface, output_box.width,
											  output_box.height);
	}
}

static void lock_node_handle_destroy(struct wl_listener *listener, void *data) {
	struct comp_session_lock_output *output =
		wl_container_of(listener, output, destroy);
	if (output->surface) {
		refocus_output(output);
		listener_remove(&output->surface_destroy);
		listener_remove(&output->surface_map);
	}

	comp_animation_client_destroy(output->fade_animation.client);

	listener_remove(&output->destroy);
	wl_list_remove(&output->link);

	free(output);
}

void comp_session_lock_add_output(struct wlr_output *wlr_output) {
	struct comp_output *output = wlr_output->data;
	struct comp_session_lock *lock = &server.comp_session_lock;

	struct comp_session_lock_output *lock_output =
		calloc(1, sizeof(*lock_output));
	if (!lock_output) {
		wlr_log(WLR_ERROR, "Could not allocate session locked output");
		abort();
	}

	lock_output->object.scene_tree = alloc_tree(output->layers.session_lock);
	lock_output->object.content_tree =
		alloc_tree(lock_output->object.scene_tree);
	lock_output->output = output;
	lock_output->object.scene_tree->node.data = &lock_output->object;
	lock_output->object.data = lock_output;
	lock_output->object.type = COMP_OBJECT_TYPE_LOCK_OUTPUT;
	lock_output->object.destroying = false;

	lock_output->opacity = 0.0f;
	lock_output->fade_animation.client = comp_animation_client_init(
		server.animation_mgr, LOCK_ANIMATION_FADE_DURATION_MS,
		&fade_animation_impl, lock_output);

	if (!lock_output->object.scene_tree) {
		wlr_log(WLR_ERROR, "Could not allocate session scene trees");
		free(lock_output);
		abort();
	}

	// float color = lock->abandoned ? 1.0f : 0.0f;
	lock_output->background =
		wlr_scene_rect_create(lock_output->object.content_tree, 0, 0,
							  (float[4]){1.0f, 0.0f, 0.0f, 1.0f});
	if (!lock_output->background) {
		wlr_log(WLR_ERROR, "Could not allocate lock color fallback background");
		wlr_scene_node_destroy(&lock_output->object.scene_tree->node);
		free(lock_output);
		abort();
	}

	wlr_scene_node_set_enabled(&lock_output->background->node, lock->abandoned);

	add_fade_animation(lock_output, 0.0f, 1.0f);

	listener_init(&lock_output->destroy);
	listener_connect(&lock_output->object.scene_tree->node.events.destroy,
					 &lock_output->destroy, lock_node_handle_destroy);

	lock_output_reconfigure(lock_output);

	wl_list_insert(&lock->outputs, &lock_output->link);
}

/* Lock */

void comp_session_lock_arrange(void) {
	if (!server.comp_session_lock.locked) {
		return;
	}

	if (!wl_list_empty(&server.comp_session_lock.outputs)) {
		struct comp_session_lock_output *lock_output;
		wl_list_for_each(lock_output, &server.comp_session_lock.outputs, link) {
			lock_output_reconfigure(lock_output);
		}
	}
}

static void handle_surface_map(struct wl_listener *listener, void *data) {
	struct comp_session_lock_output *l_output =
		wl_container_of(listener, l_output, surface_map);
	struct comp_session_lock *lock = &server.comp_session_lock;

	if (lock->focused == NULL) {
		focus_surface(l_output->surface->surface);
	}
	mark_effects_dirty(l_output);
	// cursor_rebase_all();
}

static void handle_surface_destroy(struct wl_listener *listener, void *data) {
	struct comp_session_lock_output *output =
		wl_container_of(listener, output, surface_destroy);
	refocus_output(output);

	if (!output->surface) {
		wlr_log(WLR_ERROR, "Surface already destroyed??");
	}

	output->surface = NULL;
	listener_remove(&output->surface_destroy);
	listener_remove(&output->surface_map);
}

static void handle_new_surface(struct wl_listener *listener, void *data) {
	struct comp_session_lock *lock = &server.comp_session_lock;
	struct wlr_session_lock_surface_v1 *lock_surface = data;
	struct comp_output *output = lock_surface->output->data;

	wlr_log(WLR_DEBUG, "new lock layer surface");

	struct comp_session_lock_output *current_lock_output, *lock_output = NULL;
	wl_list_for_each(current_lock_output, &lock->outputs, link) {
		if (current_lock_output->output == output) {
			lock_output = current_lock_output;
			break;
		}
	}
	if (!lock_output) {
		wlr_log(WLR_ERROR, "No Output to lock (%i, %i)!",
				wl_list_length(&lock->outputs),
				// Removes fallback output
				wl_list_length(&server.outputs) - 1);
		abort();
	}
	if (lock_output->surface) {
		wlr_log(WLR_ERROR, "Tried to set surface for already locked output!");
		abort();
	}

	lock_output->surface = lock_surface;
	lock_output->surface->data = &lock_output->object.scene_tree;

	lock_output->surface->surface->data = &lock_output->object;
	wlr_scene_subsurface_tree_create(lock_output->object.content_tree,
									 lock_surface->surface);

	listener_init(&lock_output->surface_destroy);
	listener_connect(&lock_surface->events.destroy,
					 &lock_output->surface_destroy, handle_surface_destroy);

	listener_init(&lock_output->surface_map);
	listener_connect(&lock_surface->surface->events.map,
					 &lock_output->surface_map, handle_surface_map);

	lock_output_reconfigure(lock_output);
}

static void handle_unlock(struct wl_listener *listener, void *data) {
	struct comp_session_lock *lock = &server.comp_session_lock;
	wlr_log(WLR_DEBUG, "session unlocked");

	lock->abandoned = false;
	lock->locked = false;
	comp_seat_surface_unfocus(lock->focused, true);
	lock->focused = NULL;

	struct comp_session_lock_output *l_output;
	wl_list_for_each(l_output, &lock->outputs, link) {
		l_output->object.destroying = true;
		wlr_scene_node_set_enabled(&l_output->background->node, false);
		comp_object_save_buffer(&l_output->object);
		add_fade_animation(l_output, 1.0f, 0.0f);
	}

	// Triggers a refocus of the topmost surface layer if necessary
	// TODO: Make layer surface focus per-output based on cursor position
	struct comp_output *output;
	wl_list_for_each(output, &server.outputs, link) {
		if (output == server.fallback_output) {
			continue;
		}
		comp_output_arrange_layers(output);
	}
}

static void handle_abandon(struct wl_listener *listener, void *data) {
	struct comp_session_lock *lock = &server.comp_session_lock;
	wlr_log(WLR_DEBUG, "session lock abandoned");

	// TODO: Draw text telling the user that the lock has been abandoned
	struct comp_session_lock_output *lock_output;
	wl_list_for_each(lock_output, &lock->outputs, link) {
		wlr_scene_node_set_enabled(&lock_output->background->node, true);
	}

	lock->focused = NULL;
	// Only change the state if still locked. Fixes state being abandoned after
	// unlock
	if (lock->locked) {
		lock->abandoned = true;
	}
	listener_remove(&lock->destroy);
	listener_remove(&lock->unlock);
	listener_remove(&lock->new_surface);
}

/* Manager */

static void handle_session_lock(struct wl_listener *listener, void *data) {
	struct comp_session_lock *lock = &server.comp_session_lock;
	struct wlr_session_lock_v1 *wlr_session_lock = data;
	struct wl_client *client =
		wl_resource_get_client(wlr_session_lock->resource);

	if (lock->locked) {
		if (lock->abandoned) {
			reset_state(lock);
		} else {
			wlr_log(WLR_ERROR, "Cannot lock an already locked session");
			wlr_session_lock_v1_destroy(wlr_session_lock);
			return;
		}
	}

	wlr_log(WLR_DEBUG, "session locked");

	// Send unfocus event to focused clients
	comp_seat_unfocus_unless_client(client);

	struct comp_output *output;
	wl_list_for_each(output, &server.outputs, link) {
		if (output == server.fallback_output) {
			continue;
		}
		comp_session_lock_add_output(output->wlr_output);
	}

	listener_init(&lock->new_surface);
	listener_connect(&wlr_session_lock->events.new_surface, &lock->new_surface,
					 handle_new_surface);

	listener_init(&lock->unlock);
	listener_connect(&wlr_session_lock->events.unlock, &lock->unlock,
					 handle_unlock);

	listener_init(&lock->destroy);
	listener_connect(&wlr_session_lock->events.destroy, &lock->destroy,
					 handle_abandon);

	wlr_session_lock_v1_send_locked(wlr_session_lock);
	lock->locked = true;
}

static void handle_session_lock_destroy(struct wl_listener *listener,
										void *data) {
	if (server.comp_session_lock.locked) {
		reset_state(&server.comp_session_lock);
	}

	listener_remove(&server.comp_session_lock.new_lock);
	listener_remove(&server.comp_session_lock.manager_destroy);

	server.comp_session_lock.mgr = NULL;
}

void comp_session_lock_create(void) {
	wl_list_init(&server.comp_session_lock.outputs);

	server.comp_session_lock.mgr =
		wlr_session_lock_manager_v1_create(server.wl_display);

	listener_init(&server.comp_session_lock.new_lock);
	listener_connect(&server.comp_session_lock.mgr->events.new_lock,
					 &server.comp_session_lock.new_lock, handle_session_lock);

	listener_init(&server.comp_session_lock.manager_destroy);
	listener_connect(&server.comp_session_lock.mgr->events.destroy,
					 &server.comp_session_lock.manager_destroy,
					 handle_session_lock_destroy);
}
