#include "output/output.h"

#include "core/server.h"
#include "shell/layer.h"
#include "shell/placement.h"
#include "shell/view.h"
#include "shell/workspace.h"
#include "util/log.h"

#include <stdlib.h>
#include <time.h>
#include <wlr/backend/wayland.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_tearing_control_v1.h>

static void output_frame(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_output *output = wl_container_of(listener, output, frame);
    struct wlr_scene_output *scene_output = output->scene_output;

    if (!wlr_scene_output_needs_frame(scene_output)) {
        return;
    }

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    if (!wlr_scene_output_build_state(scene_output, &state, NULL)) {
        wlr_output_state_finish(&state);
        return;
    }

    bool allow_tearing = false;
    if (output->server->tearing_control) {
        struct north_view *view;
        wl_list_for_each(view, &output->server->views, link)
        {
            if (view->state == VIEW_FULLSCREEN && view->surface) {
                struct wlr_box out_box;
                wlr_output_layout_get_box(output->server->output_layout, output->wlr_output,
                                          &out_box);
                struct wlr_box view_box = {
                    .x = view->geometry.x,
                    .y = view->geometry.y,
                    .width = view->geometry.width,
                    .height = view->geometry.height,
                };
                struct wlr_box intersection;
                if (wlr_box_intersection(&intersection, &out_box, &view_box)) {
                    enum wp_tearing_control_v1_presentation_hint hint =
                        wlr_tearing_control_manager_v1_surface_hint_from_surface(
                            output->server->tearing_control, view->surface);
                    if (hint == WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC) {
                        allow_tearing = true;
                    }
                    break;
                }
            }
        }
    }

    if (allow_tearing) {
        state.tearing_page_flip = true;
        if (!wlr_output_test_state(output->wlr_output, &state)) {
            state.tearing_page_flip = false;
        }
    }

    if (!wlr_output_commit_state(output->wlr_output, &state)) {
        if (state.tearing_page_flip) {
            state.tearing_page_flip = false;
            wlr_output_commit_state(output->wlr_output, &state);
        }
    }
    wlr_output_state_finish(&state);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_request_state(struct wl_listener *listener, void *data)
{
    struct north_output *output = wl_container_of(listener, output, request_state);
    const struct wlr_output_event_request_state *event = data;
    if (wlr_output_commit_state(output->wlr_output, event->state)) {
        server_update_output_manager_config(output->server);
    }
}

static void output_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_output *output = wl_container_of(listener, output, destroy);
    struct north_server *server = output->server;
    bool is_nested = wlr_output_is_wl(output->wlr_output);

    if (server->switcher.active && server->switcher.output == output) {
        switcher_cancel(server);
    }

    LOG_INFO("Output '%s' disconnected", output->wlr_output->name);

    struct north_output *target_output = NULL;
    struct north_output *other;
    wl_list_for_each(other, &server->outputs, link)
    {
        if (other != output) {
            target_output = other;
            break;
        }
    }

    if (target_output) {
        workspace_migrate_views_to_output(output, target_output);
    } else {
        struct north_view *view;
        wl_list_for_each(view, &server->views, link)
        {
            if (view->workspace && view->workspace->output == output) {
                view->saved_workspace_index = view->workspace->index;
                view->workspace = NULL;
                wlr_scene_node_reparent(&view->tree->node, server->layers.views);
            }
        }
    }

    workspace_output_finish(output);

    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->request_state.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);
    free(output);

    server_update_output_manager_config(server);

    if (is_nested && wl_list_empty(&server->outputs)) {
        LOG_INFO("All nested outputs closed, terminating compositor");
        wl_display_terminate(server->display);
    }
}

void output_handle_new(struct north_server *server, struct wlr_output *wlr_output)
{
    if (!wlr_output_init_render(wlr_output, server->allocator, server->renderer)) {
        LOG_ERROR("Failed to initialize renderer for output '%s'", wlr_output->name);
        return;
    }

    struct north_output *output = calloc(1, sizeof(*output));
    if (!output) {
        LOG_ERROR("Failed allocating north_output");
        return;
    }
    output->server = server;
    output->wlr_output = wlr_output;
    wl_list_init(&output->layer_surfaces);

    output->scene_output = wlr_scene_output_create(server->scene, wlr_output);
    if (!output->scene_output) {
        LOG_ERROR("Failed to create scene output for '%s'", wlr_output->name);
        free(output);
        return;
    }

    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);

    output->request_state.notify = output_request_state;
    wl_signal_add(&wlr_output->events.request_state, &output->request_state);

    output->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);

    wl_list_insert(&server->outputs, &output->link);

    const NorthOutputConfig *ocfg = config_find_output(server->config, wlr_output->name);

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, ocfg ? ocfg->enabled : true);

    if (ocfg && ocfg->width > 0 && ocfg->height > 0) {
        wlr_output_state_set_custom_mode(&state, ocfg->width, ocfg->height,
                                         ocfg->refresh_mhz > 0 ? ocfg->refresh_mhz : 60000);
    } else {
        struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
        if (mode) {
            wlr_output_state_set_mode(&state, mode);
        }
    }

    float scale = 1.0f;
    if (ocfg && ocfg->scale > 0.0f) {
        scale = ocfg->scale;
    } else {
        scale = placement_default_scale(wlr_output->phys_width, wlr_output->width);
    }
    wlr_output_state_set_scale(&state, scale);

    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    int pos_x = ocfg ? ocfg->x : 0;
    int pos_y = ocfg ? ocfg->y : 0;
    if (ocfg) {
        wlr_output_layout_add(server->output_layout, wlr_output, pos_x, pos_y);
    } else {
        wlr_output_layout_add_auto(server->output_layout, wlr_output);
    }

    struct wlr_box lbox;
    wlr_output_layout_get_box(server->output_layout, wlr_output, &lbox);
    output->usable_area = (NorthBox){lbox.x, lbox.y, lbox.width, lbox.height};

    int ws_count = 9;
    if (server->config && server->config->workspaces.count > 0) {
        ws_count = server->config->workspaces.count;
    }
    workspace_output_init(output, ws_count);

    struct north_view *v;
    wl_list_for_each(v, &server->views, link)
    {
        if (!v->workspace) {
            struct north_workspace *ws = workspace_get(output, v->saved_workspace_index);
            if (!ws) {
                ws = output->active_workspace;
            }
            v->workspace = ws;
            wlr_scene_node_reparent(&v->tree->node, ws->tree);
            if (v->toplevel_handle && output->wlr_output) {
                wlr_foreign_toplevel_handle_v1_output_enter(v->toplevel_handle, output->wlr_output);
            }
        }
    }

    view_focus_top_visible(server);

    LOG_INFO("Output '%s' configured: mode %dx%d, scale %.2f", wlr_output->name, wlr_output->width,
             wlr_output->height, scale);

    server_update_output_manager_config(server);
}

NorthBox output_get_usable_box(struct north_output *output)
{
    if (!output || !output->wlr_output) {
        return (NorthBox){0};
    }
    return output->usable_area;
}

struct north_output *output_at(struct north_server *server, double lx, double ly)
{
    struct wlr_output *wlr_out = wlr_output_layout_output_at(server->output_layout, lx, ly);
    if (!wlr_out) {
        return NULL;
    }

    struct north_output *output;
    wl_list_for_each(output, &server->outputs, link)
    {
        if (output->wlr_output == wlr_out) {
            return output;
        }
    }
    return NULL;
}

void server_update_output_manager_config(struct north_server *server)
{
    if (!server || !server->output_manager) {
        return;
    }

    struct wlr_output_configuration_v1 *config = wlr_output_configuration_v1_create();
    if (!config) {
        LOG_ERROR("Failed to allocate output configuration");
        return;
    }

    struct north_output *output;
    wl_list_for_each(output, &server->outputs, link)
    {
        struct wlr_output_configuration_head_v1 *config_head =
            wlr_output_configuration_head_v1_create(config, output->wlr_output);
        if (!config_head) {
            continue;
        }

        if (output->wlr_output->enabled) {
            struct wlr_box box;
            wlr_output_layout_get_box(server->output_layout, output->wlr_output, &box);
            config_head->state.x = box.x;
            config_head->state.y = box.y;
        } else {
            config_head->state.x = 0;
            config_head->state.y = 0;
        }
    }

    wlr_output_manager_v1_set_configuration(server->output_manager, config);
}

static bool validate_head_state(struct north_server *server,
                                const struct wlr_output_configuration_head_v1 *config_head)
{
    if (!config_head || !config_head->state.output) {
        return false;
    }

    struct north_output *output = NULL;
    struct north_output *o;
    wl_list_for_each(o, &server->outputs, link)
    {
        if (o->wlr_output == config_head->state.output) {
            output = o;
            break;
        }
    }
    if (!output) {
        return false;
    }

    if (config_head->state.enabled) {
        if (config_head->state.scale < 0.25f || config_head->state.scale > 10.0f) {
            return false;
        }

        if (config_head->state.mode) {
            bool found = false;
            struct wlr_output_mode *m;
            wl_list_for_each(m, &output->wlr_output->modes, link)
            {
                if (m == config_head->state.mode) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false;
            }
        } else {
            if (config_head->state.custom_mode.width < 64 ||
                config_head->state.custom_mode.height < 64 ||
                config_head->state.custom_mode.width > 16384 ||
                config_head->state.custom_mode.height > 16384) {
                return false;
            }
        }
    }

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_head_v1_state_apply(&config_head->state, &state);
    bool ok = wlr_output_test_state(output->wlr_output, &state);
    wlr_output_state_finish(&state);
    return ok;
}

static bool is_output_enabled_in_config(const struct wlr_output_configuration_v1 *config,
                                        const struct north_output *output)
{
    if (!config || !output) {
        return false;
    }
    struct wlr_output_configuration_head_v1 *head;
    wl_list_for_each(head, &config->heads, link)
    {
        if (head->state.output == output->wlr_output) {
            return head->state.enabled;
        }
    }
    return output->wlr_output->enabled;
}

void output_manager_test(struct north_server *server, struct wlr_output_configuration_v1 *config)
{
    if (!config) {
        return;
    }

    bool ok = true;
    struct wlr_output_configuration_head_v1 *config_head;
    wl_list_for_each(config_head, &config->heads, link)
    {
        if (!validate_head_state(server, config_head)) {
            ok = false;
            break;
        }
    }

    if (ok) {
        wlr_output_configuration_v1_send_succeeded(config);
    } else {
        wlr_output_configuration_v1_send_failed(config);
    }
    wlr_output_configuration_v1_destroy(config);
}

void output_manager_apply(struct north_server *server, struct wlr_output_configuration_v1 *config)
{
    if (!config) {
        return;
    }

    bool ok = true;
    struct wlr_output_configuration_head_v1 *config_head;
    wl_list_for_each(config_head, &config->heads, link)
    {
        if (!validate_head_state(server, config_head)) {
            ok = false;
            break;
        }
    }

    if (!ok) {
        wlr_output_configuration_v1_send_failed(config);
        wlr_output_configuration_v1_destroy(config);
        return;
    }

    wl_list_for_each(config_head, &config->heads, link)
    {
        if (!config_head->state.enabled) {
            continue;
        }

        struct north_output *output = NULL;
        struct north_output *o;
        wl_list_for_each(o, &server->outputs, link)
        {
            if (o->wlr_output == config_head->state.output) {
                output = o;
                break;
            }
        }
        if (!output) {
            continue;
        }

        bool was_enabled = output->wlr_output->enabled;

        struct wlr_output_state state;
        wlr_output_state_init(&state);
        wlr_output_head_v1_state_apply(&config_head->state, &state);
        bool committed = wlr_output_commit_state(output->wlr_output, &state);
        wlr_output_state_finish(&state);

        if (!committed) {
            LOG_ERROR("Failed to commit state for output '%s'", output->wlr_output->name);
            continue;
        }

        wlr_output_layout_add(server->output_layout, output->wlr_output, config_head->state.x,
                              config_head->state.y);

        struct wlr_box lbox;
        wlr_output_layout_get_box(server->output_layout, output->wlr_output, &lbox);

        if (!was_enabled) {
            LOG_INFO("Re-enabling output '%s'", output->wlr_output->name);
            int ws_count = 9;
            if (server->config && server->config->workspaces.count > 0) {
                ws_count = server->config->workspaces.count;
            }
            workspace_output_init(output, ws_count);
            layer_arrange(output);

            struct north_view *view;
            wl_list_for_each(view, &server->views, link)
            {
                if (!view->workspace) {
                    struct north_workspace *ws = workspace_get(output, view->saved_workspace_index);
                    if (!ws) {
                        ws = output->active_workspace;
                    }
                    view->workspace = ws;
                    wlr_scene_node_reparent(&view->tree->node, ws->tree);
                    if (view->toplevel_handle && output->wlr_output) {
                        wlr_foreign_toplevel_handle_v1_output_enter(view->toplevel_handle,
                                                                    output->wlr_output);
                    }
                }
            }
        } else {
            layer_arrange(output);
        }

        struct north_view *view;
        wl_list_for_each(view, &server->views, link)
        {
            if (!view->workspace || view->workspace->output != output) {
                continue;
            }

            int tb_h =
                (view->decor.enabled && server->config) ? server->config->decor.titlebar_height : 0;
            if (view->state == VIEW_FULLSCREEN) {
                view_set_position(view, lbox.x, lbox.y);
                view_set_size(view, lbox.width, lbox.height);
            } else if (view->state == VIEW_MAXIMIZED) {
                view_set_position(view, output->usable_area.x, output->usable_area.y + tb_h);
                view_set_size(view, output->usable_area.width, output->usable_area.height - tb_h);
            } else if (view->state == VIEW_SNAPPED) {
                NorthBox snap_box =
                    placement_calculate_snap_box(output->usable_area, view->snap_zone, tb_h);
                view_set_position(view, snap_box.x, snap_box.y);
                view_set_size(view, snap_box.width, snap_box.height);
            } else if (view->state == VIEW_NORMAL) {
                int vx = view->geometry.x;
                int vy = view->geometry.y;
                NorthBox u = output->usable_area;
                if (vx >= u.x + u.width || vx + view->geometry.width <= u.x ||
                    vy >= u.y + u.height || vy + view->geometry.height <= u.y) {
                    int nx = u.x + (u.width - view->geometry.width) / 2;
                    int ny = u.y + (u.height - view->geometry.height) / 2;
                    if (nx < u.x) {
                        nx = u.x;
                    }
                    if (ny < u.y) {
                        ny = u.y;
                    }
                    view_set_position(view, nx, ny);
                }
            }

            float scale = output->wlr_output ? output->wlr_output->scale : 1.0f;
            decoration_update(&view->decor, view->geometry.width, view->geometry.height, scale);
        }
    }

    wl_list_for_each(config_head, &config->heads, link)
    {
        if (config_head->state.enabled) {
            continue;
        }

        struct north_output *output = NULL;
        struct north_output *o;
        wl_list_for_each(o, &server->outputs, link)
        {
            if (o->wlr_output == config_head->state.output) {
                output = o;
                break;
            }
        }
        if (!output) {
            continue;
        }

        bool was_enabled = output->wlr_output->enabled;

        struct wlr_output_state state;
        wlr_output_state_init(&state);
        wlr_output_head_v1_state_apply(&config_head->state, &state);
        bool committed = wlr_output_commit_state(output->wlr_output, &state);
        wlr_output_state_finish(&state);

        if (!committed) {
            LOG_ERROR("Failed to commit disable state for output '%s'", output->wlr_output->name);
            continue;
        }

        if (was_enabled) {
            LOG_INFO("Disabling output '%s'", output->wlr_output->name);
            wlr_output_layout_remove(server->output_layout, output->wlr_output);

            struct north_output *target_output = NULL;
            struct north_output *other;
            wl_list_for_each(other, &server->outputs, link)
            {
                if (other != output && is_output_enabled_in_config(config, other)) {
                    target_output = other;
                    break;
                }
            }

            if (target_output) {
                workspace_migrate_views_to_output(output, target_output);
            } else {
                struct north_view *view;
                wl_list_for_each(view, &server->views, link)
                {
                    if (view->workspace && view->workspace->output == output) {
                        view->saved_workspace_index = view->workspace->index;
                        view->workspace = NULL;
                        wlr_scene_node_reparent(&view->tree->node, server->layers.views);
                    }
                }
            }

            workspace_output_finish(output);
            output->usable_area = (NorthBox){0};
        }
    }

    view_focus_top_visible(server);
    wlr_output_configuration_v1_send_succeeded(config);
    wlr_output_configuration_v1_destroy(config);
    server_update_output_manager_config(server);
}
