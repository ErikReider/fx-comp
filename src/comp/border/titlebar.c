#define _XOPEN_SOURCE 600 // for M_PI

#include <cairo.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <scenefx/types/wlr_scene.h>
#include <stdio.h>
#include <stdlib.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include "comp/border/titlebar.h"
#include "comp/server.h"
#include "comp/toplevel.h"
#include "comp/widget.h"
#include "constants.h"
#include "util.h"

bool comp_titlebar_should_be_shown(struct comp_toplevel *toplevel) {
	if (toplevel->using_csd || toplevel->xdg_decoration == NULL) {
		return false;
	}
	return toplevel->titlebar &&
		   toplevel->titlebar->widget.scene_buffer->node.enabled;
}

static void titlebar_pointer_button(struct comp_widget *widget, double x,
									double y,
									struct wlr_pointer_button_event *event) {
	if (event->state != WLR_BUTTON_PRESSED) {
		return;
	}

	struct comp_titlebar *titlebar = wl_container_of(widget, titlebar, widget);

	// Check if the cursor is hovering over a button.
	// Call click handler if hovered.
	for (size_t i = 0; i < TITLEBAR_NUM_BUTTONS; i++) {
		struct comp_widget_click_region *button = titlebar->buttons.order[i];
		if (button->cursor_hovering && button->handle_click != NULL) {
			button->handle_click(widget, button);
			return;
		}
	}

	// Focus the titlebars toplevel
	struct comp_toplevel *toplevel = titlebar->toplevel;
	comp_toplevel_focus(toplevel, toplevel->xdg_toplevel->base->surface);

	comp_toplevel_begin_interactive(toplevel, COMP_CURSOR_MOVE, 0);
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
			button->cursor_hovering = true;
			should_redraw = true;
		} else if (button->cursor_hovering) {
			button->cursor_hovering = false;
			should_redraw = true;
		}
	}
	if (should_redraw) {
		comp_widget_draw(widget);
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
			should_redraw = true;
		}
		button->cursor_hovering = false;
	}
	if (should_redraw) {
		comp_widget_draw(widget);
	}
}

static void titlebar_draw(struct comp_widget *widget, cairo_t *cr,
						  int surface_width, int surface_height, float scale) {
	struct comp_titlebar *titlebar = wl_container_of(widget, titlebar, widget);
	struct comp_toplevel *toplevel = titlebar->toplevel;

	struct wlr_box geometry;
	wlr_xdg_surface_get_geometry(toplevel->xdg_toplevel->base, &geometry);

	const int toplevel_radius = toplevel->corner_radius;
	const int toplevel_x = BORDER_WIDTH;
	const int toplevel_y = TITLEBAR_HEIGHT;
	const int toplevel_width = geometry.width;
	const int toplevel_height = geometry.height;

	const int titlebar_radii = titlebar->widget.corner_radius;
	const int button_spacing = titlebar_radii;
	const int total_button_width =
		TITLEBAR_NUM_BUTTONS * (button_spacing + TITLEBAR_BUTTON_SIZE);

	int button_left_padding =
		titlebar->buttons.on_right
			? titlebar->widget.object.width - total_button_width
			: button_spacing;

	/*
	 * Colors
	 */

	float *background_color = (float[4])TITLEBAR_COLOR_BACKGROUND_FOCUSED;
	float *foreground_color = (float[4])TITLEBAR_COLOR_FOREGROUND_FOCUSED;
	float *border_color = (float[4])TITLEBAR_COLOR_BORDER_FOCUSED;
	if (!titlebar->toplevel->focused) {
		background_color = (float[4])TITLEBAR_COLOR_BACKGROUND_UNFOCUSED;
		foreground_color = (float[4])TITLEBAR_COLOR_FOREGROUND_UNFOCUSED;
		border_color = (float[4])TITLEBAR_COLOR_BORDER_UNFOCUSED;
	}

	/*
	 * Draw titlebar
	 */

	const int x = ceil((double)BORDER_WIDTH);
	const int y = ceil((double)BORDER_WIDTH);

	// Draw background
	if (!toplevel->using_csd) {
		cairo_set_source_rgba(cr, background_color[0], background_color[1],
							  background_color[2], background_color[3]);
		cairo_draw_rounded_rect(cr, surface_width, surface_height, 0, 0,
								titlebar_radii);
		cairo_close_path(cr);
		cairo_fill(cr);
	}

	// Draw whole perimeter border
	cairo_set_source_rgba(cr, border_color[0], border_color[1], border_color[2],
						  border_color[3]);
	cairo_draw_rounded_rect(cr, surface_width - x, surface_height - y, x * 0.5,
							y * 0.5,
							toplevel->corner_radius + BORDER_WIDTH * 0.5);
	cairo_set_line_width(cr, BORDER_WIDTH);
	cairo_stroke(cr);

	if (!toplevel->using_csd) {
		// Draw titlebar bottom border
		cairo_set_line_width(cr, BORDER_WIDTH);
		cairo_move_to(cr, toplevel_x, toplevel_y - BORDER_WIDTH * 0.5);
		cairo_line_to(cr, toplevel_x + toplevel_width,
					  toplevel_y - BORDER_WIDTH * 0.5);
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

		// TODO: Title

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
					 (TITLEBAR_BUTTON_SIZE + button_spacing) * i,
				.y = (TITLEBAR_HEIGHT - TITLEBAR_BUTTON_SIZE) / 2,
			};

			if (button->cursor_hovering) {
				cairo_set_source_rgba(cr, 1, 0, 0, 0.5);
			} else {
				cairo_set_source_rgba(cr, 1, 0, 0, 1);
			}

			cairo_rectangle(cr, button->region.x, button->region.y,
							button->region.width, button->region.height);
			cairo_fill(cr);
		}
		cairo_restore(cr);
	}
}

static void titlebar_destroy(struct comp_widget *widget) {
	struct comp_titlebar *titlebar = wl_container_of(widget, titlebar, widget);

	free(titlebar);
}

static const struct comp_widget_impl comp_titlebar_widget_impl = {
	.draw = titlebar_draw,
	.handle_pointer_enter = titlebar_pointer_enter,
	.handle_pointer_leave = titlebar_pointer_leave,
	.handle_pointer_motion = titlebar_pointer_motion,
	.handle_pointer_button = titlebar_pointer_button,
	.destroy = titlebar_destroy,
};

static void handle_close_click(struct comp_widget *widget,
							   struct comp_widget_click_region *region) {
	struct comp_titlebar *titlebar = wl_container_of(widget, titlebar, widget);
	wlr_xdg_toplevel_send_close(titlebar->toplevel->xdg_toplevel);
}

static void handle_fullscreen_click(struct comp_widget *widget,
									struct comp_widget_click_region *region) {
	struct comp_titlebar *titlebar = wl_container_of(widget, titlebar, widget);
	struct wlr_xdg_toplevel *xdg_toplevel = titlebar->toplevel->xdg_toplevel;
	wlr_xdg_toplevel_set_fullscreen(xdg_toplevel,
									!xdg_toplevel->current.fullscreen);
}

static void handle_minimize_click(struct comp_widget *widget,
								  struct comp_widget_click_region *region) {
	struct comp_titlebar *titlebar = wl_container_of(widget, titlebar, widget);
	// TODO: Add wlr_foreign_toplevel_handle_v1
}

struct comp_titlebar *comp_titlebar_init(struct comp_server *server,
										 struct comp_toplevel *toplevel) {
	struct comp_titlebar *titlebar = calloc(1, sizeof(struct comp_titlebar));
	if (titlebar == NULL) {
		wlr_log(WLR_ERROR, "Failed to allocate comp_titlebar");
		return NULL;
	}

	if (!comp_widget_init(&titlebar->widget, server, &toplevel->object,
						  &comp_titlebar_widget_impl)) {
		free(titlebar);
		return NULL;
	}

	wlr_scene_node_set_enabled(&titlebar->widget.scene_buffer->node, true);
	titlebar->toplevel = toplevel;

	// Set the titlebar decoration data
	titlebar->widget.opacity = 1;
	titlebar->widget.corner_radius = toplevel->corner_radius + BORDER_WIDTH;
	titlebar->widget.shadow_data = toplevel->shadow_data;

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

	return titlebar;
}
