#define _XOPEN_SOURCE 600 // for M_PI

#include <assert.h>
#include <cairo.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <pango/pango-font.h>
#include <pango/pango-layout.h>
#include <pango/pangocairo.h>
#include <pixman.h>
#include <scenefx/types/wlr_scene.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include "comp/server.h"
#include "comp/widget.h"
#include "constants.h"
#include "desktop/toplevel.h"
#include "desktop/widgets/titlebar.h"
#include "seat/seat.h"
#include "util.h"

void comp_titlebar_refresh_corner_radii(struct comp_titlebar *titlebar) {
	titlebar->widget.corner_radius =
		titlebar->toplevel->corner_radius + BORDER_WIDTH * 0.5;
}

void comp_titlebar_change_title(struct comp_titlebar *titlebar) {
	if (comp_titlebar_should_be_shown(titlebar->toplevel)) {
		pixman_region32_union_rect(
			&titlebar->widget.damage, &titlebar->widget.damage, 0, BORDER_WIDTH,
			titlebar->widget.width, titlebar->bar_height);
		comp_widget_draw_damaged(&titlebar->widget);
	}
}

bool comp_titlebar_should_be_shown(struct comp_toplevel *toplevel) {
	if (toplevel->using_csd) {
		return false;
	}
	return toplevel->titlebar &&
		   toplevel->titlebar->widget.scene_buffer->node.enabled;
}

void comp_titlebar_calculate_bar_height(struct comp_titlebar *titlebar) {
	titlebar->bar_height = TITLEBAR_BUTTON_MARGIN * 2 + TITLEBAR_BUTTON_SIZE +
						   TITLEBAR_SEPARATOR_HEIGHT;
}

static void get_button_colors(enum comp_titlebar_button_type type,
							  uint32_t *focus_color, uint32_t *unfocus_color,
							  uint32_t *hover_color,
							  uint32_t *foreground_color) {
	switch (type) {
	case COMP_TITLEBAR_BUTTON_CLOSE:
		*focus_color = TITLEBAR_COLOR_BUTTON_CLOSE_FOCUSED;
		*unfocus_color = TITLEBAR_COLOR_BUTTON_CLOSE_UNFOCUSED;
		*hover_color = TITLEBAR_COLOR_BUTTON_CLOSE_HOVER;
		*foreground_color = TITLEBAR_COLOR_BUTTON_CLOSE_FOREGROUND;
		break;
	case COMP_TITLEBAR_BUTTON_FULLSCREEN:
		*focus_color = TITLEBAR_COLOR_BUTTON_FULLSCREEN_FOCUSED;
		*unfocus_color = TITLEBAR_COLOR_BUTTON_FULLSCREEN_UNFOCUSED;
		*hover_color = TITLEBAR_COLOR_BUTTON_FULLSCREEN_HOVER;
		*foreground_color = TITLEBAR_COLOR_BUTTON_FULLSCREEN_FOREGROUND;
		break;
	case COMP_TITLEBAR_BUTTON_MINIMIZE:
		*focus_color = TITLEBAR_COLOR_BUTTON_MINIMIZE_FOCUSED;
		*unfocus_color = TITLEBAR_COLOR_BUTTON_MINIMIZE_UNFOCUSED;
		*hover_color = TITLEBAR_COLOR_BUTTON_MINIMIZE_HOVER;
		*foreground_color = TITLEBAR_COLOR_BUTTON_MINIMIZE_FOREGROUND;
		break;
	}
}

static void get_button_props(enum comp_titlebar_button_type type,
							 char **icon_name, int *padding) {
	switch (type) {
	case COMP_TITLEBAR_BUTTON_CLOSE:
		*icon_name = TITLEBAR_BUTTON_CLOSE_ICON_NAME;
		*padding = TITLEBAR_BUTTON_CLOSE_ICON_PADDING;
		break;
	case COMP_TITLEBAR_BUTTON_FULLSCREEN:
		*icon_name = TITLEBAR_BUTTON_FULLSCREEN_ICON_NAME;
		*padding = TITLEBAR_BUTTON_FULLSCREEN_ICON_PADDING;
		break;
	case COMP_TITLEBAR_BUTTON_MINIMIZE:
		*icon_name = TITLEBAR_BUTTON_MINIMIZE_ICON_NAME;
		*padding = TITLEBAR_BUTTON_MINIMIZE_ICON_PADDING;
		break;
	}
}

static void get_bar_colors(bool is_focused, uint32_t *background_color,
						   uint32_t *foreground_color, uint32_t *border_color,
						   uint32_t *inner_border_color) {
	if (!is_focused) {
		*background_color = TITLEBAR_COLOR_BACKGROUND_UNFOCUSED;
		*foreground_color = TITLEBAR_COLOR_FOREGROUND_UNFOCUSED;
		*border_color = TITLEBAR_COLOR_BORDER_UNFOCUSED;
	} else {
		*background_color = TITLEBAR_COLOR_BACKGROUND_FOCUSED;
		*foreground_color = TITLEBAR_COLOR_FOREGROUND_FOCUSED;
		*border_color = TITLEBAR_COLOR_BORDER_FOCUSED;
	}
	*inner_border_color = TITLEBAR_COLOR_INNER_BORDER;
}

static void titlebar_pointer_button(struct comp_widget *widget, double x,
									double y,
									struct wlr_pointer_button_event *event) {
	if (event->button != BTN_LEFT) {
		return;
	}

	struct comp_titlebar *titlebar = wl_container_of(widget, titlebar, widget);
	struct comp_toplevel *toplevel = titlebar->toplevel;

	// Check if the cursor is hovering over a button
	struct comp_widget_click_region *hovered_button = NULL;
	for (size_t i = 0; i < TITLEBAR_NUM_BUTTONS; i++) {
		struct comp_widget_click_region *button = titlebar->buttons.order[i];
		if (button->cursor_hovering && button->handle_click != NULL) {
			hovered_button = button;
			break;
		}
	}

	switch (event->state) {
	case WLR_BUTTON_RELEASED:
		// Call click handler if hovered but only on click release
		if (hovered_button) {
			hovered_button->handle_click(widget, hovered_button);
		}
		break;
	case WLR_BUTTON_PRESSED:
		// Don't move the toplevel if we're pressing on a button
		if (hovered_button == NULL) {
			bool has_focus = server.seat->focused_toplevel == toplevel;
			if (!has_focus) {
				// Focus the titlebars toplevel
				comp_seat_surface_focus(
					&toplevel->object, comp_toplevel_get_wlr_surface(toplevel));
			}

			// Don't move tiled directly, require focus first
			if (toplevel->tiling_mode == COMP_TILING_MODE_FLOATING ||
				has_focus) {
				comp_toplevel_begin_interactive(toplevel, COMP_CURSOR_MOVE, 0);
			}
		}
		break;
	}
}

static void titlebar_pointer_motion(struct comp_widget *widget, double x,
									double y) {
	struct comp_titlebar *titlebar = wl_container_of(widget, titlebar, widget);

	// Only redraw the titlebar if the cursor just entered the vicinity of
	// one of the buttons or on leave
	bool should_redraw = false;
	for (size_t i = 0; i < TITLEBAR_NUM_BUTTONS; i++) {
		struct comp_widget_click_region *button = titlebar->buttons.order[i];
		if (wlr_box_contains_point(&button->region, x, y)) {
			if (button->cursor_hovering) {
				continue;
			}
			pixman_region32_union_rect(
				&widget->damage, &widget->damage, button->region.x,
				button->region.y, button->region.width, button->region.height);
			button->cursor_hovering = true;
			should_redraw = true;
		} else if (button->cursor_hovering) {
			pixman_region32_union_rect(
				&widget->damage, &widget->damage, button->region.x,
				button->region.y, button->region.width, button->region.height);
			button->cursor_hovering = false;
			should_redraw = true;
		}
	}
	if (should_redraw) {
		comp_widget_draw_damaged(widget);
	}
}

static void titlebar_pointer_enter(struct comp_widget *widget) {
}

static void titlebar_pointer_leave(struct comp_widget *widget) {
	struct comp_titlebar *titlebar = wl_container_of(widget, titlebar, widget);

	// Only redraw the titlebar if the cursor just left the vicinity of
	// one of the buttons
	bool should_redraw = false;
	for (size_t i = 0; i < TITLEBAR_NUM_BUTTONS; i++) {
		struct comp_widget_click_region *button = titlebar->buttons.order[i];
		if (button->cursor_hovering) {
			pixman_region32_union_rect(
				&widget->damage, &widget->damage, button->region.x,
				button->region.y, button->region.width, button->region.height);
			should_redraw = true;
		}
		button->cursor_hovering = false;
	}
	if (should_redraw) {
		comp_widget_draw_damaged(widget);
	}
}

static void titlebar_draw(struct comp_widget *widget, cairo_t *cr,
						  int surface_width, int surface_height, float scale) {
	struct comp_titlebar *titlebar = wl_container_of(widget, titlebar, widget);
	struct comp_toplevel *toplevel = titlebar->toplevel;

	const bool is_focused =
		comp_seat_object_is_focus(server.seat, &toplevel->object);

	const int TITLEBAR_HEIGHT = titlebar->bar_height + BORDER_WIDTH;

	const int toplevel_radius = toplevel->corner_radius;
	const double toplevel_x = BORDER_WIDTH;
	const int toplevel_y = TITLEBAR_HEIGHT;
	const int toplevel_width = toplevel->state.width;
	const int toplevel_height = toplevel->state.height;

	const int titlebar_radii = titlebar->widget.corner_radius;
	const int button_margin = titlebar_radii;
	const int total_button_width =
		((TITLEBAR_NUM_BUTTONS - 1) * TITLEBAR_BUTTON_SPACING) +
		(TITLEBAR_NUM_BUTTONS * TITLEBAR_BUTTON_SIZE);

	const int button_left_padding =
		titlebar->buttons.on_right
			? titlebar->widget.width - total_button_width - button_margin
			: button_margin;

	const int max_text_width =
		MAX(0, titlebar->widget.width -
				   (total_button_width + button_margin * 2) * 2);

	/*
	 * Colors
	 */

	uint32_t background_color;
	uint32_t foreground_color;
	uint32_t border_color;
	uint32_t inner_border_color;
	get_bar_colors(is_focused, &background_color, &foreground_color,
				   &border_color, &inner_border_color);

	/*
	 * Draw titlebar
	 */

	const double x = BORDER_WIDTH;
	const double y = BORDER_WIDTH;

	// Draw background
	if (!toplevel->using_csd) {
		cairo_set_rgba32(cr, &background_color);
		cairo_draw_rounded_rect(cr, surface_width - x * 2,
								surface_height - y * 2, x, y, titlebar_radii);
		cairo_close_path(cr);
		cairo_fill(cr);
	}

	// Draw whole perimeter border
	cairo_set_rgba32(cr, &border_color);
	cairo_draw_rounded_rect(cr, surface_width - x, surface_height - y, x * 0.5,
							y * 0.5, titlebar->widget.corner_radius);
	cairo_set_line_width(cr, BORDER_WIDTH);
	cairo_stroke(cr);

	if (!toplevel->using_csd) {
		// Draw titlebar separator
		cairo_set_line_width(cr, TITLEBAR_SEPARATOR_HEIGHT);
		cairo_move_to(cr, toplevel_x,
					  toplevel_y - TITLEBAR_SEPARATOR_HEIGHT * 0.5);
		cairo_line_to(cr, toplevel_x + toplevel_width,
					  toplevel_y - TITLEBAR_SEPARATOR_HEIGHT * 0.5);
		cairo_stroke(cr);

		/*
		 * Make the center transparent.
		 * Draws a semi rounded transparent region over the xdg_surfaces
		 * geometry region
		 */
		cairo_save(cr);
		cairo_operator_t operator= cairo_get_operator(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);

		// Top right
		cairo_move_to(cr, toplevel_x + toplevel_width, toplevel_y);
		// Bottom right
		cairo_arc(cr, toplevel_x + toplevel_width - toplevel_radius,
				  toplevel_y + toplevel_height - toplevel_radius,
				  toplevel_radius, M_PI / 180.0, M_PI * 0.5);
		// Bottom left
		cairo_arc(cr, toplevel_x + toplevel_radius,
				  toplevel_y + toplevel_height - toplevel_radius,
				  toplevel_radius, M_PI * 0.5, M_PI);
		// Top left
		cairo_line_to(cr, toplevel_x, toplevel_y);
		cairo_close_path(cr);

		cairo_clip(cr);
		cairo_set_source_rgba(cr, 0, 0, 0, 0);
		cairo_paint(cr);

		// Restore operator
		cairo_set_operator(cr, operator);
		cairo_restore(cr);

		/*
		 * Title
		 */

		// text rendering
		char *title = comp_toplevel_get_title(toplevel);
		if (title && max_text_width > 0) {
			cairo_save(cr);

			// Set font
			PangoLayout *layout = pango_cairo_create_layout(cr);
			pango_layout_set_font_description(layout, titlebar->font);
			pango_layout_set_text(layout, title, -1);
			pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
			pango_layout_set_justify(layout, true);
			pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
			pango_layout_set_single_paragraph_mode(layout, true);
			pango_layout_set_wrap(layout, PANGO_WRAP_WORD);
			pango_layout_set_width(layout, max_text_width * PANGO_SCALE);

			int text_width, text_height;
			pango_layout_get_pixel_size(layout, &text_width, &text_height);

			// Center vertically
			cairo_move_to(cr, total_button_width + button_margin * 2,
						  // Compensate for separator and border size
						  BORDER_WIDTH + (titlebar->bar_height - text_height -
										  TITLEBAR_SEPARATOR_HEIGHT) *
											 0.5);

			// Draw the text
			cairo_set_rgba32(cr, &foreground_color);
			pango_cairo_show_layout(cr, layout);

			g_object_unref(layout);
			cairo_restore(cr);
		}

		/*
		 * Titlebar buttons
		 */

		// Recalculate the titlebar button positions
		cairo_save(cr);
		for (size_t i = 0; i < TITLEBAR_NUM_BUTTONS; i++) {
			struct comp_widget_click_region *button =
				titlebar->buttons.order[i];
			button->region = (struct wlr_box){
				.width = TITLEBAR_BUTTON_SIZE,
				.height = TITLEBAR_BUTTON_SIZE,
				.x = button_left_padding +
					 (TITLEBAR_BUTTON_SIZE + TITLEBAR_BUTTON_SPACING) * i,
				.y = BORDER_WIDTH + TITLEBAR_BUTTON_MARGIN,
			};
			enum comp_titlebar_button_type type =
				*((enum comp_titlebar_button_type *)button->data);

			// Colors
			uint32_t focus_color;
			uint32_t unfocus_color;
			uint32_t hover_color;
			uint32_t foreground_color;
			get_button_colors(type, &focus_color, &unfocus_color, &hover_color,
							  &foreground_color);

			// Draw background
			if (button->cursor_hovering) {
				cairo_set_rgba32(cr, &hover_color);
			} else if (!is_focused) {
				cairo_set_rgba32(cr, &unfocus_color);
			} else {
				cairo_set_rgba32(cr, &focus_color);
			}
			assert(button->region.width == button->region.height);
			const int button_radius = button->region.width * 0.5;
			cairo_new_path(cr);
			cairo_arc(cr, button->region.x + button_radius,
					  button->region.y + button_radius, button_radius, 0,
					  2 * M_PI);
			cairo_close_path(cr);
			cairo_fill(cr);

			// Draw icon
			if (TITLEBAR_BUTTONS_ALWAYS_VISIBLE || button->cursor_hovering) {
				char *icon_name = NULL;
				int icon_padding;
				get_button_props(type, &icon_name, &icon_padding);
				if (icon_name == NULL) {
					continue;
				}
				int x = button->region.x + icon_padding;
				int y = button->region.y + icon_padding;
				int size = TITLEBAR_BUTTON_SIZE - icon_padding * 2;

				cairo_draw_icon_from_name(cr, icon_name, &foreground_color,
										  size, x, y, scale);
			}
		}
		cairo_restore(cr);
	}

	// Draw whole inner perimeter border
	cairo_set_rgba32(cr, &inner_border_color);
	cairo_draw_rounded_rect(cr, surface_width - x * 2, surface_height - y * 2,
							x, y, toplevel->corner_radius);
	cairo_set_line_width(cr, INNER_BORDER_WIDTH);
	cairo_stroke(cr);
}

static bool titlebar_handle_accepts_input(struct comp_widget *widget,
										  struct wlr_scene_buffer *buffer,
										  double *x, double *y) {
	struct comp_titlebar *titlebar = wl_container_of(widget, titlebar, widget);
	struct comp_toplevel *toplevel = titlebar->toplevel;

	// Disable input if the toplevel requires cursor constraint
	struct wlr_pointer_constraint_v1 *constraint =
		server.seat->cursor->active_constraint;
	if (constraint) {
		struct comp_toplevel *constraint_toplevel =
			comp_toplevel_from_wlr_surface(constraint->surface);
		if (constraint_toplevel == toplevel) {
			return false;
		}
	}

	double titlebar_height = BORDER_WIDTH;
	if (!toplevel->using_csd) {
		titlebar_height += titlebar->bar_height;
	}

	// Don't accept input if the pointer is over the toplevel surface
	pixman_region32_t region;
	pixman_region32_init_rect(&region, BORDER_WIDTH, titlebar_height,
							  toplevel->state.width, toplevel->state.height);
	bool contains = pixman_region32_contains_point(&region, *x, *y, NULL);
	pixman_region32_fini(&region);
	return !contains;
}

static void titlebar_destroy(struct comp_widget *widget) {
	struct comp_titlebar *titlebar = wl_container_of(widget, titlebar, widget);
	titlebar->toplevel->titlebar = NULL;

	free(titlebar->buttons.close.data);
	free(titlebar->buttons.fullscreen.data);
	free(titlebar->buttons.minimize.data);

	listener_remove(&titlebar->output_enter);
	listener_remove(&titlebar->output_leave);

	pango_font_description_free(titlebar->font);
	free(titlebar);
}

static const struct comp_widget_impl comp_titlebar_widget_impl = {
	.draw = titlebar_draw,
	.handle_pointer_enter = titlebar_pointer_enter,
	.handle_pointer_leave = titlebar_pointer_leave,
	.handle_pointer_motion = titlebar_pointer_motion,
	.handle_pointer_button = titlebar_pointer_button,
	.handle_point_accepts_input = titlebar_handle_accepts_input,
	.destroy = titlebar_destroy,
};

static void handle_close_click(struct comp_widget *widget,
							   struct comp_widget_click_region *region) {
	struct comp_titlebar *titlebar = wl_container_of(widget, titlebar, widget);
	comp_toplevel_close(titlebar->toplevel);
}

static void handle_fullscreen_click(struct comp_widget *widget,
									struct comp_widget_click_region *region) {
	struct comp_titlebar *titlebar = wl_container_of(widget, titlebar, widget);
	comp_toplevel_toggle_fullscreen(titlebar->toplevel);
}

static void handle_minimize_click(struct comp_widget *widget,
								  struct comp_widget_click_region *region) {
	struct comp_titlebar *titlebar = wl_container_of(widget, titlebar, widget);
	comp_toplevel_toggle_minimized(titlebar->toplevel);
}

static void handle_output_enter(struct wl_listener *listener, void *data) {
	struct comp_titlebar *titlebar =
		wl_container_of(listener, titlebar, output_enter);
	if (!titlebar->toplevel) {
		wlr_log(WLR_ERROR, "Titlebar doesn't have a Toplevel!");
		return;
	}

	if (titlebar->toplevel->wlr_foreign_toplevel) {
		struct wlr_scene_output *output = data;
		wlr_foreign_toplevel_handle_v1_output_enter(
			titlebar->toplevel->wlr_foreign_toplevel, output->output);
	}
}

static void handle_output_leave(struct wl_listener *listener, void *data) {
	struct comp_titlebar *titlebar =
		wl_container_of(listener, titlebar, output_leave);
	if (!titlebar->toplevel) {
		wlr_log(WLR_ERROR, "Titlebar doesn't have a Toplevel!");
		return;
	}

	if (titlebar->toplevel->wlr_foreign_toplevel) {
		struct wlr_scene_output *output = data;
		wlr_foreign_toplevel_handle_v1_output_leave(
			titlebar->toplevel->wlr_foreign_toplevel, output->output);
	}
}

struct comp_titlebar *comp_titlebar_init(struct comp_server *server,
										 struct comp_toplevel *toplevel) {
	struct comp_titlebar *titlebar = calloc(1, sizeof(struct comp_titlebar));
	if (!titlebar) {
		wlr_log(WLR_ERROR, "Failed to allocate comp_titlebar");
		return NULL;
	}

	struct shadow_data shadow_data = {
		.color = wlr_render_color_from_color(
			&(const uint32_t){TOPLEVEL_SHADOW_COLOR}),
		.blur_sigma = TOPLEVEL_SHADOW_BLUR_SIGMA,
		.offset_x = TOPLEVEL_SHADOW_X_OFFSET,
		.offset_y = TOPLEVEL_SHADOW_Y_OFFSET,
	};

	if (!comp_widget_init(&titlebar->widget, server, &toplevel->object,
						  toplevel->decoration_scene_tree, shadow_data,
						  &comp_titlebar_widget_impl)) {
		free(titlebar);
		return NULL;
	}

	wlr_scene_node_set_enabled(&titlebar->widget.scene_buffer->node, true);
	titlebar->toplevel = toplevel;

	// Make the decorations the output enter/leave event holders
	// TODO: Will this get all messed up when titlebar is disabled due to the
	// toplevel being fullscreen?
	listener_connect_init(&titlebar->widget.scene_buffer->events.output_enter,
						  &titlebar->output_enter, handle_output_enter);
	listener_connect_init(&titlebar->widget.scene_buffer->events.output_leave,
						  &titlebar->output_leave, handle_output_leave);

	comp_titlebar_calculate_bar_height(titlebar);

	// Pango font config
	titlebar->font = pango_font_description_new();
	pango_font_description_set_family(titlebar->font, TITLEBAR_TEXT_FONT);
	pango_font_description_set_weight(titlebar->font, PANGO_WEIGHT_BOLD);
	pango_font_description_set_absolute_size(titlebar->font,
											 TITLEBAR_TEXT_SIZE * PANGO_SCALE);

	// Set the titlebar decoration data
	comp_titlebar_refresh_corner_radii(titlebar);
	if (toplevel->corner_radius == 0) {
		titlebar->widget.corner_radius = 0;
	}

	/*
	 * Buttons
	 */

	// Setup button positions
	titlebar->buttons.on_right = TITLEBAR_BUTTONS_ON_RIGHT;
	// Change button position depending on which side the buttons
	// are on to match a certain fruit-based OS
	if (titlebar->buttons.on_right) {
		titlebar->buttons.order[0] = &titlebar->buttons.minimize;
		titlebar->buttons.order[1] = &titlebar->buttons.fullscreen;
		titlebar->buttons.order[2] = &titlebar->buttons.close;
	} else {
		titlebar->buttons.order[0] = &titlebar->buttons.close;
		titlebar->buttons.order[1] = &titlebar->buttons.minimize;
		titlebar->buttons.order[2] = &titlebar->buttons.fullscreen;
	}

	// Setup button callbacks
	titlebar->buttons.close.handle_click = handle_close_click;
	titlebar->buttons.fullscreen.handle_click = handle_fullscreen_click;
	titlebar->buttons.minimize.handle_click = handle_minimize_click;

	// Close
	titlebar->buttons.close.data = malloc(sizeof(int));
	if (!titlebar->buttons.close.data) {
		wlr_log(WLR_ERROR, "Could not allocate titlebar close button data");
		goto malloc_close_button_err;
	}
	*((int *)titlebar->buttons.close.data) = COMP_TITLEBAR_BUTTON_CLOSE;
	// Fullscreen
	titlebar->buttons.fullscreen.data = malloc(sizeof(int));
	if (!titlebar->buttons.fullscreen.data) {
		wlr_log(WLR_ERROR,
				"Could not allocate titlebar fullscreen button data");
		goto malloc_fullscreen_button_err;
	}
	*((int *)titlebar->buttons.fullscreen.data) =
		COMP_TITLEBAR_BUTTON_FULLSCREEN;
	// Minimize
	titlebar->buttons.minimize.data = malloc(sizeof(int));
	if (!titlebar->buttons.minimize.data) {
		wlr_log(WLR_ERROR, "Could not allocate titlebar minimize button data");
		goto malloc_minimize_button_err;
	}
	*((int *)titlebar->buttons.minimize.data) = COMP_TITLEBAR_BUTTON_MINIMIZE;

	return titlebar;

malloc_minimize_button_err:
	free(titlebar->buttons.fullscreen.data);
malloc_fullscreen_button_err:
	free(titlebar->buttons.close.data);
malloc_close_button_err:
	free(titlebar);
	return NULL;
}
