#include <cairo.h>
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

// TODO: Make configurable and adapt the width and height to the outputs aspect
// ratio
#define SIZE 80
#define PADDING 8

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

	const int ws_width = (width - PADDING * (indicator->num_workspaces + 1)) /
						 indicator->num_workspaces;
	const int ws_height = (height - PADDING * 2);

	// Background
	cairo_set_rgba32(cr,
					 &(const uint32_t){TITLEBAR_COLOR_BACKGROUND_UNFOCUSED});
	cairo_draw_rounded_rect(cr, width, height, 0, 0, EFFECTS_CORNER_RADII);
	cairo_fill(cr);

	for (int i = 0; i < indicator->num_workspaces; i++) {
		const int x_offset = PADDING + (ws_width + PADDING) * i;

		uint32_t bg_color = TITLEBAR_COLOR_BACKGROUND_FOCUSED;
		uint32_t fg_color = TITLEBAR_COLOR_FOREGROUND_FOCUSED;
		if (i == indicator->active_ws_index) {
			bg_color = TITLEBAR_COLOR_FOREGROUND_UNFOCUSED;
			fg_color = TITLEBAR_COLOR_BACKGROUND_UNFOCUSED;
		}
		cairo_set_rgba32(cr, &bg_color);
		cairo_draw_rounded_rect(cr, ws_width, ws_height, x_offset, PADDING,
								EFFECTS_CORNER_RADII - PADDING);
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
		pango_layout_set_width(layout, ws_width * PANGO_SCALE);

		int text_width, text_height;
		pango_layout_get_pixel_size(layout, &text_width, &text_height);

		// Center vertically
		cairo_move_to(cr, x_offset,
					  // Compensate for separator and border size
					  PADDING + (ws_height - text_height) * 0.5);

		// Draw the text
		cairo_set_rgba32(cr, &fg_color);
		pango_cairo_show_layout(cr, layout);

		free(str);
		g_object_unref(layout);
		cairo_restore(cr);
	}

	// Fade
	double alpha;
	switch (indicator->state) {
	case COMP_WS_INDICATOR_STATE_OPENING:
		alpha = lerp(
			0, 1, ease_out_cubic(fabs(indicator->animation_client->progress)));
		break;
	case COMP_WS_INDICATOR_STATE_OPEN:
		alpha = 1;
		break;
	case COMP_WS_INDICATOR_STATE_CLOSING:
		alpha = lerp(
			0, 1,
			ease_out_cubic(fabs(1 - indicator->animation_client->progress)));
		break;
	}
	wlr_scene_buffer_set_opacity(indicator->widget.scene_buffer, alpha);
}

static const struct comp_widget_impl comp_ws_indicator_widget_impl = {
	.draw = indicator_draw,
	.destroy = indicator_destroy,
};

static void indicator_ws_change(struct wl_listener *listener, void *data) {
	struct comp_ws_indicator *indicator =
		wl_container_of(listener, indicator, ws_change);
	struct comp_output *output = indicator->output;

	int length = 0;
	struct comp_workspace *pos_ws;
	wl_list_for_each_reverse(pos_ws, &output->workspaces, output_link) {
		if (pos_ws == output->active_workspace) {
			indicator->active_ws_index = length;
		}
		length++;
	}
	indicator->num_workspaces = length;

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

	comp_animation_client_add(server.animation_mgr,
							  indicator->animation_client);
}

static void animation_update(struct comp_animation_mgr *mgr,
							 struct comp_animation_client *client) {
	struct comp_ws_indicator *indicator = client->data;
	// Don't redraw when not animating (open state)
	if (indicator->num_workspaces > 0 &&
		(indicator->state != COMP_WS_INDICATOR_STATE_OPEN ||
		 indicator->force_update)) {
		indicator->force_update = false;
		const int width = SIZE * indicator->num_workspaces + PADDING * 2 +
						  PADDING * indicator->num_workspaces;
		const int height = SIZE + PADDING * 2;
		comp_widget_draw_resize(&indicator->widget, width, height);

		// Always center
		comp_widget_center_on_output(&indicator->widget, indicator->output);
	}
}
static void animation_done(struct comp_animation_mgr *mgr,
						   struct comp_animation_client *client) {
	struct comp_ws_indicator *indicator = client->data;

	indicator->force_update = true;

	switch (indicator->state) {
	case COMP_WS_INDICATOR_STATE_OPENING:
		indicator->state = COMP_WS_INDICATOR_STATE_OPEN;
		client->duration_ms = WORKSPACE_SWITCHER_VISIBLE_MS;
		comp_animation_client_add(server.animation_mgr,
								  indicator->animation_client);
		break;
	case COMP_WS_INDICATOR_STATE_OPEN:
		indicator->state = COMP_WS_INDICATOR_STATE_CLOSING;
		client->duration_ms = WORKSPACE_SWITCHER_FADE_OUT_MS;
		comp_animation_client_add(server.animation_mgr,
								  indicator->animation_client);
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
						  output->layers.shell_top,
						  &comp_ws_indicator_widget_impl)) {
		free(indicator);
		return NULL;
	}

	indicator->animation_client = comp_animation_client_init(
		server->animation_mgr, WORKSPACE_SWITCHER_FADE_IN_MS,
		&comp_animatino_client_impl, indicator);

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
