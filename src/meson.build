sources = files(
	'main.c',
	'util.c',
)

subdir('comp')
subdir('desktop')
subdir('seat')

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
