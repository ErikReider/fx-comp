sources = files(
	'main.c',
	'comp/server.c',
	'comp/output.c',
	'comp/view.c',
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
	wlroots,
	xkbcommon,
	xcb,
	xcb_icccm,
	scenefx,
]

# if have_xwayland
# 	sway_sources += 'desktop/xwayland.c'
# endif

# if wlroots_features['libinput_backend']
# 	sway_sources += 'input/libinput.c'
# endif

executable(
	'fx-comp',
	sources + wl_protos_src,
	include_directories: [inc_dirs],
	dependencies: deps,
	# link_with: [lib_sway_common],
	install: true
)
