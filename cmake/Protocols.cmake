# cmake/Protocols.cmake

set(PROTO_GEN_DIR "${CMAKE_BINARY_DIR}/protocols")
file(MAKE_DIRECTORY "${PROTO_GEN_DIR}")

set(WAYLAND_SCANNER "${NORTH_DEPS_BIN}/wayland-scanner")

function(north_generate_protocol xml_path proto_name)
  set(header_server "${PROTO_GEN_DIR}/${proto_name}-protocol.h")
  set(header_client "${PROTO_GEN_DIR}/${proto_name}-client-protocol.h")
  set(source_code "${PROTO_GEN_DIR}/${proto_name}-protocol.c")

  add_custom_command(
    OUTPUT "${header_server}"
    COMMAND ${WAYLAND_SCANNER} server-header "${xml_path}" "${header_server}"
    DEPENDS "${xml_path}" dep_wayland
    COMMENT "Generating server header for ${proto_name}"
  )

  add_custom_command(
    OUTPUT "${header_client}"
    COMMAND ${WAYLAND_SCANNER} client-header "${xml_path}" "${header_client}"
    DEPENDS "${xml_path}" dep_wayland
    COMMENT "Generating client header for ${proto_name}"
  )

  add_custom_command(
    OUTPUT "${source_code}"
    COMMAND ${WAYLAND_SCANNER} private-code "${xml_path}" "${source_code}"
    DEPENDS "${xml_path}" dep_wayland
    COMMENT "Generating protocol code for ${proto_name}"
  )

  list(APPEND NORTH_PROTO_SOURCES "${header_server}" "${header_client}" "${source_code}")
  set(NORTH_PROTO_SOURCES "${NORTH_PROTO_SOURCES}" PARENT_SCOPE)
endfunction()

set(XDG_SHELL_XML "${CMAKE_SOURCE_DIR}/third_party/wayland-protocols/stable/xdg-shell/xdg-shell.xml")
set(XDG_DECOR_XML "${CMAKE_SOURCE_DIR}/third_party/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml")
set(LAYER_SHELL_XML "${CMAKE_SOURCE_DIR}/third_party/wlroots/protocol/wlr-layer-shell-unstable-v1.xml")

north_generate_protocol("${XDG_SHELL_XML}" "xdg-shell")
north_generate_protocol("${XDG_DECOR_XML}" "xdg-decoration-unstable-v1")
north_generate_protocol("${LAYER_SHELL_XML}" "wlr-layer-shell-unstable-v1")

add_library(north_protocols STATIC ${NORTH_PROTO_SOURCES})
target_include_directories(north_protocols PUBLIC "${PROTO_GEN_DIR}" "${NORTH_DEPS_INCLUDE}")
target_link_libraries(north_protocols PUBLIC north::wayland_server)
add_dependencies(north_protocols dep_wayland dep_wayland_protocols dep_wlroots)
