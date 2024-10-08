# FX-Comp

My compositor built primarily for testing [SceneFX](https://github.com/wlrfx/scenefx)

Use at your own discretion...

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
- [ ] foreign toplevel for ironbar support
- [x] Tiling
- [x] Lock support
- [ ] Pointer Constraint
- [x] Fullscreen
- [ ] Fullscreen titlebar on top hover
    - [ ] Delay
    - [ ] Animation
- [ ] Minimize
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

Thanks to Sway, Hyprland, and TinyWL for showing how stuff needs to be done! :)
