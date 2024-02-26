sources = files(
	'main.c',
	'util.c',
	'comp/border/edge.c',
	'comp/border/titlebar.c',
	'comp/cairo_buffer.c',
	'comp/object.c',
	'comp/output.c',
	'comp/toplevel.c',
	'comp/seat.c',
	'comp/server.c',
	'comp/widget.c',
	'desktop/xdg.c',
	'desktop/xdg_decoration.c',
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
]

executable(
	'fx-comp',
	sources + wl_protos_src,
	include_directories: [inc_dirs],
	dependencies: deps,
	install: true
)
