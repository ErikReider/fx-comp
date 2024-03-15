sources = files(
	'main.c',
	'util.c',
	'comp/cairo_buffer.c',
	'comp/object.c',
	'comp/output.c',
	'comp/toplevel.c',
	'comp/server.c',
	'comp/widget.c',
	'comp/workspace.c',
	'desktop/layer_shell.c',
	'desktop/xdg.c',
	'desktop/xdg_decoration.c',
	'desktop/xdg_popup.c',
	'desktop/widgets/resize_edge.c',
	'desktop/widgets/titlebar.c',
	'seat/cursor.c',
	'seat/keyboard.c',
	'seat/seat.c',
)

deps = [
	cairo,
	drm,
	jsonc,
	libevdev,
	libinput,
	libudev,
	math,
	pango,
	pangocairo,
	pcre2,
	pixman,
	threads,
	wayland_server,
	scenefx, # IMPORTANT: SceneFX dep before wlroots
	wlroots,
	xkbcommon,
	xcb,
	xcb_icccm,
	glib,
	gio,
	gtk3,
]

executable(
	'fx-comp',
	sources + wl_protos_src,
	include_directories: [inc_dirs],
	dependencies: deps,
	install: true
)
