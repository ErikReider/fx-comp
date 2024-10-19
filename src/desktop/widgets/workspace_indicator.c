#include <cairo.h>
#include <stdint.h>
#include <stdlib.h>
#include <wlr/util/log.h>

#include "comp/animation_mgr.h"
#include "comp/object.h"
#include "comp/output.h"
#include "comp/server.h"
#include "comp/widget.h"
#include "comp/workspace.h"
#include "constants.h"
#include "desktop/widgets/workspace_indicator.h"
#include "util.h"

static void set_visible(struct comp_ws_indicator *indicator, bool state) {
	indicator->visible = state;
	wlr_scene_node_set_enabled(&indicator->widget.object.scene_tree->node,
							   state);
}

static void indicator_destroy(struct comp_widget *widget) {
	struct comp_ws_indicator *indicator =
		wl_container_of(widget, indicator, widget);

	comp_animation_client_destroy(indicator->animation_client);

	wl_list_remove(&indicator->ws_change.link);

	pango_font_description_free(indicator->font);

	free(indicator);
}

static void indicator_draw(struct comp_widget *widget, cairo_t *cr, int width,
						   int height, float scale) {
	struct comp_ws_indicator *indicator =
		wl_container_of(widget, indicator, widget);
	struct comp_output *output = indicator->output;

	// Background
	cairo_set_rgba32(cr, &(const uint32_t){OVERLAY_COLOR_BACKGROUND});
	cairo_draw_rounded_rect(cr, width, height, 0, 0, EFFECTS_CORNER_RADII);
	cairo_fill(cr);

	int i = 0;
	struct comp_workspace *ws;
	wl_list_for_each_reverse(ws, &output->workspaces, output_link) {
		const int x_offset =
			OVERLAY_PADDING + (indicator->item_width + OVERLAY_PADDING) * i;

		uint32_t bg_color = WORKSPACE_SWITCHER_COLOR_FOCUSED_BACKGROUND;
		uint32_t fg_color = WORKSPACE_SWITCHER_COLOR_FOCUSED_FOREGROUND;
		if (ws == output->active_workspace) {
			bg_color = WORKSPACE_SWITCHER_COLOR_UNFOCUSED_BACKGROUND;
			fg_color = WORKSPACE_SWITCHER_COLOR_UNFOCUSED_FOREGROUND;
		}
		cairo_set_rgba32(cr, &bg_color);
		cairo_draw_rounded_rect(
			cr, indicator->item_width, indicator->item_height, x_offset,
			OVERLAY_PADDING, EFFECTS_CORNER_RADII - OVERLAY_PADDING);
		cairo_fill(cr);

		// text rendering
		cairo_save(cr);

		const int length = snprintf(NULL, 0, "%d", i + 1);
		char *str = malloc(length + 1);
		snprintf(str, length + 1, "%d", i + 1);

		// Set font
		PangoLayout *layout = pango_cairo_create_layout(cr);
		pango_layout_set_font_description(layout, indicator->font);
		pango_layout_set_text(layout, str, length);
		pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
		pango_layout_set_justify(layout, true);
		pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
		pango_layout_set_single_paragraph_mode(layout, true);
		pango_layout_set_wrap(layout, PANGO_WRAP_WORD);
		pango_layout_set_width(layout, indicator->item_width * PANGO_SCALE);

		int text_width, text_height;
		pango_layout_get_pixel_size(layout, &text_width, &text_height);

		// Center vertically
		cairo_move_to(cr, x_offset,
					  // Compensate for separator and border size
					  OVERLAY_PADDING +
						  (indicator->item_height - text_height) * 0.5);

		// Draw the text
		cairo_set_rgba32(cr, &fg_color);
		pango_cairo_show_layout(cr, layout);

		free(str);
		g_object_unref(layout);
		cairo_restore(cr);
		i++;
	}

	// Fade
	double alpha;
	switch (indicator->state) {
	case COMP_WS_INDICATOR_STATE_OPENING:
		alpha =
			lerp(0, 1, ease_out_cubic(indicator->animation_client->progress));
		break;
	case COMP_WS_INDICATOR_STATE_OPEN:
		alpha = 1;
		break;
	case COMP_WS_INDICATOR_STATE_CLOSING:
		alpha =
			lerp(1, 0, ease_out_cubic(indicator->animation_client->progress));
		break;
	}
	wlr_scene_buffer_set_opacity(indicator->widget.scene_buffer, alpha);
}

static void resize_and_draw(struct comp_ws_indicator *indicator) {
	// Match each items aspect ratio with the outputs aspect ratio
	struct comp_output *output = indicator->output;
	const float output_width = output->geometry.width;
	const float output_height = output->geometry.height;
	indicator->item_width = WORKSPACE_SWITCHER_ITEM_WIDTH;
	indicator->item_height = WORKSPACE_SWITCHER_ITEM_HEIGHT;
	if (output_width > output_height) {
		indicator->item_width *= output_width / output_height;
	} else if (output_width < output_height) {
		indicator->item_height *= output_height / output_width;
	}

	const int num_workspaces = wl_list_length(&output->workspaces);
	const int width = indicator->item_width * num_workspaces +
					  OVERLAY_PADDING * 2 +
					  OVERLAY_PADDING * (num_workspaces - 1);
	const int height = indicator->item_height + OVERLAY_PADDING * 2;
	comp_widget_damage_full(&indicator->widget);
	comp_widget_draw_resize(&indicator->widget, width, height);
}

static bool center(struct comp_widget *widget) {
	struct comp_ws_indicator *indicator =
		wl_container_of(widget, indicator, widget);
	if (!wl_list_empty(&indicator->output->workspaces)) {
		resize_and_draw(indicator);
	}

	// Return false to not override the default centering logic
	return false;
}

static const struct comp_widget_impl comp_ws_indicator_widget_impl = {
	.draw = indicator_draw,
	.destroy = indicator_destroy,
	.center = center,
};

static void indicator_ws_change(struct wl_listener *listener, void *data) {
	struct comp_ws_indicator *indicator =
		wl_container_of(listener, indicator, ws_change);

	if (!indicator->visible) {
		set_visible(indicator, true);
		indicator->state = COMP_WS_INDICATOR_STATE_OPENING;

		indicator->animation_client->duration_ms =
			WORKSPACE_SWITCHER_FADE_IN_MS;
	} else {
		indicator->force_update = true;
		indicator->state = COMP_WS_INDICATOR_STATE_OPEN;
		indicator->animation_client->duration_ms =
			WORKSPACE_SWITCHER_VISIBLE_MS;
	}

	comp_animation_client_add(server.animation_mgr, indicator->animation_client,
							  true);
}

static void animation_update(struct comp_animation_mgr *mgr,
							 struct comp_animation_client *client) {
	struct comp_ws_indicator *indicator = client->data;
	// Don't redraw when not animating (open state)
	if (!wl_list_empty(&indicator->output->workspaces) &&
		(indicator->state != COMP_WS_INDICATOR_STATE_OPEN ||
		 indicator->force_update)) {
		indicator->force_update = false;
		resize_and_draw(indicator);

		// Always center
		comp_widget_center_on_output(&indicator->widget, indicator->output);
	}
}

static void animation_done(struct comp_animation_mgr *mgr,
						   struct comp_animation_client *client,
						   bool cancelled) {
	struct comp_ws_indicator *indicator = client->data;

	indicator->force_update = true;

	switch (indicator->state) {
	case COMP_WS_INDICATOR_STATE_OPENING:
		indicator->state = COMP_WS_INDICATOR_STATE_OPEN;
		client->duration_ms = WORKSPACE_SWITCHER_VISIBLE_MS;
		comp_animation_client_add(server.animation_mgr,
								  indicator->animation_client, true);
		break;
	case COMP_WS_INDICATOR_STATE_OPEN:
		indicator->state = COMP_WS_INDICATOR_STATE_CLOSING;
		client->duration_ms = WORKSPACE_SWITCHER_FADE_OUT_MS;
		comp_animation_client_add(server.animation_mgr,
								  indicator->animation_client, true);
		break;
	case COMP_WS_INDICATOR_STATE_CLOSING:
		client->duration_ms = WORKSPACE_SWITCHER_FADE_IN_MS;
		set_visible(indicator, false);
		break;
	}
}

static const struct comp_animation_client_impl comp_animatino_client_impl = {
	.done = animation_done,
	.update = animation_update,
};

static bool handle_point_accepts_input(struct wlr_scene_buffer *buffer,
									   double *x, double *y) {
	return false;
}

struct comp_ws_indicator *comp_ws_indicator_init(struct comp_server *server,
												 struct comp_output *output) {
	struct comp_ws_indicator *indicator =
		calloc(1, sizeof(struct comp_ws_indicator));
	if (indicator == NULL) {
		wlr_log(WLR_ERROR, "Failed to allocate comp_titlebar");
		return NULL;
	}

	if (!comp_widget_init(&indicator->widget, server, &output->object,
						  output->layers.shell_overlay,
						  &comp_ws_indicator_widget_impl)) {
		free(indicator);
		return NULL;
	}

	indicator->animation_client = comp_animation_client_init(
		server->animation_mgr, WORKSPACE_SWITCHER_FADE_IN_MS,
		&comp_animatino_client_impl, indicator);

	indicator->item_width = WORKSPACE_SWITCHER_ITEM_WIDTH;
	indicator->item_height = WORKSPACE_SWITCHER_ITEM_HEIGHT;

	// Pango font config
	indicator->font = pango_font_description_new();
	pango_font_description_set_family(indicator->font, TITLEBAR_TEXT_FONT);
	pango_font_description_set_weight(indicator->font, PANGO_WEIGHT_BOLD);
	pango_font_description_set_absolute_size(indicator->font,
											 TITLEBAR_TEXT_SIZE * PANGO_SCALE);

	wlr_scene_node_set_enabled(&indicator->widget.scene_buffer->node, true);
	set_visible(indicator, false);

	wlr_scene_buffer_set_corner_radius(indicator->widget.scene_buffer,
									   EFFECTS_CORNER_RADII);
	indicator->widget.shadow_data.enabled = true;
	indicator->widget.shadow_data.color =
		wlr_render_color_from_color(&(const uint32_t){TOPLEVEL_SHADOW_COLOR});
	indicator->widget.shadow_data.blur_sigma = TOPLEVEL_SHADOW_BLUR_SIGMA;
	indicator->widget.shadow_data.offset_x = TOPLEVEL_SHADOW_X_OFFSET;
	indicator->widget.shadow_data.offset_y = TOPLEVEL_SHADOW_Y_OFFSET;
	wlr_scene_buffer_set_shadow_data(indicator->widget.scene_buffer,
									 indicator->widget.shadow_data);

	indicator->force_update = false;

	// Disable input
	indicator->widget.scene_buffer->point_accepts_input =
		handle_point_accepts_input;

	indicator->output = output;

	indicator->ws_change.notify = indicator_ws_change;
	wl_signal_add(&output->events.ws_change, &indicator->ws_change);

	return indicator;
}
