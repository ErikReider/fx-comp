#include <wlr/backend.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_swapchain_manager.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/log.h>

#include "comp/lock.h"
#include "comp/output.h"
#include "comp/server.h"
#include "seat/cursor.h"
#include "seat/seat.h"

static void server_update_monitors(struct comp_server *server) {
	struct wlr_output_configuration_v1 *output_config =
		wlr_output_configuration_v1_create();

	// Remove all disabled outputs from wlr_output_layout
	struct comp_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (output->wlr_output->enabled || output == server->fallback_output) {
			continue;
		}

		if (output_config) {
			struct wlr_output_configuration_head_v1 *head =
				wlr_output_configuration_head_v1_create(output_config,
														output->wlr_output);
			if (head) {
				head->state.enabled = false;
			}
		}
		wlr_output_layout_remove(server->output_layout, output->wlr_output);
		comp_output_disable(output);
	}

	// Add non-existent outputs
	wl_list_for_each(output, &server->outputs, link) {
		if (output->wlr_output->enabled &&
			!wlr_output_layout_get(server->output_layout, output->wlr_output)) {
			wlr_output_layout_add_auto(server->output_layout,
									   output->wlr_output);
		}
	}

	wl_list_for_each(output, &server->outputs, link) {
		if (!output->wlr_output->enabled || output == server->fallback_output) {
			continue;
		}

		// TODO: Update toplevel positions/tiling/sizes
		comp_output_update_sizes(output);

		if (output_config) {
			struct wlr_output_configuration_head_v1 *head =
				wlr_output_configuration_head_v1_create(output_config,
														output->wlr_output);
			if (head) {
				head->state.enabled = true;
				head->state.mode = output->wlr_output->current_mode;
				head->state.x = output->geometry.x;
				head->state.y = output->geometry.y;
			}
		}
	}

	comp_session_lock_arrange();

	wlr_output_manager_v1_set_configuration(server->output_manager,
											output_config);
}

static bool
apply_resolved_output_configs(struct wlr_output_configuration_head_v1 *configs,
							  size_t configs_len, bool test) {
	struct wlr_backend_output_state *states =
		calloc(configs_len, sizeof(*states));
	if (!states) {
		return false;
	}

	wlr_log(WLR_DEBUG, "Committing %zd outputs", configs_len);
	for (size_t i = 0; i < configs_len; i++) {
		struct wlr_output_configuration_head_v1 *head = &configs[i];
		struct wlr_backend_output_state *backend_state = &states[i];

		struct wlr_output *output = head->state.output;
		struct comp_output *monitor = output->data;

		backend_state->output = head->state.output;
		wlr_output_state_init(&backend_state->base);

		struct wlr_output_state *state = &backend_state->base;

		// Skip fallback
		if (monitor == server.fallback_output) {
			continue;
		}

		wlr_log(WLR_DEBUG, "Preparing config for %s", head->state.output->name);
		wlr_output_state_set_enabled(state, head->state.enabled);
		if (head->state.enabled) {
			if (head->state.mode) {
				wlr_output_state_set_mode(state, head->state.mode);
			} else {
				wlr_output_state_set_custom_mode(
					state, head->state.custom_mode.width,
					head->state.custom_mode.height,
					head->state.custom_mode.refresh);
			}

			if (monitor->geometry.x != head->state.x ||
				monitor->geometry.y != head->state.y) {
				wlr_output_layout_add(server.output_layout, output,
									  head->state.x, head->state.y);
			}
			wlr_output_state_set_transform(state, head->state.transform);
			wlr_output_state_set_scale(state, head->state.scale);
			wlr_xcursor_manager_load(server.seat->cursor->cursor_mgr,
									 head->state.scale);
			wlr_output_state_set_adaptive_sync_enabled(
				state, head->state.adaptive_sync_enabled);
		}
	}

	struct wlr_output_swapchain_manager swapchain_mgr;
	wlr_output_swapchain_manager_init(&swapchain_mgr, server.backend);
	bool ok = wlr_output_swapchain_manager_prepare(&swapchain_mgr, states,
												   configs_len);

	if (!ok || test) {
		goto out;
	}

	for (size_t i = 0; i < configs_len; i++) {
		struct wlr_output_configuration_head_v1 *head = &configs[i];
		struct wlr_backend_output_state *backend_state = &states[i];

		struct wlr_output *output = head->state.output;
		struct comp_output *monitor = output->data;

		// Skip fallback
		if (monitor == server.fallback_output) {
			continue;
		}

		struct wlr_scene_output_state_options opts = {
			.swapchain = wlr_output_swapchain_manager_get_swapchain(
				&swapchain_mgr, output),
		};
		struct wlr_scene_output *scene_output = monitor->scene_output;
		struct wlr_output_state *state = &backend_state->base;
		if (!wlr_scene_output_build_state(scene_output, state, &opts)) {
			wlr_log(WLR_ERROR, "Building output state for '%s' failed",
					backend_state->output->name);
			goto out;
		}
	}

	ok = wlr_backend_commit(server.backend, states, configs_len);
	if (!ok) {
		wlr_log(WLR_ERROR, "Backend commit failed");
		goto out;
	}

	wlr_log(WLR_DEBUG, "Commit of %zd outputs succeeded", configs_len);

	wlr_output_swapchain_manager_apply(&swapchain_mgr);

out:
	wlr_output_swapchain_manager_finish(&swapchain_mgr);
	for (size_t i = 0; i < configs_len; i++) {
		struct wlr_backend_output_state *backend_state = &states[i];
		wlr_output_state_finish(&backend_state->base);
	}
	free(states);

	return ok;
}

static void
server_apply_output_config(struct comp_server *server,
						   struct wlr_output_configuration_v1 *output_config,
						   bool test) {
	bool ok = false;

	size_t configs_len = wl_list_length(&output_config->heads);
	struct wlr_output_configuration_head_v1 *configs =
		calloc(configs_len, sizeof(*configs));
	if (!configs) {
		goto done;
	}

	size_t i = 0;
	struct wlr_output_configuration_head_v1 *head;
	wl_list_for_each(head, &output_config->heads, link) {
		configs[i++] = *head;
	}

	ok = apply_resolved_output_configs(configs, configs_len, test);

	free(configs);

done:
	if (ok) {
		wlr_output_configuration_v1_send_succeeded(output_config);
	} else {
		wlr_output_configuration_v1_send_failed(output_config);
	}
	wlr_output_configuration_v1_destroy(output_config);

	server_update_monitors(server);
}

void comp_server_layout_change(struct wl_listener *listener, void *data) {
	struct comp_server *server =
		wl_container_of(listener, server, layout_change);

	server_update_monitors(server);
}

void comp_server_output_manager_apply(struct wl_listener *listener,
									  void *data) {
	struct comp_server *server =
		wl_container_of(listener, server, output_manager_apply);
	struct wlr_output_configuration_v1 *output_config = data;

	server_apply_output_config(server, output_config, false);
	wlr_output_configuration_v1_destroy(output_config);
}

void comp_server_output_manager_test(struct wl_listener *listener, void *data) {
	struct comp_server *server =
		wl_container_of(listener, server, output_manager_test);
	struct wlr_output_configuration_v1 *output_config = data;

	server_apply_output_config(server, output_config, true);
	wlr_output_configuration_v1_destroy(output_config);
}

struct comp_output *get_active_output(struct comp_server *server) {
	if (server->active_output) {
		return server->active_output;
	}

	struct comp_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (output->wlr_output->enabled) {
			return output;
		}
	}

	wlr_log(
		WLR_ERROR,
		"Could not get a active output! Falling back to HEADLESS output...\n"
		"Number of outputs: %i",
		wl_list_length(&server->outputs));
	return server->fallback_output;
}
