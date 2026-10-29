# tests/CMakeLists.txt

function(add_unity_test test_target test_source)
  cmake_parse_arguments(ARG "" "" "LIBRARIES;DEPENDS" ${ARGN})
  add_executable(${test_target} ${test_source})
  target_include_directories(${test_target}
    PRIVATE
      ${CMAKE_SOURCE_DIR}/src
      ${PROTO_GEN_DIR}
  )
  target_compile_definitions(${test_target}
    PRIVATE
      NORTH_COMPOSITOR_BIN="${CMAKE_BINARY_DIR}/src/north-compositor"
  )
  target_link_libraries(${test_target}
    PRIVATE
      unity
      project_warnings
      project_sanitizers
      ${ARG_LIBRARIES}
  )
  if(ARG_DEPENDS)
    add_dependencies(${test_target} ${ARG_DEPENDS})
  endif()
  set_target_properties(${test_target} PROPERTIES
    BUILD_RPATH "${NORTH_DEPS_LIB}"
    INSTALL_RPATH "${NORTH_DEPS_LIB}"
    INSTALL_RPATH_USE_LINK_PATH TRUE
    LINK_FLAGS "-Wl,--disable-new-dtags"
  )
  add_test(NAME ${test_target} COMMAND ${test_target})
endfunction()

# Unit tests (link only north_core_pure, no wlroots runtime needed)
add_unity_test(test_arena unit/test_arena.c
  LIBRARIES north_util
)

add_unity_test(test_config_parser unit/test_config_parser.c
  LIBRARIES north_core_pure
)

add_unity_test(test_config_values unit/test_config_values.c
  LIBRARIES north_core_pure
)

add_unity_test(test_keybind_parse unit/test_keybind_parse.c
  LIBRARIES north_core_pure
)

add_unity_test(test_placement unit/test_placement.c
  LIBRARIES north_core_pure
)

# Integration tests (run against compiled north-compositor on headless backend)
add_unity_test(test_headless_startup integration/test_headless_startup.c
  LIBRARIES north::wayland_client north_protocols
  DEPENDS north-compositor
)

add_unity_test(test_xdg_toplevel integration/test_xdg_toplevel.c
  LIBRARIES north::wayland_client north_protocols
  DEPENDS north-compositor
)

add_unity_test(test_config_reload integration/test_config_reload.c
  LIBRARIES north_util
  DEPENDS north-compositor
)
