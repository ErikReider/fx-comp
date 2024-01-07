sources = files(
	'main.c',
	'comp/server.c',
	'comp/output.c',
	'comp/toplevel.c',
	'comp/seat.c',
	'desktop/xdg.c',
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
