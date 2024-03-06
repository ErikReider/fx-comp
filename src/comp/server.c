#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/log.h>

#include "comp/output.h"
#include "comp/server.h"
#include "seat/cursor.h"

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

	wlr_output_manager_v1_set_configuration(server->output_manager,
											output_config);
}

static void
server_apply_output_config(struct comp_server *server,
						   struct wlr_output_configuration_v1 *output_config,
						   bool test) {
	// TODO: Check if HEADLESS output or not
	bool ok = true;

	struct wlr_output_configuration_head_v1 *head;
	wl_list_for_each(head, &output_config->heads, link) {
		struct wlr_output *output = head->state.output;
		struct comp_output *monitor = output->data;

		wlr_output_enable(output, head->state.enabled);
		if (head->state.enabled) {
			if (head->state.mode) {
				wlr_output_set_mode(output, head->state.mode);
			} else {
				wlr_output_set_custom_mode(output,
										   head->state.custom_mode.width,
										   head->state.custom_mode.height,
										   head->state.custom_mode.refresh);
			}

			if (monitor->geometry.x != head->state.x ||
				monitor->geometry.y != head->state.y) {
				wlr_output_layout_add(server->output_layout, output,
									  head->state.x, head->state.y);
			}
			wlr_output_set_transform(output, head->state.transform);
			wlr_output_set_scale(output, head->state.scale);
			wlr_xcursor_manager_load(server->cursor->cursor_mgr,
									 head->state.scale);
			wlr_output_enable_adaptive_sync(output,
											head->state.adaptive_sync_enabled);
		}

		if (test) {
			ok &= wlr_output_test(output);
			wlr_output_rollback(output);
		} else {
			ok &= wlr_output_commit(output);
		}
	}

	if (ok) {
		wlr_output_configuration_v1_send_succeeded(output_config);
	} else {
		wlr_output_configuration_v1_send_failed(output_config);
	}

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
