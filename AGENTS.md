# Agent Guidelines for north-compositor

`north-compositor` is a lightweight, tiling/floating Wayland compositor written in C11 using wlroots 0.20.2.

---

## 1. Architecture & Subsystems

- **Core (`src/core/`)**: Server lifecycle (`north_server`), Wayland display, event loop, backend/session initialization, idle inhibitor management.
- **Output (`src/output/`)**: DRM modesetting, output layout, scaling, usable area calculation, layer shell arrangement, and `wlr-output-management-v1` implementation.
- **Shell (`src/shell/`)**: View abstraction (`north_view`), XDG toplevel (`xdg.c`), layer shell (`layer.c`), session lock (`lock.c`), workspaces (`workspace.c`), Alt+Tab switcher HUD (`switcher.c`), and placement algorithms (`placement.c`).
- **Input (`src/input/`)**: Seat management (`seat.c`), keyboard handling and keybinding dispatch (`keyboard.c`, `keybind.c`), pointer/cursor motion and window resizing/moving (`pointer.c`).
- **Config (`src/config/`)**: INI-style parser (`parser.c`), configuration loader and built-in defaults (`config.c`), inotify file watch (`watch.c`).
- **Decoration (`src/decor/`)**: Server-side window decorations (`decoration.c`) rendered via Cairo buffers (`cairo_buffer.c`).
- **Utilities (`src/util/`)**: Memory arenas (`arena.c`), tagged-union Result types (`error.h`), wildcard glob matching (`match.c`), and structured logging (`log.c`).
- **XWayland (`src/xwayland/`)**: X11 client support via `wlr_xwayland`.
- **Tests (`tests/`)**: Unity unit tests (`tests/unit/`) and headless Wayland integration tests (`tests/integration/`).
- **Vendor (`third_party/`)**: Submodules including wlroots. Never modify files inside `third_party/wlroots`.

---

## 2. Build, Test, and Verification Commands

Maintain command stability across sessions:

```bash
# Debug build and test
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure

# AddressSanitizer build and test
cmake --build build/asan
ctest --test-dir build/asan --output-on-failure

# Formatting check
find src tests -type f \( -name '*.c' -o -name '*.h' \) -exec clang-format --dry-run --Werror {} +

# Apply formatting
clang-format -i <file1> <file2>
```

---

## 3. Engineering & Coding Standards

- **C Standard**: C11 with `-Wall -Wextra -Wpedantic -Werror -Wno-unused-parameter`.
- **Memory Management**:
  - Prefer memory arenas (`Arena`) for grouped lifetimes (parsers, configs, transient operations).
  - Explicit resource destruction: pair allocation with destruction handlers (`view_destroy`, `output_destroy`, `switcher_finish`).
  - Close Wayland client proxies and listeners in tests to avoid LSAN leaks.
- **Error Handling**:
  - Use tagged-union `Result` types (`RESULT_OK`, `RESULT_ERR`, `ERROR_CREATE`) for fallible operations.
  - Return early on errors; avoid deeply nested conditionals.
- **Comments & Formatting**:
  - Conform strictly to `.clang-format`.
  - Write self-documenting code. Never write numbered step markers (such as `// 1.`, `// Step 2:`) in comments or docstrings.
  - Zero emojis across code, comments, commit messages, and documentation.
- **DRM & Session Invariants**:
  - Bare-metal VT switching destroys and re-creates DRM outputs. Views must preserve `saved_workspace_index` and be re-adopted in `output_handle_new()` when outputs return.
  - Always restore window/keyboard focus via `view_focus_top_visible()` upon VT activation.

---

## 4. Git & Workflow Rules

- Never stage or commit changes (`git add`, `git commit`) without explicit user instruction.
- Protect submodules: do not modify or commit files within `third_party/wlroots`.
- Always verify all 18 test suites pass under both `build/debug` and `build/asan` before considering any task complete.
