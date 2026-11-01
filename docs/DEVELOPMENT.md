# North Compositor: Developer & Contributor Guide

This guide details code organization, memory management principles, error handling standards, testing workflows, and debugging practices for `north-compositor`.

---

## 1. Toolchain & Code Style

- **Language Standard**: C11 with `-std=gnu11 -Wall -Wextra -Wpedantic -Werror -Wno-unused-parameter`.
- **Formatting**: Strictly adheres to [`.clang-format`](../.clang-format).
  - Verify formatting across the codebase:
    ```bash
    find src tests -type f \( -name '*.c' -o -name '*.h' \) -exec clang-format --dry-run --Werror {} +
    ```
  - Format modified files:
    ```bash
    clang-format -i path/to/file.c path/to/file.h
    ```
- **Code Comments**:
  - Write self-documenting code with clear variable and function names.
  - Never write numbered step markers (such as `// 1.`, `// Step 2:`) in comments or docstrings.
  - Strictly zero emojis across code, comments, commit messages, and documentation.

---

## 2. Core Architectural Patterns

### 2.1 Memory Arenas (`src/util/arena.h`)
`Arena` allocators provide linear block allocation with $O(1)$ amortized allocation and bulk deallocation.

```c
#include "util/arena.h"

// Initialize arena with 512 KB capacity
Arena arena = arena_create(1024 * 512);

// Allocate typed structure
NorthConfig *cfg = arena_push_type(&arena, NorthConfig);

// Allocate dynamic array
NorthKeybind *binds = arena_push_array(&arena, NorthKeybind, 64);

// Destroy all memory in single call
arena_destroy(&arena);
```

#### Rules for Arenas:
- Use arenas for grouped lifetimes: configuration parsing, INI document processing, command arguments, and test fixtures.
- Subsystems with indeterminate individual lifetimes (such as views, outputs, and keyboards) use explicit allocation and destruction pairs (`view_destroy`, `output_destroy`, `switcher_finish`).

### 2.2 Tagged-Union Result Error Handling (`src/util/error.h`)
Never return silent NULL pointers or unchecked integers for fallible operations. Use tagged-union `Result` types:

```c
#include "util/error.h"

typedef struct {
    bool ok;
    union {
        NorthColor value;
        NorthError err;
    };
} NorthColorResult;

NorthColorResult parse_color(const char *str)
{
    if (!str) {
        return RESULT_ERR(NorthColor, ERROR_CREATE(ERR_INVALID_ARGUMENT, "str is NULL"));
    }
    // ...
    return RESULT_OK(NorthColor, color);
}
```

---

## 3. DRM Session & Bare-Metal Invariants

When developing hardware backend features:

1. **VT Switching Destroys Connectors**:
   - Bare-metal VT switching (`Ctrl+Alt+F1` -> TTY 1) causes wlroots DRM backend to destroy connected outputs (`wlr_output_destroy`).
   - When outputs are destroyed, views must preserve their `saved_workspace_index` on `struct north_view`.
   - When the compositor VT reactivates (`Ctrl+Alt+F3`), `output_handle_new()` re-discovers the output and adopts all views where `view->workspace == NULL`, reparenting them to their corresponding workspace.
2. **Keyboard Focus Recovery**:
   - Always call `view_focus_top_visible(server)` upon output creation and session reactivation (`server_handle_session_active`).
   - Restore keyboard focus via `wlr_seat_keyboard_notify_enter()`.

---

## 4. Test Suite Architecture

Automated tests are divided into two distinct suites under [`tests/`](../tests/):

1. **Unit Tests (`tests/unit/`)**:
   - Pure C logic tested via the Unity Test Framework without requiring Wayland display servers.
   - `test_arena`: Memory arena allocations, alignments, and capacity checks.
   - `test_pattern_match`: Wildcard glob pattern evaluation (`*`, `?`).
   - `test_config_parser`: INI syntax tokenization, section grouping, and error states.
   - `test_config_values`: Configuration structure parsing and clamp limits.
   - `test_keybind_parse`: Modifier masking and keysym string parsing.
   - `test_placement`: Initial window placement heuristics.

2. **Integration Tests (`tests/integration/`)**:
   - Headless Wayland integration tests that boot an actual `struct north_server` using `wlr_backend_autocreate` (headless backend), spawning real Wayland client connections over UNIX sockets.
   - `test_headless_startup`: Server lifecycle, display initialization, and socket teardown.
   - `test_xdg_toplevel`: Client mapping, commit cycle, resizing, and state verification.
   - `test_layer_shell`: Layer surface arrangement and exclusive zone calculations.
   - `test_session_lock`: Screen lock protocol negotiation and input blocking.
   - `test_presentation`: Presentation time feedback cycles.
   - `test_workspaces`: Workspace cycling, client protocol queries, and view transitions.
   - `test_output_management`: Head inspection, mode testing, and runtime reconfiguration.
   - `test_window_rules`: Automatic workspace assignment and geometry overriding.
   - `test_switcher`: Cairo Alt+Tab HUD activation, cycling, mouse selection, and cancellation.

### Running Test Targets

```bash
# Debug preset
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure

# AddressSanitizer preset
cmake --build build/asan
ctest --test-dir build/asan --output-on-failure
```

### LSAN & Memory Leak Prevention in Tests
- Wayland client proxies (`wl_registry`, `wl_compositor`, `xdg_wm_base`, `ext_workspace_manager_v1`) must be explicitly destroyed via their respective `*_destroy()` functions before client disconnect.
- Always call `wl_display_roundtrip()` or `wl_display_flush()` to ensure all server-client events are cleared before tearing down the test server.
