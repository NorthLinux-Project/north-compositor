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
set(FOREIGN_TOPLEVEL_XML "${CMAKE_SOURCE_DIR}/third_party/wlroots/protocol/wlr-foreign-toplevel-management-unstable-v1.xml")
set(IDLE_INHIBIT_XML "${CMAKE_SOURCE_DIR}/third_party/wayland-protocols/unstable/idle-inhibit/idle-inhibit-unstable-v1.xml")
set(EXT_IDLE_NOTIFY_XML "${CMAKE_SOURCE_DIR}/third_party/wayland-protocols/staging/ext-idle-notify/ext-idle-notify-v1.xml")
set(SESSION_LOCK_XML "${CMAKE_SOURCE_DIR}/third_party/wayland-protocols/staging/ext-session-lock/ext-session-lock-v1.xml")
set(POINTER_CONSTRAINT_XML "${CMAKE_SOURCE_DIR}/third_party/wayland-protocols/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml")
set(RELATIVE_POINTER_XML "${CMAKE_SOURCE_DIR}/third_party/wayland-protocols/unstable/relative-pointer/relative-pointer-unstable-v1.xml")
set(SCREENCOPY_XML "${CMAKE_SOURCE_DIR}/third_party/wlroots/protocol/wlr-screencopy-unstable-v1.xml")
set(IMAGE_COPY_CAPTURE_XML "${CMAKE_SOURCE_DIR}/third_party/wayland-protocols/staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml")
set(VIEWPORTER_XML "${CMAKE_SOURCE_DIR}/third_party/wayland-protocols/stable/viewporter/viewporter.xml")
set(FRACTIONAL_SCALE_XML "${CMAKE_SOURCE_DIR}/third_party/wayland-protocols/staging/fractional-scale/fractional-scale-v1.xml")
set(TEARING_CONTROL_XML "${CMAKE_SOURCE_DIR}/third_party/wayland-protocols/staging/tearing-control/tearing-control-v1.xml")
set(PRESENTATION_TIME_XML "${CMAKE_SOURCE_DIR}/third_party/wayland-protocols/stable/presentation-time/presentation-time.xml")
set(LINUX_DRM_SYNCOBJ_XML "${CMAKE_SOURCE_DIR}/third_party/wayland-protocols/staging/linux-drm-syncobj/linux-drm-syncobj-v1.xml")
set(VIRTUAL_KEYBOARD_XML "${CMAKE_SOURCE_DIR}/third_party/wlroots/protocol/virtual-keyboard-unstable-v1.xml")
set(EXT_WORKSPACE_XML "${CMAKE_SOURCE_DIR}/third_party/wayland-protocols/staging/ext-workspace/ext-workspace-v1.xml")

north_generate_protocol("${XDG_SHELL_XML}" "xdg-shell")
north_generate_protocol("${XDG_DECOR_XML}" "xdg-decoration-unstable-v1")
north_generate_protocol("${LAYER_SHELL_XML}" "wlr-layer-shell-unstable-v1")
north_generate_protocol("${FOREIGN_TOPLEVEL_XML}" "wlr-foreign-toplevel-management-unstable-v1")
north_generate_protocol("${IDLE_INHIBIT_XML}" "idle-inhibit-unstable-v1")
north_generate_protocol("${EXT_IDLE_NOTIFY_XML}" "ext-idle-notify-v1")
north_generate_protocol("${SESSION_LOCK_XML}" "ext-session-lock-v1")
north_generate_protocol("${POINTER_CONSTRAINT_XML}" "pointer-constraints-unstable-v1")
north_generate_protocol("${RELATIVE_POINTER_XML}" "relative-pointer-unstable-v1")
north_generate_protocol("${SCREENCOPY_XML}" "wlr-screencopy-unstable-v1")
north_generate_protocol("${IMAGE_COPY_CAPTURE_XML}" "ext-image-copy-capture-v1")
north_generate_protocol("${VIEWPORTER_XML}" "viewporter")
north_generate_protocol("${FRACTIONAL_SCALE_XML}" "fractional-scale-v1")
north_generate_protocol("${TEARING_CONTROL_XML}" "tearing-control-v1")
north_generate_protocol("${PRESENTATION_TIME_XML}" "presentation-time")
north_generate_protocol("${LINUX_DRM_SYNCOBJ_XML}" "linux-drm-syncobj-v1")
north_generate_protocol("${VIRTUAL_KEYBOARD_XML}" "virtual-keyboard-unstable-v1")
north_generate_protocol("${EXT_WORKSPACE_XML}" "ext-workspace-v1")

add_library(north_protocols STATIC ${NORTH_PROTO_SOURCES})
target_include_directories(north_protocols PUBLIC "${PROTO_GEN_DIR}" "${NORTH_DEPS_INCLUDE}")
target_link_libraries(north_protocols PUBLIC north::wayland_server)
add_dependencies(north_protocols dep_wayland dep_wayland_protocols dep_wlroots)
