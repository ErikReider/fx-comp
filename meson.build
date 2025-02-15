project(
	'fx-comp',
	'c',
	version: '0.1',
	meson_version: '>=0.60.0',
	default_options: [
		'c_std=c11',
		'warning_level=2',
		'werror=true',
	],
)

add_project_arguments(
	[
		'-DWLR_USE_UNSTABLE',

		'-Wno-unused-parameter',
		'-Wno-unused-result',
		'-Wno-missing-braces',
		'-Wno-format-zero-length',
		'-Wno-missing-field-initializers',
		'-Wundef',
		'-Wvla',
		'-Wlogical-op',
		'-Wmissing-include-dirs',
		'-Wold-style-definition',
		'-Wpointer-arith',
		'-Winit-self',
		'-Wstrict-prototypes',
		'-Wimplicit-fallthrough=2',
		'-Wendif-labels',
		'-Wstrict-aliasing=2',
		'-Woverflow',
		'-Wmissing-prototypes',
		'-Walloca',
	],
	language: 'c',
)

# Execute the wlroots subproject, if any
wlroots_options = [ 'examples=false' ]
wlroots_version = ['>=0.18.0', '<0.19.0']
subproject(
	'wlroots',
	default_options: wlroots_options,
	required: false,
	version: wlroots_version,
)
wlroots = dependency('wlroots-0.18',
	version: wlroots_version,
	default_options: wlroots_options,
)

# Execute the scenefx subproject, if any
scenefx_version = ['>=0.2.0']
subproject(
	'scenefx',
	required: false,
	version: scenefx_version,
)
scenefx = dependency('scenefx-0.2', version: scenefx_version)

cc = meson.get_compiler('c')

null_dep = dependency('', required: false)

jsonc = dependency('json-c', version: '>=0.13')
pcre2 = dependency('libpcre2-8')
wayland_server = dependency('wayland-server', version: '>=1.21.0')
wayland_client = dependency('wayland-client')
wayland_cursor = dependency('wayland-cursor')
wayland_protos = dependency('wayland-protocols', version: '>=1.24')
xkbcommon = dependency('xkbcommon', version: '>=1.5.0')
cairo = dependency('cairo')
pango = dependency('pango')
pangocairo = dependency('pangocairo')
gdk_pixbuf = dependency('gdk-pixbuf-2.0', required: true)
pixman = dependency('pixman-1')
libevdev = dependency('libevdev')
libinput = dependency('libinput', version: '>=1.26.0')
xcb = dependency('xcb', required: true)
drm = dependency('libdrm')
libudev = dependency('libudev')
math = cc.find_library('m')
rt = cc.find_library('rt')
xcb_icccm = dependency('xcb-icccm', required: true)
threads = dependency('threads') # for pthread_setschedparam
glib_version = '>=2.7'
glib = dependency('glib-2.0', version: glib_version, required: true)
gio = dependency('gio-2.0', version: glib_version, required: true)
gtk3 = dependency('gtk+-3.0', version: '>=3.24', required: true)

inc_dirs = include_directories('include')

subdir('protocols')
subdir('src')
