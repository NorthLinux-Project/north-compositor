#include "core/server.h"

#include "output/output.h"
#include "shell/layer.h"
#include "shell/lock.h"
#include "shell/view.h"
#include "shell/xdg.h"
#include "util/log.h"
#include "xwayland/xwayland.h"

#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>

struct north_toplevel_decor {
    struct north_server *server;
    struct wlr_xdg_toplevel_decoration_v1 *wlr_decor;
    struct wl_listener request_mode;
    struct wl_listener destroy;
};

static void decor_handle_request_mode(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_toplevel_decor *decor = wl_container_of(listener, decor, request_mode);
    enum wlr_xdg_toplevel_decoration_v1_mode mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
    if (!decor->server->config->decor.force_ssd &&
        decor->wlr_decor->requested_mode != WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_NONE) {
        mode = decor->wlr_decor->requested_mode;
    }
    if (decor->wlr_decor->toplevel->base->initialized) {
        wlr_xdg_toplevel_decoration_v1_set_mode(decor->wlr_decor, mode);
    } else {
        decor->wlr_decor->scheduled_mode = mode;
    }
}

static void decor_handle_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_toplevel_decor *decor = wl_container_of(listener, decor, destroy);
    wl_list_remove(&decor->request_mode.link);
    wl_list_remove(&decor->destroy.link);
    free(decor);
}

static void server_handle_new_decoration(struct wl_listener *listener, void *data)
{
    struct north_server *server = wl_container_of(listener, server, new_decoration);
    struct wlr_xdg_toplevel_decoration_v1 *wlr_decor = data;

    struct north_toplevel_decor *decor = calloc(1, sizeof(*decor));
    if (!decor) {
        return;
    }
    decor->server = server;
    decor->wlr_decor = wlr_decor;

    decor->request_mode.notify = decor_handle_request_mode;
    wl_signal_add(&wlr_decor->events.request_mode, &decor->request_mode);

    decor->destroy.notify = decor_handle_destroy;
    wl_signal_add(&wlr_decor->events.destroy, &decor->destroy);

    enum wlr_xdg_toplevel_decoration_v1_mode mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
    if (!server->config->decor.force_ssd &&
        wlr_decor->requested_mode != WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_NONE) {
        mode = wlr_decor->requested_mode;
    }
    if (wlr_decor->toplevel->base->initialized) {
        wlr_xdg_toplevel_decoration_v1_set_mode(wlr_decor, mode);
    } else {
        wlr_decor->scheduled_mode = mode;
    }
}

static void server_handle_new_server_decoration(struct wl_listener *listener, void *data)
{
    (void)listener;
    struct wlr_server_decoration *decor = data;
    decor->mode = WLR_SERVER_DECORATION_MANAGER_MODE_SERVER;
}

static void server_handle_new_layer_surface(struct wl_listener *listener, void *data)
{
    struct north_server *server = wl_container_of(listener, server, new_layer_surface);
    struct wlr_layer_surface_v1 *layer_surface = data;
    layer_surface_handle_new(server, layer_surface);
}

struct north_idle_inhibitor {
    struct north_server *server;
    struct wlr_idle_inhibitor_v1 *wlr_inhibitor;
    struct wl_listener destroy;
};

static void idle_inhibitor_handle_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_idle_inhibitor *inhibitor = wl_container_of(listener, inhibitor, destroy);
    struct north_server *server = inhibitor->server;
    wl_list_remove(&inhibitor->destroy.link);
    free(inhibitor);

    server->idle_inhibit_count--;
    if (server->idle_notifier) {
        wlr_idle_notifier_v1_set_inhibited(server->idle_notifier, server->idle_inhibit_count > 0);
    }
}

static void server_handle_new_idle_inhibitor(struct wl_listener *listener, void *data)
{
    struct north_server *server = wl_container_of(listener, server, new_idle_inhibitor);
    struct wlr_idle_inhibitor_v1 *wlr_inhibitor = data;

    struct north_idle_inhibitor *inhibitor = calloc(1, sizeof(*inhibitor));
    if (!inhibitor)
        return;
    inhibitor->server = server;
    inhibitor->wlr_inhibitor = wlr_inhibitor;

    inhibitor->destroy.notify = idle_inhibitor_handle_destroy;
    wl_signal_add(&wlr_inhibitor->events.destroy, &inhibitor->destroy);

    server->idle_inhibit_count++;
    if (server->idle_notifier) {
        wlr_idle_notifier_v1_set_inhibited(server->idle_notifier, true);
    }
}

static void server_handle_new_output(struct wl_listener *listener, void *data)
{
    struct north_server *server = wl_container_of(listener, server, new_output);
    struct wlr_output *wlr_output = data;
    output_handle_new(server, wlr_output);
}

static void server_handle_backend_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_server *server = wl_container_of(listener, server, backend_destroy);
    LOG_INFO("Backend destroyed, terminating compositor");
    wl_display_terminate(server->display);
}

static void server_handle_new_input(struct wl_listener *listener, void *data)
{
    struct north_server *server = wl_container_of(listener, server, new_input);
    struct wlr_input_device *device = data;
    seat_add_device(&server->seat, device);
}

static void constraint_handle_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_server *server = wl_container_of(listener, server, constraint_destroy);
    wl_list_remove(&server->constraint_destroy.link);
    server->active_constraint = NULL;
}

void server_set_active_constraint(struct north_server *server,
                                  struct wlr_pointer_constraint_v1 *constraint)
{
    if (server->active_constraint == constraint)
        return;
    if (server->active_constraint) {
        wl_list_remove(&server->constraint_destroy.link);
        wlr_pointer_constraint_v1_send_deactivated(server->active_constraint);
        server->active_constraint = NULL;
    }
    if (constraint) {
        server->active_constraint = constraint;
        server->constraint_destroy.notify = constraint_handle_destroy;
        wl_signal_add(&constraint->events.destroy, &server->constraint_destroy);
        wlr_pointer_constraint_v1_send_activated(constraint);
    }
}

void server_break_pointer_constraint(struct north_server *server)
{
    if (server->active_constraint) {
        LOG_INFO("Pointer constraint manually broken via emergency keybind");
        server_set_active_constraint(server, NULL);
    }
}

static void server_handle_new_pointer_constraint(struct wl_listener *listener, void *data)
{
    struct north_server *server = wl_container_of(listener, server, new_pointer_constraint);
    struct wlr_pointer_constraint_v1 *constraint = data;
    struct wlr_surface *focused = server->seat.wlr_seat->pointer_state.focused_surface;
    if (focused && focused == constraint->surface) {
        server_set_active_constraint(server, constraint);
    }
}

static void server_handle_new_virtual_keyboard(struct wl_listener *listener, void *data)
{
    struct north_server *server = wl_container_of(listener, server, new_virtual_keyboard);
    struct wlr_virtual_keyboard_v1 *vk = data;
    seat_add_keyboard(&server->seat, &vk->keyboard);
}

void server_set_session_locked(struct north_server *server, bool locked)
{
    wlr_scene_node_set_enabled(&server->layers.background->node, !locked);
    wlr_scene_node_set_enabled(&server->layers.bottom->node, !locked);
    wlr_scene_node_set_enabled(&server->layers.views->node, !locked);
    wlr_scene_node_set_enabled(&server->layers.top->node, !locked);
    wlr_scene_node_set_enabled(&server->layers.fullscreen->node, !locked);
    wlr_scene_node_set_enabled(&server->layers.overlay->node, !locked);
    wlr_scene_node_set_enabled(&server->layers.popups->node, !locked);
}

BoolResult server_init(struct north_server *server, NorthConfig *config)
{
    memset(server, 0, sizeof(*server));
    server->config = config;

    wl_list_init(&server->outputs);
    wl_list_init(&server->views);

    server->display = wl_display_create();
    if (!server->display) {
        return RESULT_ERR(Bool, ERROR_CREATE(ERR_INTERNAL, "failed creating wl_display"));
    }
    server->loop = wl_display_get_event_loop(server->display);

    server->backend = wlr_backend_autocreate(server->loop, &server->session);
    if (!server->backend) {
        return RESULT_ERR(Bool, ERROR_CREATE(ERR_INTERNAL, "failed autocreating wlr_backend"));
    }

    server->renderer = wlr_renderer_autocreate(server->backend);
    if (!server->renderer) {
        return RESULT_ERR(Bool, ERROR_CREATE(ERR_INTERNAL, "failed autocreating wlr_renderer"));
    }
    wlr_renderer_init_wl_display(server->renderer, server->display);

    server->allocator = wlr_allocator_autocreate(server->backend, server->renderer);
    if (!server->allocator) {
        return RESULT_ERR(Bool, ERROR_CREATE(ERR_INTERNAL, "failed autocreating wlr_allocator"));
    }

    server->compositor = wlr_compositor_create(server->display, 6, server->renderer);
    server->subcompositor = wlr_subcompositor_create(server->display);
    wlr_data_device_manager_create(server->display);
    wlr_primary_selection_v1_device_manager_create(server->display);

    server->scene = wlr_scene_create();
    server->output_layout = wlr_output_layout_create(server->display);
    server->scene_layout = wlr_scene_attach_output_layout(server->scene, server->output_layout);

    server->layers.background = wlr_scene_tree_create(&server->scene->tree);
    server->layers.bottom = wlr_scene_tree_create(&server->scene->tree);
    server->layers.views = wlr_scene_tree_create(&server->scene->tree);
    server->layers.top = wlr_scene_tree_create(&server->scene->tree);
    server->layers.fullscreen = wlr_scene_tree_create(&server->scene->tree);
    server->layers.overlay = wlr_scene_tree_create(&server->scene->tree);
    server->layers.popups = wlr_scene_tree_create(&server->scene->tree);
    server->layers.lock = wlr_scene_tree_create(&server->scene->tree);

    server->snap_preview_tree = wlr_scene_tree_create(server->layers.overlay);
    float fill_color[4] = {
        server->config->snap.preview_color.r, server->config->snap.preview_color.g,
        server->config->snap.preview_color.b, server->config->snap.preview_color.a};
    server->snap_preview_fill = wlr_scene_rect_create(server->snap_preview_tree, 0, 0, fill_color);
    float border_color[4] = {
        server->config->snap.preview_border_color.r, server->config->snap.preview_border_color.g,
        server->config->snap.preview_border_color.b, server->config->snap.preview_border_color.a};
    for (int i = 0; i < 4; i++) {
        server->snap_preview_borders[i] =
            wlr_scene_rect_create(server->snap_preview_tree, 0, 0, border_color);
    }
    wlr_scene_node_set_enabled(&server->snap_preview_tree->node, false);

    seat_init(&server->seat, server);

    server->xdg_shell = wlr_xdg_shell_create(server->display, 6);
    server->xdg_decoration_manager = wlr_xdg_decoration_manager_v1_create(server->display);
    server->server_decoration_manager = wlr_server_decoration_manager_create(server->display);
    wlr_server_decoration_manager_set_default_mode(server->server_decoration_manager,
                                                   WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);

    server->cursor_shape_manager = wlr_cursor_shape_manager_v1_create(server->display, 1);
    server->viewporter = wlr_viewporter_create(server->display);
    server->fractional_scale_manager = wlr_fractional_scale_manager_v1_create(server->display, 1);
    wlr_single_pixel_buffer_manager_v1_create(server->display);
    server->presentation = wlr_presentation_create(server->display, server->backend, 2);
    server->tearing_control = wlr_tearing_control_manager_v1_create(server->display, 1);

    int drm_fd = wlr_renderer_get_drm_fd(server->renderer);
    if (drm_fd >= 0) {
        server->syncobj_manager =
            wlr_linux_drm_syncobj_manager_v1_create(server->display, 1, drm_fd);
        if (server->syncobj_manager) {
            LOG_INFO("Explicit sync (linux-drm-syncobj-v1) initialized with DRM fd %d", drm_fd);
        } else {
            LOG_WARN("Failed to create linux-drm-syncobj-v1 manager");
        }
    } else {
        LOG_INFO("DRM fd not available from renderer, explicit sync disabled");
    }

    wlr_xdg_output_manager_v1_create(server->display, server->output_layout);
    server->output_manager = wlr_output_manager_v1_create(server->display);

    struct wlr_linux_dmabuf_v1 *dmabuf =
        wlr_linux_dmabuf_v1_create_with_renderer(server->display, 5, server->renderer);
    if (dmabuf) {
        wlr_scene_set_linux_dmabuf_v1(server->scene, dmabuf);
    }

    server->new_output.notify = server_handle_new_output;
    wl_signal_add(&server->backend->events.new_output, &server->new_output);

    server->new_input.notify = server_handle_new_input;
    wl_signal_add(&server->backend->events.new_input, &server->new_input);

    server->new_xdg_toplevel.notify = server_handle_new_xdg_toplevel;
    wl_signal_add(&server->xdg_shell->events.new_toplevel, &server->new_xdg_toplevel);

    server->new_xdg_popup.notify = server_handle_new_xdg_popup;
    wl_signal_add(&server->xdg_shell->events.new_popup, &server->new_xdg_popup);

    server->new_decoration.notify = server_handle_new_decoration;
    wl_signal_add(&server->xdg_decoration_manager->events.new_toplevel_decoration,
                  &server->new_decoration);

    server->new_server_decoration.notify = server_handle_new_server_decoration;
    wl_signal_add(&server->server_decoration_manager->events.new_decoration,
                  &server->new_server_decoration);

    server->layer_shell = wlr_layer_shell_v1_create(server->display, 4);
    server->new_layer_surface.notify = server_handle_new_layer_surface;
    wl_signal_add(&server->layer_shell->events.new_surface, &server->new_layer_surface);

    server->foreign_toplevel_manager = wlr_foreign_toplevel_manager_v1_create(server->display);

    server->idle_notifier = wlr_idle_notifier_v1_create(server->display);

    server->idle_inhibit_manager = wlr_idle_inhibit_v1_create(server->display);
    server->new_idle_inhibitor.notify = server_handle_new_idle_inhibitor;
    wl_signal_add(&server->idle_inhibit_manager->events.new_inhibitor, &server->new_idle_inhibitor);

    session_lock_init(server);

    server->pointer_constraints = wlr_pointer_constraints_v1_create(server->display);
    server->new_pointer_constraint.notify = server_handle_new_pointer_constraint;
    wl_signal_add(&server->pointer_constraints->events.new_constraint,
                  &server->new_pointer_constraint);

    server->relative_pointer_manager = wlr_relative_pointer_manager_v1_create(server->display);

    server->screencopy_manager = wlr_screencopy_manager_v1_create(server->display);

    server->image_copy_capture_manager =
        wlr_ext_image_copy_capture_manager_v1_create(server->display, 1);

    server->virtual_keyboard_manager = wlr_virtual_keyboard_manager_v1_create(server->display);
    server->new_virtual_keyboard.notify = server_handle_new_virtual_keyboard;
    wl_signal_add(&server->virtual_keyboard_manager->events.new_virtual_keyboard,
                  &server->new_virtual_keyboard);

    xwayland_init(server);

    server->backend_destroy.notify = server_handle_backend_destroy;
    wl_signal_add(&server->backend->events.destroy, &server->backend_destroy);

    return RESULT_OK(Bool, true);
}

BoolResult server_start(struct north_server *server, const char **socket_out)
{
    const char *socket = wl_display_add_socket_auto(server->display);
    if (!socket) {
        return RESULT_ERR(Bool, ERROR_CREATE(ERR_IO, "failed to add Wayland display socket"));
    }

    if (!wlr_backend_start(server->backend)) {
        return RESULT_ERR(Bool, ERROR_CREATE(ERR_INTERNAL, "failed starting wlr_backend"));
    }

    if (socket_out) {
        *socket_out = socket;
    }
    return RESULT_OK(Bool, true);
}

void server_finish(struct north_server *server)
{
    wl_list_remove(&server->new_output.link);
    wl_list_remove(&server->new_input.link);
    wl_list_remove(&server->new_xdg_toplevel.link);
    wl_list_remove(&server->new_xdg_popup.link);
    wl_list_remove(&server->new_decoration.link);
    wl_list_remove(&server->new_server_decoration.link);
    wl_list_remove(&server->new_layer_surface.link);
    wl_list_remove(&server->new_idle_inhibitor.link);
    wl_list_remove(&server->new_pointer_constraint.link);
    wl_list_remove(&server->new_virtual_keyboard.link);
    wl_list_remove(&server->backend_destroy.link);

    server_set_active_constraint(server, NULL);
    session_lock_finish(server);

    struct north_view *view, *tmp_view;
    wl_list_for_each_safe(view, tmp_view, &server->views, link) { view_destroy(view); }

    xwayland_finish(server);
    seat_finish(&server->seat);

    struct north_output *output, *tmp_output;
    wl_list_for_each_safe(output, tmp_output, &server->outputs, link)
    {
        wlr_output_destroy(output->wlr_output);
    }

    wlr_scene_node_destroy(&server->scene->tree.node);
    wlr_output_layout_destroy(server->output_layout);

    wlr_allocator_destroy(server->allocator);
    wlr_renderer_destroy(server->renderer);
    wlr_backend_destroy(server->backend);
    wl_display_destroy_clients(server->display);
    wl_display_destroy(server->display);
}
