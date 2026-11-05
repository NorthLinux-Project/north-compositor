# North Compositor: Configuration Reference

`north-compositor` uses an INI-style configuration file format (`compositor.conf`).

---

## 1. Configuration File Locations

The compositor resolves configuration in the following order:
1. File explicitly passed on CLI via `-c <path>`
2. `$XDG_CONFIG_HOME/north/compositor.conf`
3. `~/.config/north/compositor.conf`
4. `./compositor.conf` (Current working directory)
5. Built-in defaults

Changes written to `compositor.conf` are monitored via inotify and reloaded dynamically, or manually reloaded using `Super+Shift+R`.

---

## 2. Configuration Sections

### 2.1 Keyboard (`[input:keyboard]`)

Controls seat keyboard keymap, repeat delay, and repeat rates.

```ini
[input:keyboard]
layout = us              # XKB layout name (e.g. us, de, fr)
variant =                # Optional XKB variant
options =                # Optional XKB options (e.g. caps:escape)
repeat_rate = 25         # Key repeat frequency in Hz
repeat_delay = 600       # Milliseconds before key repeat begins
```

### 2.2 Touchpad (`[input:touchpad]`)

Controls touchpad gesture settings and scrolling behavior.

```ini
[input:touchpad]
tap = true               # Enable tap-to-click
natural_scroll = true    # Reverse scrolling direction
```

### 2.3 Pointer (`[input:pointer]`)

Configures mouse pointer parameters.

```ini
[input:pointer]
natural_scroll = false   # Reverse mouse wheel scroll direction
```

### 2.4 Output Displays (`[output:<connector>]`)

Configures resolution, refresh rate, scaling, and layout positions for display outputs (eDP-1, HDMI-A-1, DP-1).

```ini
[output:eDP-1]
mode = 1920x1080@60      # Format: <width>x<height>@<rate_hz>
scale = 1.0              # Output scaling factor (e.g. 1.0, 1.25, 2.0)
position = 0,0           # Logical desktop position <x>,<y>
enabled = true           # Enable or disable output
```

### 2.5 Server-Side Decorations (`[decoration]`)

Configures server-rendered titlebars, borders, shadows, and color themes.

```ini
[decoration]
font = Sans 10           # Pango font description
titlebar_height = 28     # Height of the titlebar in pixels
border_width = 1         # Window border thickness
shadow_radius = 18       # Drop shadow radius
force_ssd = false        # Force SSD even when client requests CSD
active_title = #1e2230ff # Titlebar color when window is active
inactive_title = #2a2e3aff # Titlebar color when window is inactive
active_border = #3b4252ff  # Border color when window is active
inactive_border = #2e3440ff # Border color when window is inactive
```

Colors are specified in `#RRGGBB` or `#RRGGBBAA` hexadecimal notation.

### 2.6 Workspaces (`[workspaces]`)

```ini
[workspaces]
count = 9                # Total number of virtual workspaces (1 to 9)
```

### 2.7 Animations (`[animation]`)

```ini
[animation]
enabled = true           # Enable smooth scene-graph animations (true/false)
duration_ms = 200        # Transition duration in milliseconds (0 to 5000)
```

### 2.8 Declarative Window Rules (`[rule:<name>]`)

Applies geometry, initial workspace, snapping, and decoration styles to windows matching criteria on initial map.

Criteria properties:
- `app_id`: Glob pattern matching application identifier (Wayland `app_id` or X11 `class`).
- `title`: Glob pattern matching window title bar text.

Glob wildcards:
- `*`: Matches zero or more characters.
- `?`: Matches any single character.

Action properties:
- `workspace`: Workspace index (1 through 9) to place the window upon spawn.
- `width` / `height`: Initial window dimensions in pixels.
- `maximize`: Boolean (`true` / `false`) to launch maximized.
- `fullscreen`: Boolean (`true` / `false`) to launch in fullscreen mode.
- `snap`: Initial snap placement (`left`, `right`, `top_left`, `top_right`).
- `ssd`: Force server-side decorations (`true` / `false`).

```ini
[rule:browser]
app_id = firefox*
workspace = 1
maximize = true

[rule:terminal]
app_id = alacritty
workspace = 2
snap = left

[rule:calculator]
title = Calculator
width = 400
height = 500
ssd = true
```

---

## 3. Keybinding Definitions (`[bind]`)

Keybindings follow the format: `<Modifier>+<Key> = <action> [arguments]`.

### Supported Modifiers
- `Super` (or `Mod4`, `Logo`)
- `Ctrl` (or `Control`)
- `Alt` (or `Mod1`)
- `Shift`

### Supported Actions

| Action | Argument | Description |
| :--- | :--- | :--- |
| `exec` | `<command>` | Execute shell command or binary |
| `close` | None | Close the currently focused window |
| `maximize` | None | Toggle maximization state |
| `restore` | None | Restore from maximized or snapped geometry |
| `fullscreen` | None | Toggle fullscreen state |
| `snap_left` | None | Snap window to left half of screen |
| `snap_right` | None | Snap window to right half of screen |
| `snap_top_left` | None | Snap window to top-left quarter |
| `snap_top_right` | None | Snap window to top-right quarter |
| `focus_next` | None | Cycle focus / open visual Alt+Tab switcher forward |
| `focus_prev` | None | Cycle focus / open visual Alt+Tab switcher backward |
| `workspace` | `<1..9>` | Switch to virtual workspace |
| `move_to_workspace`| `<1..9>` | Move focused window to workspace |
| `workspace_next`| None | Switch to next workspace |
| `workspace_prev`| None | Switch to previous workspace |
| `reload` | None | Reload configuration from disk |
| `exit` | None | Terminate compositor cleanly |

---

## 4. Complete Default Keybindings Table

| Shortcut | Action | Description |
| :--- | :--- | :--- |
| `Super + Return` | `exec alacritty` | Launch terminal |
| `Super + Q` | `close` | Close focused window |
| `Super + Up` | `maximize` | Maximize window |
| `Super + Down` | `restore` | Restore window size |
| `Super + Left` | `snap_left` | Snap to left half |
| `Super + Right` | `snap_right` | Snap to right half |
| `Super + Alt + Left` | `snap_top_left` | Snap to top-left quarter |
| `Super + Alt + Right`| `snap_top_right`| Snap to top-right quarter |
| `Super + F` | `fullscreen` | Fullscreen toggle |
| `Alt + Tab` | `focus_next` | Open Alt+Tab HUD / cycle forward |
| `Alt + Shift + Tab` | `focus_prev` | Alt+Tab HUD cycle backward |
| `Super + 1` .. `9` | `workspace 1..9` | Switch to workspace 1..9 |
| `Super + Shift + 1` .. `9` | `move_to_workspace 1..9` | Move window to workspace 1..9 |
| `Super + Ctrl + Left` | `workspace_prev` | Previous workspace |
| `Super + Ctrl + Right`| `workspace_next` | Next workspace |
| `Ctrl + Alt + F1` .. `F12` | *VT Switch* | Switch Linux Virtual Terminal |
| `Super + Shift + R` | `reload` | Reload configuration |
| `Super + Shift + E` | `exit` | Exit compositor |

---

## 4. External Scripting with `northctl`

The `northctl` CLI connects to the running compositor over `$NORTH_IPC_SOCK` to query state, send window management commands, or stream events for desktop status bars (such as Waybar).

### 4.1 State Queries
```bash
northctl get workspaces   # Returns array of active workspaces and output associations
northctl get views        # Returns array of open windows, titles, app_ids, and geometries
northctl get outputs      # Returns connected outputs, modes, and usable areas
northctl get version      # Returns compositor version metadata
```

### 4.2 Window & Compositor Control
```bash
northctl msg "workspace 2"           # Switch to workspace 2
northctl msg "move-to-workspace 3"   # Move focused window to workspace 3
northctl msg "focus next"            # Focus next window
northctl msg "close"                 # Close focused window
northctl msg "maximize"              # Toggle maximize state
northctl msg "fullscreen"            # Toggle fullscreen state
northctl reload                      # Reload configuration file
```

### 4.3 Real-Time Event Streaming (Waybar integration)
```bash
# Stream all events as JSON lines to stdout
northctl subscribe

# Stream only workspace and window events
northctl subscribe workspace window
```

