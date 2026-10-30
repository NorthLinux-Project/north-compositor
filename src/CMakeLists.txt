# src/CMakeLists.txt

add_library(north_util STATIC
  util/arena.c
  util/log.c
)
target_include_directories(north_util PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(north_util PUBLIC project_warnings project_sanitizers)

add_library(north_core_pure STATIC
  config/parser.c
  config/config.c
  shell/placement.c
  input/keybind.c
)
target_include_directories(north_core_pure PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(north_core_pure
  PUBLIC
    north_util
    north::xkbcommon
    project_warnings
    project_sanitizers
)

add_library(north_core STATIC
  core/server.c
  output/output.c
  shell/view.c
  shell/xdg.c
  shell/layer.c
  shell/lock.c
  xwayland/xwayland.c
  decor/cairo_buffer.c
  decor/titlebar.c
  decor/decoration.c
  input/seat.c
  input/keyboard.c
  input/pointer.c
  config/watch.c
)
target_include_directories(north_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(north_core
  PUBLIC
    north_core_pure
    north_protocols
    north::wlroots
    north::wayland_server
    north::drm
    PkgConfig::PIXMAN
    PkgConfig::LIBINPUT
    PkgConfig::PANGO
    PkgConfig::CAIRO
    PkgConfig::FONTCONFIG
    PkgConfig::LIBSEAT
    PkgConfig::LIBUDEV
    PkgConfig::VULKAN
    PkgConfig::XCB
    PkgConfig::XCB_ICCCM
    PkgConfig::XCB_EWMH
    project_warnings
    project_sanitizers
)

add_executable(north-compositor main.c)
target_include_directories(north-compositor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(north-compositor
  PRIVATE
    north_core
    project_warnings
    project_sanitizers
)

set_target_properties(north-compositor PROPERTIES
  BUILD_RPATH "${NORTH_DEPS_LIB}"
  INSTALL_RPATH "${NORTH_DEPS_LIB}"
  INSTALL_RPATH_USE_LINK_PATH TRUE
  LINK_FLAGS "-Wl,--disable-new-dtags"
)
