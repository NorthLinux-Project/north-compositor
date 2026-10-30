# cmake/Dependencies.cmake
include(ExternalProject)
find_package(PkgConfig REQUIRED)

set(NORTH_DEPS_PREFIX "${CMAKE_BINARY_DIR}/deps/prefix")
set(NORTH_DEPS_INCLUDE "${NORTH_DEPS_PREFIX}/include")
set(NORTH_DEPS_LIB "${NORTH_DEPS_PREFIX}/lib")
set(NORTH_DEPS_BIN "${NORTH_DEPS_PREFIX}/bin")

file(MAKE_DIRECTORY "${NORTH_DEPS_INCLUDE}")
file(MAKE_DIRECTORY "${NORTH_DEPS_INCLUDE}/wlroots-0.20")
file(MAKE_DIRECTORY "${NORTH_DEPS_INCLUDE}/libdrm")
file(MAKE_DIRECTORY "${NORTH_DEPS_LIB}")
file(MAKE_DIRECTORY "${NORTH_DEPS_BIN}")

set(NORTH_MESON_ENV
  ${CMAKE_COMMAND} -E env
  "PKG_CONFIG_PATH=${NORTH_DEPS_LIB}/pkgconfig:${NORTH_DEPS_PREFIX}/share/pkgconfig:$ENV{PKG_CONFIG_PATH}"
  "PATH=${NORTH_DEPS_BIN}:$ENV{PATH}"
  "LD_LIBRARY_PATH=${NORTH_DEPS_LIB}:$ENV{LD_LIBRARY_PATH}"
)

set(NORTH_MESON_BUILD_TYPE "debug")
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(NORTH_MESON_BUILD_TYPE "release")
endif()

function(north_add_meson_dep name source_rel)
  cmake_parse_arguments(ARG "" "" "OPTIONS;DEPENDS;BYPRODUCTS" ${ARGN})
  ExternalProject_Add(dep_${name}
    SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/${source_rel}"
    BINARY_DIR "${CMAKE_BINARY_DIR}/deps/build/${name}"
    CONFIGURE_COMMAND ${NORTH_MESON_ENV} meson setup <BINARY_DIR> <SOURCE_DIR>
      --prefix=${NORTH_DEPS_PREFIX}
      --libdir=lib
      --buildtype=${NORTH_MESON_BUILD_TYPE}
      ${ARG_OPTIONS}
    BUILD_COMMAND ${NORTH_MESON_ENV} meson compile -C <BINARY_DIR>
    INSTALL_COMMAND ${NORTH_MESON_ENV} meson install -C <BINARY_DIR>
    DEPENDS ${ARG_DEPENDS}
    BUILD_BYPRODUCTS ${ARG_BYPRODUCTS}
  )
endfunction()

# 1. wayland
north_add_meson_dep(wayland wayland
  OPTIONS
    -Dtests=false
    -Ddocumentation=false
    -Ddtd_validation=false
  BYPRODUCTS
    "${NORTH_DEPS_LIB}/libwayland-server.so"
    "${NORTH_DEPS_LIB}/libwayland-client.so"
    "${NORTH_DEPS_BIN}/wayland-scanner"
)

# 2. wayland-protocols
north_add_meson_dep(wayland_protocols wayland-protocols
  OPTIONS
    -Dtests=false
  DEPENDS
    dep_wayland
)

# 3. libdrm
north_add_meson_dep(libdrm libdrm
  OPTIONS
    -Dauto_features=disabled
    -Dtests=false
  BYPRODUCTS
    "${NORTH_DEPS_LIB}/libdrm.so"
)

# 4. libxkbcommon
north_add_meson_dep(libxkbcommon libxkbcommon
  OPTIONS
    -Denable-tools=false
    -Denable-x11=false
    -Denable-docs=false
    -Denable-xkbregistry=false
    -Denable-wayland=false
  BYPRODUCTS
    "${NORTH_DEPS_LIB}/libxkbcommon.so"
)

# 5. wlroots
north_add_meson_dep(wlroots wlroots
  OPTIONS
    -Dexamples=false
    -Drenderers=gles2,vulkan
    -Dbackends=drm,libinput
    -Dxwayland=enabled
    -Dcolor-management=enabled
  DEPENDS
    dep_wayland
    dep_wayland_protocols
    dep_libdrm
    dep_libxkbcommon
  BYPRODUCTS
    "${NORTH_DEPS_LIB}/libwlroots-0.20.so"
)

# Imported library targets
add_library(north::wayland_server SHARED IMPORTED GLOBAL)
set_target_properties(north::wayland_server PROPERTIES
  IMPORTED_LOCATION "${NORTH_DEPS_LIB}/libwayland-server.so"
  INTERFACE_INCLUDE_DIRECTORIES "${NORTH_DEPS_INCLUDE}"
)
add_dependencies(north::wayland_server dep_wayland)

add_library(north::wayland_client SHARED IMPORTED GLOBAL)
set_target_properties(north::wayland_client PROPERTIES
  IMPORTED_LOCATION "${NORTH_DEPS_LIB}/libwayland-client.so"
  INTERFACE_INCLUDE_DIRECTORIES "${NORTH_DEPS_INCLUDE}"
)
add_dependencies(north::wayland_client dep_wayland)

add_library(north::drm SHARED IMPORTED GLOBAL)
set_target_properties(north::drm PROPERTIES
  IMPORTED_LOCATION "${NORTH_DEPS_LIB}/libdrm.so"
  INTERFACE_INCLUDE_DIRECTORIES "${NORTH_DEPS_INCLUDE};${NORTH_DEPS_INCLUDE}/libdrm"
)
add_dependencies(north::drm dep_libdrm)

add_library(north::xkbcommon SHARED IMPORTED GLOBAL)
set_target_properties(north::xkbcommon PROPERTIES
  IMPORTED_LOCATION "${NORTH_DEPS_LIB}/libxkbcommon.so"
  INTERFACE_INCLUDE_DIRECTORIES "${NORTH_DEPS_INCLUDE}"
)
add_dependencies(north::xkbcommon dep_libxkbcommon)

add_library(north::wlroots SHARED IMPORTED GLOBAL)
set_target_properties(north::wlroots PROPERTIES
  IMPORTED_LOCATION "${NORTH_DEPS_LIB}/libwlroots-0.20.so"
  INTERFACE_INCLUDE_DIRECTORIES "${NORTH_DEPS_INCLUDE};${NORTH_DEPS_INCLUDE}/wlroots-0.20"
)
add_dependencies(north::wlroots dep_wlroots)

# System dependencies
pkg_check_modules(PIXMAN REQUIRED IMPORTED_TARGET pixman-1)
pkg_check_modules(LIBINPUT REQUIRED IMPORTED_TARGET libinput)
pkg_check_modules(PANGO REQUIRED IMPORTED_TARGET pango pangocairo)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
pkg_check_modules(LIBSEAT REQUIRED IMPORTED_TARGET libseat)
pkg_check_modules(LIBUDEV REQUIRED IMPORTED_TARGET libudev)
pkg_check_modules(VULKAN REQUIRED IMPORTED_TARGET vulkan)
pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb)
pkg_check_modules(XCB_ICCCM REQUIRED IMPORTED_TARGET xcb-icccm)
pkg_check_modules(XCB_EWMH REQUIRED IMPORTED_TARGET xcb-ewmh)

# Unity submodule target for testing
if(BUILD_TESTING)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/unity/src/unity.c")
    message(FATAL_ERROR "Missing Unity submodule in third_party/unity")
  endif()

  add_library(unity STATIC
    third_party/unity/src/unity.c
    third_party/unity/src/unity.h
    third_party/unity/src/unity_internals.h
  )
  target_include_directories(unity PUBLIC third_party/unity/src)
endif()
