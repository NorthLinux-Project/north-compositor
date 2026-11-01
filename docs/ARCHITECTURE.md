# North Compositor: Subsystem Architecture & Technical Specification

`north-compositor` is a lightweight, high-performance tiling/floating Wayland compositor written in C11 using `wlroots` 0.20.2, Cairo, and Pango.

---

## 1. System Overview & Architecture Diagram

```
                              +---------------------------------------+
                              |                main.c                 |
                              | Entry Point & Wayland Event Loop Run  |
                              +-------------------+-------------------+
                                                  |
                +---------------------------------+---------------------------------+
                |                                 |                                 |
                v                                 v                                 v
+-------------------------------+ +-------------------------------+ +-------------------------------+
|          src/core/            | |          src/output/          | |          src/shell/           |
|  - server.c (north_server,    | |  - output.c (north_output,    | |  - view.c (north_view, SSD,   |
|    display, loop, backend,    | |    modesetting, layout,       | |    geometry, state, rules)    |
|    allocator, renderer, scene)| |    usable area calculation)   | |  - xdg.c (wlr_xdg_shell)      |
|  - idle.c (inhibit, notifier) | |  - wlr-output-management-v1   | |  - layer.c (wlr_layer_shell)  |
+---------------+---------------+ +---------------+---------------+ |  - workspace.c (ext-workspace)|
                |                                 |                 |  - switcher.c (Alt+Tab HUD)   |
                |                                 |                 |  - lock.c (session lock)      |
                |                                 |                 +---------------+---------------+
                |                                 |                                 |
                +---------------------------------+---------------------------------+
                |                                 |                                 |
                v                                 v                                 v
+-------------------------------+ +-------------------------------+ +-------------------------------+
|          src/input/           | |          src/decor/           | |          src/config/          |
|  - seat.c (wlr_seat, focus)   | |  - decoration.c (titlebars,   | |  - config.c (config loader,   |
|  - keyboard.c (XKB, dispatch) | |    borders, drop shadows)     | |    defaults, bindings)        |
|  - keybind.c (parsing, table) | |  - cairo_buffer.c (Cairo      | |  - parser.c (INI tokenizer)   |
|  - pointer.c (cursor, motion, | |    surface backing buffers)   | |  - watch.c (inotify reload)   |
|    resizing, moving, snapping)| +-------------------------------+ +-------------------------------+
+---------------+---------------+                                                   |
                |                                                                   |
                +---------------------------------+---------------------------------+
                                                  |
                                                  v
                                  +-------------------------------+
                                  |           src/util/           |
                                  |  - arena.c (Memory arenas)    |
                                  |  - error.h (Result types)     |
                                  |  - match.c (Wildcard globs)   |
                                  |  - log.c (Structured logging) |
                                  +-------------------------------+
```

---

## 2. Core Subsystems

### 2.1 Core Subsystem (`src/core/`)

The core server orchestrates the Wayland display server, libwayland event loop, hardware backend, and rendering pipeline.

- **Server Lifecycle (`north_server`)**:
  - `server_init()`: Creates `wl_display`, extracts the event loop, and calls `wlr_backend_autocreate()` with session pointer support.
  - Initializes `wlr_renderer` and `wlr_allocator`.
  - Creates the root scene graph (`server->scene = wlr_scene_create()`) and establishes layered trees:
    - `layers.background`: Wallpaper and desktop backgrounds.
    - `layers.bottom`: Desktop widgets and bottom panels.
    - `layers.views`: Normal application windows and tiled views.
    - `layers.top`: Top taskbars and docks.
    - `layers.overlay`: Switcher HUD, popups, and lockscreens.
- **Session & DRM Lifecycle**:
  - Registers `server->session_active` listener on `server->session->events.active`.
  - When returning to the compositor VT from another virtual terminal, triggers output frame scheduling and calls `view_focus_top_visible()` to restore keyboard focus.
- **Idle Inhibition**:
  - `wlr_idle_notifier_v1` tracks user input activity.
  - `wlr_idle_inhibit_v1` allows media players and presentation applications to suppress display sleep.

### 2.2 Output Subsystem (`src/output/`)

Manages physical display outputs, DRM modesetting, output scaling, layout positioning, and usable desktop bounds.

- **Output Management**:
  - `output_handle_new()`: Initializes renderer buffers for incoming `wlr_output`, establishes initial modes, positions outputs via `wlr_output_layout`, initializes workspace sets, and adopts existing views.
  - `output_handle_destroy()`: Unlinks destroyed monitors, preserves `saved_workspace_index` on all child views, and migrates views to remaining outputs or scene layers.
- **Usable Area & Layer Shell Arrangement**:
  - Calculates usable screen boundaries after reserving exclusive zones requested by panels and status bars via `wlr_layer_shell_v1`.
- **Dynamic Output Configuration Protocol (`wlr-output-management-v1`)**:
  - Publishes active heads, modes, positions, and transform states to clients.
  - Implements atomic test and apply handlers (`server_handle_output_mgr_apply`, `server_handle_output_mgr_test`) for tools like `wlr-randr` and `kanshi`.

### 2.3 Shell Subsystem (`src/shell/`)

Encapsulates application windows, layer surfaces, virtual workspaces, and visual overlays.

- **View Abstraction (`struct north_view`)**:
  - Unifies XDG toplevels (`xdg.c`) and XWayland surfaces (`xwayland.c`).
  - Maintains window states (`VIEW_NORMAL`, `VIEW_MAXIMIZED`, `VIEW_FULLSCREEN`, `VIEW_SNAPPED`).
  - Tracks client surface, scene tree, server-side decoration, and window geometry.
- **Virtual Workspaces (`src/shell/workspace.c`)**:
  - Implements `ext-workspace-v1` for desktop workspace pagination.
  - Each output maintains an independent or unified set of workspaces (`output->workspaces`).
  - Switching workspaces toggles scene node visibility (`wlr_scene_node_set_enabled`) and transfers keyboard focus to the topmost visible view.
- **Visual Alt+Tab HUD Switcher (`src/shell/switcher.c`)**:
  - Retained overlay layer component rendered with Cairo.
  - Groups windows belonging to the active workspace on the focused output.
  - Features mouse card selection, wrap-around cycling, and Escape cancellation.
- **Window Placement & Snapping (`src/shell/placement.c`)**:
  - Initial placement algorithm positions new windows avoiding overlaps within usable area bounds.
  - Snapping engine computes half-screen and quarter-screen bounding boxes (`snap_left`, `snap_right`, `snap_top_left`, `snap_top_right`).

### 2.4 Input Subsystem (`src/input/`)

Interfaces with libinput via wlroots to process seats, keyboards, cursors, touchpads, and pointer constraints.

- **Seat Management (`src/input/seat.c`)**:
  - Single unified seat (`north_seat`) supporting pointer and keyboard capabilities.
- **Keyboard Handling & Dispatch (`src/input/keyboard.c`, `src/input/keybind.c`)**:
  - Compiles XKB keymaps with user-defined layouts, variants, and options (`us`, repeat rates).
  - Modifier-level parsing maps Shift, Ctrl, Alt, and Super masks.
  - Matches keysyms case-insensitively and through level-0 raw keysym inspection to support shifted shortcut triggers (e.g. `Super+Shift+E`).
  - Implements bare-metal VT switching (`Ctrl+Alt+F1..F12` and `XF86Switch_VT_1..12`) via `wlr_session_change_vt()`.
- **Pointer Interactions (`src/input/pointer.c`)**:
  - Interactive window moving and resizing via cursor edge grabbing.
  - Click-to-focus and decoration button hit-testing.
  - Pointer constraints (`wlr_pointer_constraints_v1`) support for games and capture utilities.

### 2.5 Config Subsystem (`src/config/`)

Handles configuration parsing, in-memory representations, and live reloads.

- **Parser (`src/config/parser.c`)**:
  - Custom INI-style recursive descent tokenizer and parser allocating all structures into a temporary `Arena`.
- **Config Loader (`src/config/config.c`)**:
  - Resolves configuration file paths through CLI argument `-c`, `$XDG_CONFIG_HOME`, `~/.config/north/compositor.conf`, and `./compositor.conf`.
  - Provides compiled built-in defaults if no configuration file is present.
- **Hot Reload (`src/config/watch.c`)**:
  - Monitors configuration files via inotify descriptors to re-apply rules, decoration colors, and keybindings on file write.

### 2.6 Server-Side Decorations (`src/decor/`)

Renders window titlebars, active/inactive borders, and drop shadows using Cairo software rasterization wrapped in Wayland scene buffers (`cairo_buffer.c`).

- Window close, maximize, and minimize buttons.
- Configurable titlebar font rendering with Pango layout formatting.

### 2.7 Utility & Core Primitives (`src/util/`)

- **Memory Arena (`src/util/arena.c`)**:
  - Linear contiguous block allocator (`Arena`) providing $O(1)$ allocation and zero fragmentation.
  - Used for grouped lifetimes such as configuration loading, string building, and test fixtures.
- **Result & Error Handling (`src/util/error.h`)**:
  - Portable tagged-union Result types providing explicit error propagation without unstructured pointer returns.
- **Wildcard Pattern Matching (`src/util/match.c`)**:
  - Glob matching implementation for window rules (`*`, `?`).

---

## 3. Data Structures & Scene Graph Hierarchy

```
server->scene (Root Scene)
 │
 ├── layers.background (Background layer-shell surfaces)
 ├── layers.bottom     (Bottom panels, desktop docks)
 ├── layers.views      (Active workspaces & application views)
 │    │
 │    ├── workspace_0 (Scene Tree)
 │    │    ├── view_A->tree (Toplevel Window Node)
 │    │    │    ├── decor.tree (Titlebar & Borders)
 │    │    │    └── surface_tree (Client wl_surface buffer)
 │    │    └── view_B->tree
 │    │
 │    └── workspace_1 (Scene Tree, disabled when inactive)
 │
 ├── layers.top        (Top taskbars, panels)
 └── layers.overlay    (Alt+Tab Switcher HUD, Screen Lock, Popups)
      └── switcher->tree
           └── scene_buffer (Cairo-rendered HUD cards)
```
