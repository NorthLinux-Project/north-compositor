# north-compositor

`north-compositor` is a lightweight, high-performance tiling/floating Wayland compositor written in C11 using `wlroots` 0.20.2.

---

## Features

- **Virtual Workspaces**: Multi-workspace management with `ext-workspace-v1` protocol integration and workspace cycling.
- **Dynamic Display Management**: Multi-monitor output arrangement, scaling, and runtime reconfiguration via `wlr-output-management-v1`.
- **Declarative Window Rules**: Pattern-based window assignment matching `app_id` and `title` with wildcard globs (`*`, `?`) for initial geometry, workspaces, snapping, and decoration styles.
- **Visual Alt+Tab HUD**: Cairo-rendered overlay switcher displaying window cards on the active workspace with keyboard cycling, Escape cancellation, and mouse click selection.
- **Snapping & Layouts**: Half-screen and corner window snapping (`snap_left`, `snap_right`, `snap_top_left`, `snap_top_right`) alongside maximize and fullscreen toggles.
- **Server-Side Decorations (SSD)**: Optional Cairo/Pango titlebars with window titles and active/inactive state styling.
- **Bare-Metal & Session Resilience**: Direct DRM/KMS modesetting with VT switching (`Ctrl+Alt+F1..F12`), preserving window workspaces and restoring focus across VT transitions.
- **Robust C11 Architecture**: Memory arenas (`Arena`) for grouped lifetimes, tagged-union `Result` error handling, structured logging, and zero memory leaks verified via AddressSanitizer.

---

## Requirements & Dependencies

### Build Tools
- C11 compliant compiler (`gcc` or `clang`)
- CMake >= 3.25
- Meson & Ninja (used by CMake to compile vendored dependencies)
- `pkg-config`

### System Libraries
- `pixman-1`
- `libinput`
- `cairo` & `pango` / `pangocairo`
- `fontconfig`
- `libseat`
- `libudev`
- `vulkan`
- `xcb`, `xcb-icccm`, `xcb-ewmh`

### Installing Dependencies

#### Arch Linux
```bash
sudo pacman -S base-devel cmake meson ninja pkgconf \
    pixman libinput pango cairo fontconfig seatd \
    systemd-libs vulkan-headers vulkan-icd-loader libxcb xcb-util-wm alacritty
```

#### Ubuntu / Debian
```bash
sudo apt install build-essential cmake meson ninja-build pkg-config \
    libpixman-1-dev libinput-dev libpango1.0-dev libcairo2-dev \
    libfontconfig1-dev libseat-dev libudev-dev libvulkan-dev \
    libxcb1-dev libxcb-icccm4-dev libxcb-ewmh-dev alacritty
```

#### Fedora
```bash
sudo dnf install gcc cmake meson ninja-build pkgconf-pkg-config \
    pixman-devel libinput-devel pango-devel cairo-devel \
    fontconfig-devel seatd-devel systemd-devel vulkan-loader-devel \
    libxcb-devel xcb-util-wm-devel alacritty
```

*(Note: Git submodules provide `wayland`, `wayland-protocols`, `libdrm`, `libxkbcommon`, and `wlroots` automatically via CMake).*

---

## Building & Testing

### 1. Clone with Submodules
```bash
git clone --recursive https://github.com/your-username/north-compositor.git
cd north-compositor
```

If already cloned without submodules:
```bash
git submodule update --init --recursive
```

### 2. Configure & Build
Using CMake Presets:

```bash
# Debug build (with testing enabled)
cmake --preset debug
cmake --build build/debug

# AddressSanitizer build
cmake --preset asan
cmake --build build/asan

# Release build
cmake --preset release
cmake --build build/release
```

### 3. Run Automated Tests
All unit and integration test suites run via CTest:

```bash
# Run tests under debug
ctest --test-dir build/debug --output-on-failure

# Run tests under AddressSanitizer (checks for memory leaks and undefined behavior)
ctest --test-dir build/asan --output-on-failure
```

---

## Running the Compositor

### Bare-Metal DRM Session (Standalone Desktop)
1. Switch to a free virtual console (e.g. `Ctrl+Alt+F3`) and log in.
2. Launch the compositor:
   ```bash
   ./build/debug/src/north-compositor
   ```
   *(Automatically loads `./compositor.conf` from the current working directory, or falls back to `$XDG_CONFIG_HOME/north/compositor.conf`).*
3. You can switch back to your existing graphical session anytime via `Ctrl+Alt+F1`, and return to `north-compositor` via `Ctrl+Alt+F3`.

### Nested Wayland/X11 Session (Development)
You can run `north-compositor` directly inside an existing desktop session (such as GNOME or KDE Plasma):
```bash
./build/debug/src/north-compositor -c compositor.conf
```
A nested Wayland window will open, letting you test window rules, layouts, and input dispatch without leaving your desktop.

---

## Default Keybindings Cheatsheet

| Shortcut | Action | Description |
| :--- | :--- | :--- |
| `Super + Return` | `exec alacritty` | Spawn terminal emulator |
| `Super + Q` | `close` | Close focused window |
| `Super + Up` | `maximize` | Toggle window maximization |
| `Super + Down` | `restore` | Restore window from maximized or snapped state |
| `Super + Left` | `snap_left` | Snap window to left half of screen |
| `Super + Right` | `snap_right` | Snap window to right half of screen |
| `Super + Alt + Left` | `snap_top_left` | Snap window to top-left quarter |
| `Super + Alt + Right` | `snap_top_right`| Snap window to top-right quarter |
| `Super + F` | `fullscreen` | Toggle fullscreen mode |
| `Alt + Tab` | `focus_next` | Open visual Alt+Tab HUD & cycle forward |
| `Alt + Shift + Tab` | `focus_prev` | Cycle backward in Alt+Tab HUD |
| `Super + 1` .. `9` | `workspace <N>` | Switch to virtual workspace 1 through 9 |
| `Super + Shift + 1` .. `9` | `move_to_workspace <N>` | Move focused window to workspace 1 through 9 |
| `Super + Ctrl + Left` | `workspace_prev` | Switch to previous workspace |
| `Super + Ctrl + Right`| `workspace_next` | Switch to next workspace |
| `Ctrl + Alt + F1` .. `F12` | *VT Switch* | Switch Linux virtual terminal (bare metal) |
| `Super + Shift + R` | `reload` | Reload configuration file (`compositor.conf`) |
| `Super + Shift + E` | `exit` | Terminate compositor cleanly |

---

## Configuration (`compositor.conf`)

`north-compositor` reads configuration from:
1. `-c /path/to/file.conf` (if passed via CLI)
2. `$XDG_CONFIG_HOME/north/compositor.conf` (or `~/.config/north/compositor.conf`)
3. `./compositor.conf` (current working directory)
4. Built-in compiled defaults (if no file is found)

### Configuration Example

```ini
[input:keyboard]
layout = us
repeat_rate = 25
repeat_delay = 600

[input:touchpad]
tap = true
natural_scroll = true

[input:pointer]
natural_scroll = false

[output:eDP-1]
mode = 1920x1080@60
scale = 1.0
position = 0,0
enabled = true

[decoration]
font = Sans 10
titlebar_height = 28
border_width = 1
shadow_radius = 18
force_ssd = false
active_title = #1e2230ff
inactive_title = #2a2e3aff
active_border = #3b4252ff
inactive_border = #2e3440ff

[workspaces]
count = 9

# Declarative window rules (wildcard matching on app_id and title)
[rule:browser]
app_id = firefox*
workspace = 1
maximize = true

[rule:terminal]
app_id = alacritty
workspace = 2
snap = left

[bind]
Super+Return = exec alacritty
Super+Q = close
Super+Up = maximize
Super+Down = restore
Super+Left = snap_left
Super+Right = snap_right
Super+Alt+Left = snap_top_left
Super+Alt+Right = snap_top_right
Super+F = fullscreen
Alt+Tab = focus_next
Alt+Shift+Tab = focus_prev
Super+Shift+R = reload
Super+Shift+E = exit

Super+1 = workspace 1
Super+2 = workspace 2
Super+3 = workspace 3
Super+4 = workspace 4
Super+5 = workspace 5
Super+6 = workspace 6
Super+7 = workspace 7
Super+8 = workspace 8
Super+9 = workspace 9

Super+Shift+1 = move_to_workspace 1
Super+Shift+2 = move_to_workspace 2
Super+Shift+3 = move_to_workspace 3
Super+Shift+4 = move_to_workspace 4
Super+Shift+5 = move_to_workspace 5
Super+Shift+6 = move_to_workspace 6
Super+Shift+7 = move_to_workspace 7
Super+Shift+8 = move_to_workspace 8
Super+Shift+9 = move_to_workspace 9

Super+Ctrl+Left = workspace_prev
Super+Ctrl+Right = workspace_next
```
