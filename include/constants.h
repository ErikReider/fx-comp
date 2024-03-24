#ifndef FX_COMP_CONSTANTS
#define FX_COMP_CONSTANTS

#define NSEC_IN_SECONDS (long)1000000000

#define HEADLESS_FALLBACK_OUTPUT_WIDTH 800
#define HEADLESS_FALLBACK_OUTPUT_HEIGHT 600

#define EFFECTS_CORNER_RADII 14

/*
 * Overlays
 */

#define OVERLAY_COLOR_BACKGROUND 0x242424CC
#define OVERLAY_COLOR_FOREGROUND 0xFFFFFFFF
#define OVERLAY_PADDING 8

/*
 * Workspace Switcher
 */

#define WORKSPACE_SWITCHER_COLOR_FOCUSED_BACKGROUND 0x303030FF
#define WORKSPACE_SWITCHER_COLOR_FOCUSED_FOREGROUND 0xFFFFFFFF
#define WORKSPACE_SWITCHER_COLOR_UNFOCUSED_BACKGROUND 0x888888FF
#define WORKSPACE_SWITCHER_COLOR_UNFOCUSED_FOREGROUND 0x202020FF
#define WORKSPACE_SWITCHER_VISIBLE_MS 1000
#define WORKSPACE_SWITCHER_FADE_IN_MS 50
#define WORKSPACE_SWITCHER_FADE_OUT_MS 150
#define WORKSPACE_SWITCHER_ITEM_WIDTH 100
#define WORKSPACE_SWITCHER_ITEM_HEIGHT 100

/*
 * Toplevel
 */

#define TOPLEVEL_NON_MAIN_OUTPUT_OPACITY 0.4
#define TOPLEVEL_SHADOW_COLOR 0x00000080
#define TOPLEVEL_SHADOW_BLUR_SIGMA 30

/*
 * Titlebar
 */

#define TITLEBAR_COLOR_BACKGROUND_FOCUSED 0x303030FF
#define TITLEBAR_COLOR_BACKGROUND_UNFOCUSED 0x202020FF
#define TITLEBAR_COLOR_FOREGROUND_FOCUSED 0xffffffFF
#define TITLEBAR_COLOR_FOREGROUND_UNFOCUSED 0x888888FF
#define TITLEBAR_COLOR_BORDER_FOCUSED 0x5d5d5dFF
#define TITLEBAR_COLOR_BORDER_UNFOCUSED 0x4b4b4bFF

// Close Button
#define TITLEBAR_COLOR_BUTTON_CLOSE_FOCUSED 0xe34e4bFF
#define TITLEBAR_COLOR_BUTTON_CLOSE_UNFOCUSED 0x525252FF
#define TITLEBAR_COLOR_BUTTON_CLOSE_HOVER 0xe34e4bFF
#define TITLEBAR_COLOR_BUTTON_CLOSE_FOREGROUND 0x710407FF
#define TITLEBAR_BUTTON_CLOSE_ICON_NAME "window-close-symbolic"
#define TITLEBAR_BUTTON_CLOSE_ICON_PADDING 2
// Fullscreen Button
#define TITLEBAR_COLOR_BUTTON_FULLSCREEN_FOCUSED 0x2eb341FF
#define TITLEBAR_COLOR_BUTTON_FULLSCREEN_UNFOCUSED 0x525252FF
#define TITLEBAR_COLOR_BUTTON_FULLSCREEN_HOVER 0x2eb341FF
#define TITLEBAR_COLOR_BUTTON_FULLSCREEN_FOREGROUND 0x11581fFF
#define TITLEBAR_BUTTON_FULLSCREEN_ICON_NAME "window-maximize-symbolic"
#define TITLEBAR_BUTTON_FULLSCREEN_ICON_PADDING 2
// Minimize Button
#define TITLEBAR_COLOR_BUTTON_MINIMIZE_FOCUSED 0xe4a93aFF
#define TITLEBAR_COLOR_BUTTON_MINIMIZE_UNFOCUSED 0x525252FF
#define TITLEBAR_COLOR_BUTTON_MINIMIZE_HOVER 0xe4a93aFF
#define TITLEBAR_COLOR_BUTTON_MINIMIZE_FOREGROUND 0x894e10FF
#define TITLEBAR_BUTTON_MINIMIZE_ICON_NAME "window-minimize-symbolic"
#define TITLEBAR_BUTTON_MINIMIZE_ICON_PADDING 2

#define TITLEBAR_BUTTON_SIZE 14
#define TITLEBAR_BUTTON_BORDER_THICKNESS 1
#define TITLEBAR_BUTTON_MARGIN 6
#define TITLEBAR_BUTTON_SPACING 12
#define TITLEBAR_BUTTONS_ON_RIGHT false
#define TITLEBAR_BUTTONS_ALWAYS_VISIBLE false
#define TITLEBAR_SEPARATOR_HEIGHT 1
#define TITLEBAR_TEXT_ELLIPSE_STRING "…"
#define TITLEBAR_TEXT_SIZE 14
#define TITLEBAR_TEXT_FONT "Clear Sans"

#define BORDER_WIDTH 1
#define BORDER_RESIZE_WIDTH 10

#define FLOATING_MOD WLR_MODIFIER_ALT

#endif // !FX_COMP_CONSTANTS
