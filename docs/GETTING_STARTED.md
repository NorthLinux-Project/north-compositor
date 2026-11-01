# Getting Started with North Compositor

This guide covers system prerequisites, building `north-compositor` from source, launching your first session, and verifying the setup.

---

## 1. System Prerequisites

`north-compositor` requires a Linux system with functional DRM/KMS drivers, udev, and hardware-accelerated OpenGL/Vulkan support:

- **GPU Drivers**: Mesa (Radeon, Intel, Nouveau) or NVIDIA proprietary drivers with DRM KMS enabled (`nvidia-drm.modeset=1`).
- **Input & Seat**: `libseat` or `systemd-logind` / `seatd` for session management.
- **Recommended Terminal**: `alacritty` (configured as default terminal).

### Package Installation

#### Arch Linux
```bash
sudo pacman -S --needed \
    base-devel cmake meson ninja pkgconf git \
    pixman libinput pango cairo fontconfig seatd \
    systemd-libs vulkan-headers vulkan-icd-loader libxcb xcb-util-wm alacritty
```

#### Ubuntu / Debian (22.04+)
```bash
sudo apt-get update && sudo apt-get install -y \
    build-essential cmake meson ninja-build pkg-config git \
    libpixman-1-dev libinput-dev libpango1.0-dev libcairo2-dev \
    libfontconfig1-dev libseat-dev libudev-dev libvulkan-dev \
    libxcb1-dev libxcb-icccm4-dev libxcb-ewmh-dev alacritty
```

#### Fedora (38+)
```bash
sudo dnf install -y \
    gcc cmake meson ninja-build pkgconf-pkg-config git \
    pixman-devel libinput-devel pango-devel cairo-devel \
    fontconfig-devel seatd-devel systemd-devel vulkan-loader-devel \
    libxcb-devel xcb-util-wm-devel alacritty
```

---

## 2. Building from Source

### 2.1 Clone Repository with Submodules
`north-compositor` vendors dependencies (including `wlroots` 0.20.2, `wayland`, `wayland-protocols`, `libdrm`, and `libxkbcommon`) as Git submodules to guarantee ABI compatibility:

```bash
git clone --recursive https://github.com/NorthLinux-Project/north-compositor.git
cd north-compositor
```

If the repository was cloned non-recursively, initialize submodules manually:
```bash
git submodule update --init --recursive
```

### 2.2 Compile Using Presets
The build system uses modern CMake Presets (`CMakePresets.json`):

```bash
# Debug configuration with full assertions and test targets
cmake --preset debug
cmake --build build/debug

# AddressSanitizer configuration for memory leak and safety verification
cmake --preset asan
cmake --build build/asan

# Optimized release configuration
cmake --preset release
cmake --build build/release
```

---

## 3. Running the Compositor

### 3.1 Standalone Bare-Metal TTY Session
To use `north-compositor` as your primary desktop environment:

1. Switch to an unused Linux virtual console (e.g. `Ctrl+Alt+F3`).
2. Log in with your user credentials.
3. Start the compositor directly from the project root:
   ```bash
   cd ~/Projects/C/north-compositor
   ./build/debug/src/north-compositor
   ```
4. `north-compositor` initializes KMS modesetting, discovers displays, and binds input seats.

#### Session Navigation
- Press **`Super+Return`** to spawn `alacritty`.
- Press **`Ctrl+Alt+F1`** to switch back to your display manager / existing desktop (e.g. KDE Plasma or GNOME on TTY 1).
- Press **`Ctrl+Alt+F3`** to return to `north-compositor` on TTY 3. All windows and focus are restored automatically.
- Press **`Super+Shift+E`** to cleanly exit the compositor.

### 3.2 Nested Development Session
For testing changes, keybindings, or window rules without leaving your active graphical desktop:

```bash
./build/debug/src/north-compositor -c compositor.conf
```

A nested Wayland window opens directly inside your current environment, allowing real-time debugging and log inspection.

---

## 4. User Configuration Setup

`north-compositor` searches for configuration files in the following priority order:
1. File explicitly passed with `-c <path>`
2. `$XDG_CONFIG_HOME/north/compositor.conf`
3. `~/.config/north/compositor.conf`
4. `./compositor.conf` in current working directory
5. Built-in defaults

To install the default configuration to your user directory:
```bash
mkdir -p ~/.config/north
cp compositor.conf ~/.config/north/compositor.conf
```

You can reload your configuration live without restarting the compositor by pressing **`Super+Shift+R`**.

---

## 5. Verification & Automated Test Suites

Verify all subsystems and memory invariants with CTest:

```bash
# Standard test suite execution
ctest --test-dir build/debug --output-on-failure

# AddressSanitizer test suite execution
ctest --test-dir build/asan --output-on-failure
```

All 18 test suites cover unit memory logic, configuration loading, keybinding parsing, window rules, tiling, workspaces, output management, and the Alt+Tab switcher.
