# FX-Comp

My compositor built primarily for testing [SceneFX](https://github.com/wlrfx/scenefx).
Follows [SceneFX](https://github.com/wlrfx/scenefx) main branch and the latest
stable Wlroots release.

Use at your own discretion...

https://github.com/user-attachments/assets/74f2ad47-25a3-47a1-93c6-363e5fceae54

Running:

```sh
fx-comp -s ./fx-comp_cmd.sh
```

```sh
#!/bin/bash
# fx-comp_cmd.sh

waybar &
swaybg -i ~/Pictures/Your_cool_pic.jpg &
```

Todo:

- [X] Basic output support
- [x] [SceneFX](https://github.com/wlrfx/scenefx) integration
- [X] Per output workspaces
- [x] Layer shell
- [X] Titlebar
    - [X] SSD
    - [X] Only display borders on CSD toplevels
    - [X] Dynamic height
    - [X] Program title in center
    - [X] Button icons
- [x] Move/resize toplevels with MOD+mouse buttons
- [ ] ext_workspace_unstable_v1
- [x] Tiling
- [ ] Restrict modal toplevels to parent (whole toplevel can't be positioned outside of parent)
- [x] Lock support
- [x] Fullscreen
- [ ] Fullscreen titlebar on top hover
    - [ ] Delay
    - [ ] Animation
- [X] Minimize
- [X] XWayland
    - [X] Regular surfaces
    - [X] Popups
    - [X] Effects
    - [X] Handling of CSD
    - [X] Unmanaged
    - [X] Transient checks
    - [ ] XCursor theme support
- [X] Drag and drop support
- [ ] Tearing support
- [ ] Adaptive sync support
- [ ] WLR Portal support
- [X] Input configuration
- [ ] Config
    - [ ] Output
    - [ ] Hotkeys
    - [ ] programs on workspaces
    - [ ] start tiled
    - [ ] Exec applications
- [ ] Misc
    - [ ] Re creating the renderer after it's lost
    - [ ] VR support?
    - [ ] Hypr protocols support
    - [ ] Hyprcursor support
    - [ ] Keep toplevels inside of output region when resizing (percentage of w/h instead of px while resizing?)
    - [ ] rlimit_max
- [ ] Protocols (at least all that Sway supports)
    - [x] alpha-modifier-v1
    - [ ] wlr-virtual-pointer-unstable-v1
    - [ ] wlr_linux_drm_sync
    - [ ] linux-explicit-syncronization-unstable-v1
    - [x] wlr-output-power-managment
    - [ ] fractional scaling
    - [ ] tablet-v2
    - [ ] content-type-hint
    - [ ] cursor-shape-v1
    - [x] ext_foreign_toplevel_list_v1
    - [x] wlr_foreign_toplevel_manager
    - [ ] idle-notify
    - [ ] security-context
    - [ ] tearing
    - [ ] transient seat
    - [ ] xdg activation
    - [ ] idle inhibit
    - [ ] keyboard shortcuts inhibit
    - [x] Pointer constrints
    - [ ] Primary selection
    - [x] XDG foreign v1
    - [x] XDG foreign v2
    - [ ] Pointer gestures
    - [ ] Xwayland shell
    - [ ] XDG dialog
    - [ ] XDG toplevel Drag
    - [ ] XDG icon? (maybe just support and don't show it in decoration?)
    - [ ] Virtual Pointer
    - [ ] KDE blur?
    - [ ] xwp_input_method_manager_v2
    - [ ] xwp_text_input_manager_v2
    - [ ] xwp_virtual_keyboard_manager_v1

Thanks to Sway, Hyprland, and TinyWL for showing how stuff needs to be done! :)
