# Desktop Portal & Screen Capture Guide

`north-compositor` integrates full support for both legacy (`zwlr_screencopy_manager_v1`) and official Wayland extension (`ext-image-copy-capture-v1`) protocols, enabling out-of-the-box screen capture, region selection, screen recording, and desktop portal integration.

---

## 1. Supported Tools

| Tool | Purpose | Underlying Protocol | Command Example |
| :--- | :--- | :--- | :--- |
| **`grim`** | Screenshots | `wlr-screencopy-v1` | `grim screenshot.png` |
| **`slurp`** | Interactive Region Selector | Layer Shell | `slurp` |
| **`wf-recorder`** | Screen & Window Recording | `wlr-screencopy-v1` | `wf-recorder -f recording.mp4` |
| **`wayvnc`** | Remote Desktop VNC Server | `wlr-screencopy-v1` | `wayvnc 0.0.0.0` |
| **`xdg-desktop-portal-wlr`** | Desktop Screen Sharing (OBS, WebRTC, Discord) | `wlr-screencopy-v1` & `wlr-output-management-v1` | System service |

---

## 2. Built-in Keybindings

`north-compositor` defines built-in defaults for immediate screenshotting:

- **`Print`**: Captures the entire desktop using `grim` to the current user's Pictures directory.
- **`Shift+Print`**: Allows dragging an interactive selection box with `slurp` and pipes the geometry directly to `grim`.

To customize these in `~/.config/north/compositor.conf`:

```ini
[keybinds]
Print = exec grim "$HOME/Pictures/Screenshot_$(date +%Y-%m-%d_%H-%M-%S).png"
Shift+Print = exec sh -c 'grim -g "$(slurp)" "$HOME/Pictures/Screenshot_$(date +%Y-%m-%d_%H-%M-%S).png"'
```

---

## 3. Desktop Portal Setup (`xdg-desktop-portal-wlr`)

For browser WebRTC screen sharing (Chromium, Firefox), OBS Studio, and video calls, install `xdg-desktop-portal-wlr` alongside the main `xdg-desktop-portal`.

### 3.1 Portal Configuration

Create `~/.config/xdg-desktop-portal/portals.conf` (or `~/.config/xdg-desktop-portal/north-portals.conf`):

```ini
[preferred]
default = wlr;gtk
org.freedesktop.impl.portal.ScreenCast = wlr
org.freedesktop.impl.portal.Screenshot = wlr
```

### 3.2 WLR Portal Specific Configuration

In `~/.config/xdg-desktop-portal-wlr/config`:

```ini
[screencast]
max_fps = 60
chooser_type = simple
chooser_cmd = slurp -f %o -or
```

---

## 4. Privacy & Session Lock Protection

When `north-compositor` is locked via `ext-session-lock-v1` (e.g. `swaylock`):

1. **Automatic Frame Invalidation**: All in-flight screencopy requests are invalidated.
2. **Commit Privacy Filter**: Attempts to copy screens during locked state are immediately failed via `zwlr_screencopy_frame_v1_send_failed()`.
3. **No Pixel Leakage**: Screen buffers cannot be read by background recording processes while the compositor is locked.
