# North Compositor: Wayland Protocols Specification

`north-compositor` implements core Wayland protocols alongside modern staging and unstable protocol extensions for desktop integration, workspaces, dynamic display management, and server-side decorations.

---

## 1. Core Wayland Protocols

| Protocol | Version | Interface | Implementation File | Description |
| :--- | :--- | :--- | :--- | :--- |
| `wayland` | 6 | `wl_compositor` | `src/core/server.c` | Core surface allocation, commit cycles, and subsurfaces |
| `wayland` | 1 | `wl_subcompositor` | `src/core/server.c` | Subsurface positioning and synchronization |
| `wayland` | 9 | `wl_seat` | `src/input/seat.c` | Keyboard, pointer, and input device capabilities |
| `wayland` | 4 | `wl_output` | `src/output/output.c` | Display geometries, modes, physical sizes, subpixel orientation |
| `wayland` | 3 | `wl_data_device_manager` | `src/core/server.c` | Clipboard selection and drag-and-drop transfers |
| `wayland` | 1 | `wl_shm` | `src/core/server.c` | Shared-memory client buffer support |

---

## 2. Shell & Window Management Extensions

### 2.1 XDG Shell (`xdg-shell`)
- **Version**: 6
- **Interfaces**: `xdg_wm_base`, `xdg_surface`, `xdg_toplevel`, `xdg_popup`
- **Implementation**: [`src/shell/xdg.c`](../src/shell/xdg.c)
- **Capabilities**:
  - Toplevel window states: `maximized`, `fullscreen`, `resizing`, `activated`.
  - Asynchronous geometry configuration and client commit synchronization.
  - Interactive window movement and edge-resize grabs.
  - Transient popup hierarchies and grab tracking.

### 2.2 Layer Shell (`wlr-layer-shell-v1`)
- **Version**: 4
- **Interfaces**: `zwlr_layer_shell_v1`, `zwlr_layer_surface_v1`
- **Implementation**: [`src/shell/layer.c`](../src/shell/layer.c)
- **Capabilities**:
  - Organizes desktop panels, docks, notifications, and wallpapers across four layers:
    - `ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND`
    - `ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM`
    - `ZWLR_LAYER_SHELL_V1_LAYER_TOP`
    - `ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY`
  - Exclusive zone reservation dynamically updates usable screen dimensions for tiled and maximized application windows.

### 2.3 Virtual Workspaces (`ext-workspace-v1`)
- **Version**: 1
- **Interfaces**: `ext_workspace_manager_v1`, `ext_workspace_group_handle_v1`, `ext_workspace_handle_v1`
- **Implementation**: [`src/shell/workspace.c`](../src/shell/workspace.c)
- **Capabilities**:
  - Publishes workspace lists, active states, and coordinates to panels and pagers (Waybar, eww).
  - Handles client workspace activation requests.
  - Coordinates scene node visibility toggles when switching workspaces.

### 2.4 Foreign Toplevel Management (`wlr-foreign-toplevel-management-v1`)
- **Version**: 3
- **Interfaces**: `zwlr_foreign_toplevel_manager_v1`, `zwlr_foreign_toplevel_handle_v1`
- **Implementation**: [`src/shell/view.c`](../src/shell/view.c)
- **Capabilities**:
  - Exposes window titles, application IDs, and activation states to taskbars and docks.
  - Allows external pagers to request maximize, minimize, fullscreen, activate, and close actions.

---

## 3. Hardware & Display Management Extensions

### 3.1 Dynamic Output Management (`wlr-output-management-v1`)
- **Version**: 4
- **Interfaces**: `zwlr_output_manager_v1`, `zwlr_output_head_v1`, `zwlr_output_mode_v1`, `zwlr_output_configuration_v1`, `zwlr_output_configuration_head_v1`
- **Implementation**: [`src/output/output.c`](../src/output/output.c)
- **Capabilities**:
  - Runtime display resolution, refresh rate, scaling factor, and layout positioning control without compositor restarts.
  - Atomic two-phase commit:
    - `test`: Validates requested mode combinations and returns success/failure before modesetting.
    - `apply`: Applies verified configurations, arranges layer surfaces, and re-attaches views.

### 3.2 Output Power Management (`wlr-output-power-management-v1`)
- **Version**: 1
- **Interfaces**: `zwlr_output_power_manager_v1`, `zwlr_output_power_v1`
- **Capabilities**: DPMS display sleep and wake control for screensavers and idle daemons.

### 3.3 Linux DMA-BUF (`linux-dmabuf-v1`)
- **Version**: 5
- **Interfaces**: `zwp_linux_dmabuf_v1`, `zwp_linux_buffer_params_v1`
- **Implementation**: [`src/core/server.c`](../src/core/server.c)
- **Capabilities**: Zero-copy GPU buffer sharing between OpenGL/Vulkan clients and the compositor scene graph.

---

## 4. Input & Session Security Extensions

### 4.1 Session Lock (`ext-session-lock-v1`)
- **Version**: 1
- **Interfaces**: `ext_session_lock_manager_v1`, `ext_session_lock_v1`, `ext_session_lock_surface_v1`
- **Implementation**: [`src/shell/lock.c`](../src/shell/lock.c)
- **Capabilities**:
  - Cryptographically secure Wayland screen locking (compatible with `swaylock`, `waylock`).
  - Disables user input propagation to regular views and displays full-screen lock surfaces on `layers.overlay`.

### 4.2 Pointer Constraints (`pointer-constraints-unstable-v1`)
- **Version**: 1
- **Interfaces**: `zwp_pointer_constraints_v1`, `zwp_confined_pointer_v1`, `zwp_locked_pointer_v1`
- **Implementation**: [`src/input/pointer.c`](../src/input/pointer.c)
- **Capabilities**: Confines or locks mouse pointer within client surfaces for FPS games and virtual machines. `Super+Escape` breaks active pointer constraints.

### 4.3 Server-Side Decoration Protocols
- **XDG Decoration**: `zxdg_decoration_manager_v1` (Version 1, [`src/core/server.c`](../src/core/server.c))
- **KDE Server Decoration**: `org_kde_kwin_server_decoration_manager` (Version 1, [`src/core/server.c`](../src/core/server.c))
- Negotiates client-side versus server-side decorations with Qt and GTK applications.

### 4.4 Presentation Time & Tearing Control
- **Presentation Time**: `wp_presentation` (Version 1, [`src/core/server.c`](../src/core/server.c)) for frame timestamp and refresh cycle tracking.
- **Tearing Control**: `wp_tearing_control_manager_v1` (Version 1, [`src/core/server.c`](../src/core/server.c)) allowing fullscreen games to request asynchronous page-flips.

### 4.5 Screen Capture & Desktop Portal Protocols
- **wlroots Screencopy (`wlr-screencopy-unstable-v1`)**:
  - Version: 3
  - Interfaces: `zwlr_screencopy_manager_v1`, `zwlr_screencopy_frame_v1`
  - Implementation: [`src/core/server.c`](../src/core/server.c), [`src/output/output.c`](../src/output/output.c)
  - Full SHM and DMA-BUF copy, damage tracking, and cursor overlay support for `grim`, `slurp`, `wf-recorder`, `wayvnc`, and `xdg-desktop-portal-wlr`.
  - Enforces strict session lock privacy: capture requests during active session lock (`ext-session-lock-v1`) fail immediately.
- **Image Copy Capture (`ext-image-copy-capture-v1`) & Source Manager (`ext-image-capture-source-v1`)**:
  - Version: 1
  - Interfaces: `ext_image_copy_capture_manager_v1`, `ext_image_copy_capture_session_v1`, `ext_output_image_capture_source_manager_v1`
  - Implementation: [`src/core/server.c`](../src/core/server.c)
  - Upstream Wayland standard extension for capturing display outputs and desktop surfaces into shared buffers.

---

## 5. Unix Domain Socket IPC Protocol

`north-compositor` implements a length-prefixed binary socket protocol over `$XDG_RUNTIME_DIR/north-ipc-$WAYLAND_DISPLAY.sock` (`NORTH_IPC_SOCK`).

### 5.1 Packet Framing

```
[4 bytes: magic "NIPC"][4 bytes: uint32_t type][4 bytes: uint32_t payload_len][N bytes: JSON payload]
```

### 5.2 Message Types & Payloads

| Opcode | Name | Direction | Payload Schema | Description |
| :--- | :--- | :--- | :--- | :--- |
| `0` | `NORTH_IPC_MSG_COMMAND` | Client -> Server | String (e.g. `"workspace 2"`) | Dispatches compositor command |
| `1` | `NORTH_IPC_MSG_GET_WORKSPACES` | Client -> Server | None | Requests workspace array |
| `2` | `NORTH_IPC_MSG_GET_VIEWS` | Client -> Server | None | Requests open windows & geometries |
| `3` | `NORTH_IPC_MSG_GET_OUTPUTS` | Client -> Server | None | Requests output geometries & scales |
| `4` | `NORTH_IPC_MSG_GET_VERSION` | Client -> Server | None | Requests version metadata |
| `5` | `NORTH_IPC_MSG_SUBSCRIBE` | Client -> Server | `["workspace", "window", ...]` | Subscribes to real-time events |
| `100` | `NORTH_IPC_REPLY_SUCCESS` | Server -> Client | JSON Object / Array | Successful command or query response |
| `101` | `NORTH_IPC_REPLY_ERROR` | Server -> Client | `{"error": "message"}` | Error reply |
| `200` | `NORTH_IPC_EVENT_WORKSPACE` | Server -> Client | `{"change": "...", "workspace": {...}}` | Workspace switch/focus notification |
| `201` | `NORTH_IPC_EVENT_WINDOW` | Server -> Client | `{"change": "...", "window": {...}}` | Window map/unmap/focus notification |
| `202` | `NORTH_IPC_EVENT_OUTPUT` | Server -> Client | `{"change": "...", "output": {...}}` | Display attach/detach notification |
| `203` | `NORTH_IPC_EVENT_CONFIG` | Server -> Client | `{"change": "reload"}` | Config reload notification |

