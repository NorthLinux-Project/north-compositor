#include "shell/workspace.h"

#include "core/server.h"
#include "output/output.h"
#include "shell/view.h"
#include "util/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void workspace_output_init(struct north_output *output, int count)
{
    if (!output) {
        return;
    }

    if (count < 1) {
        count = 1;
    } else if (count > 32) {
        count = 32;
    }

    wl_list_init(&output->workspaces);
    output->active_workspace = NULL;
    output->workspace_group = NULL;

    if (output->server && output->server->ext_workspace_manager) {
        output->workspace_group =
            wlr_ext_workspace_group_handle_v1_create(output->server->ext_workspace_manager, 0);
        if (output->workspace_group) {
            output->workspace_group->data = output;
            if (output->wlr_output) {
                wlr_ext_workspace_group_handle_v1_output_enter(output->workspace_group,
                                                               output->wlr_output);
            }
        } else {
            LOG_ERROR("Failed to create workspace group handle for output");
        }
    }

    for (int i = 0; i < count; i++) {
        struct north_workspace *ws = calloc(1, sizeof(*ws));
        if (!ws) {
            LOG_ERROR("Failed to allocate workspace %d", i);
            continue;
        }

        ws->output = output;
        ws->index = i;
        snprintf(ws->name, sizeof(ws->name), "%d", i + 1);

        ws->tree = wlr_scene_tree_create(output->server->layers.views);
        if (!ws->tree) {
            LOG_ERROR("Failed to create scene tree for workspace %d", i);
            free(ws);
            continue;
        }

        if (output->workspace_group && output->server && output->server->ext_workspace_manager) {
            uint32_t caps = EXT_WORKSPACE_HANDLE_V1_WORKSPACE_CAPABILITIES_ACTIVATE;
            ws->handle = wlr_ext_workspace_handle_v1_create(output->server->ext_workspace_manager,
                                                            ws->name, caps);
            if (ws->handle) {
                ws->handle->data = ws;
                wlr_ext_workspace_handle_v1_set_group(ws->handle, output->workspace_group);
                wlr_ext_workspace_handle_v1_set_name(ws->handle, ws->name);
                wlr_ext_workspace_handle_v1_set_active(ws->handle, i == 0);
            } else {
                LOG_ERROR("Failed to create workspace handle for workspace %d", i);
            }
        }

        if (i == 0) {
            wlr_scene_node_set_enabled(&ws->tree->node, true);
            output->active_workspace = ws;
        } else {
            wlr_scene_node_set_enabled(&ws->tree->node, false);
        }

        wl_list_insert(output->workspaces.prev, &ws->link);
    }
}

void workspace_output_finish(struct north_output *output)
{
    if (!output) {
        return;
    }

    struct north_workspace *ws, *tmp;
    wl_list_for_each_safe(ws, tmp, &output->workspaces, link)
    {
        wl_list_remove(&ws->link);
        if (ws->handle) {
            wlr_ext_workspace_handle_v1_destroy(ws->handle);
            ws->handle = NULL;
        }
        if (ws->tree) {
            wlr_scene_node_destroy(&ws->tree->node);
            ws->tree = NULL;
        }
        free(ws);
    }

    if (output->workspace_group) {
        wlr_ext_workspace_group_handle_v1_destroy(output->workspace_group);
        output->workspace_group = NULL;
    }

    output->active_workspace = NULL;
}

struct north_workspace *workspace_get(struct north_output *output, int index)
{
    if (!output || index < 0) {
        return NULL;
    }

    struct north_workspace *ws;
    wl_list_for_each(ws, &output->workspaces, link)
    {
        if (ws->index == index) {
            return ws;
        }
    }
    return NULL;
}

struct north_workspace *workspace_active(struct north_output *output)
{
    if (!output) {
        return NULL;
    }
    return output->active_workspace;
}

void workspace_switch(struct north_output *output, int index)
{
    if (!output) {
        return;
    }

    struct north_workspace *target = workspace_get(output, index);
    if (!target) {
        return;
    }

    struct north_workspace *current = output->active_workspace;
    if (current == target) {
        return;
    }

    if (current) {
        wlr_scene_node_set_enabled(&current->tree->node, false);
        if (current->handle) {
            wlr_ext_workspace_handle_v1_set_active(current->handle, false);
        }
    }

    output->active_workspace = target;
    wlr_scene_node_set_enabled(&target->tree->node, true);
    if (target->handle) {
        wlr_ext_workspace_handle_v1_set_active(target->handle, true);
    }

    struct north_server *server = output->server;
    struct north_view *top_view = NULL;
    struct north_view *v;
    wl_list_for_each(v, &server->views, link)
    {
        if (v->workspace == target) {
            top_view = v;
            break;
        }
    }

    if (top_view) {
        view_focus(top_view);
    } else {
        struct wlr_surface *prev_surface = server->seat.wlr_seat->keyboard_state.focused_surface;
        if (prev_surface) {
            struct north_view *prev_view;
            wl_list_for_each(prev_view, &server->views, link)
            {
                if (prev_view->surface == prev_surface) {
                    if (prev_view->workspace && prev_view->workspace->output == output) {
                        view_clear_focus(server);
                    }
                    break;
                }
            }
        }
    }
}

void workspace_cycle(struct north_output *output, int step)
{
    if (!output || wl_list_empty(&output->workspaces)) {
        return;
    }

    int count = wl_list_length(&output->workspaces);
    if (count <= 1) {
        return;
    }

    int cur = output->active_workspace ? output->active_workspace->index : 0;
    int next = (cur + step) % count;
    if (next < 0) {
        next += count;
    }

    workspace_switch(output, next);
}

bool workspace_view_is_visible(const struct north_view *view)
{
    if (!view || !view->workspace || !view->workspace->output) {
        return false;
    }
    return view->workspace == view->workspace->output->active_workspace;
}

void workspace_move_view(struct north_view *view, int target_index)
{
    if (!view || !view->workspace || !view->workspace->output) {
        return;
    }

    struct north_output *output = view->workspace->output;
    struct north_workspace *target_ws = workspace_get(output, target_index);
    if (!target_ws || target_ws == view->workspace) {
        return;
    }

    struct north_server *server = view->server;
    bool was_focused = false;
    if (server && server->seat.wlr_seat) {
        was_focused = (server->seat.wlr_seat->keyboard_state.focused_surface == view->surface);
    }
    bool was_active = (view->workspace == output->active_workspace);

    wlr_scene_node_reparent(&view->tree->node, target_ws->tree);
    wlr_scene_node_raise_to_top(&view->tree->node);
    view->workspace = target_ws;

    if (was_focused && was_active && target_ws != output->active_workspace) {
        wl_list_remove(&view->link);
        wl_list_insert(server->views.prev, &view->link);
        view_focus_top_visible(server);
    } else if (!was_active && target_ws == output->active_workspace) {
        if (server && server->seat.wlr_seat &&
            !server->seat.wlr_seat->keyboard_state.focused_surface) {
            view_focus(view);
        }
    }
}

void workspace_resize_count(struct north_output *output, int new_count)
{
    if (!output) {
        return;
    }

    if (new_count < 1) {
        new_count = 1;
    } else if (new_count > 32) {
        new_count = 32;
    }

    int old_count = wl_list_length(&output->workspaces);
    if (new_count == old_count) {
        return;
    }

    if (new_count > old_count) {
        for (int i = old_count; i < new_count; i++) {
            struct north_workspace *ws = calloc(1, sizeof(*ws));
            if (!ws) {
                LOG_ERROR("Failed to allocate workspace %d during resize", i);
                continue;
            }
            ws->output = output;
            ws->index = i;
            snprintf(ws->name, sizeof(ws->name), "%d", i + 1);
            ws->tree = wlr_scene_tree_create(output->server->layers.views);
            if (!ws->tree) {
                LOG_ERROR("Failed to create scene tree for workspace %d during resize", i);
                free(ws);
                continue;
            }
            wlr_scene_node_set_enabled(&ws->tree->node, false);

            if (output->workspace_group && output->server &&
                output->server->ext_workspace_manager) {
                uint32_t caps = EXT_WORKSPACE_HANDLE_V1_WORKSPACE_CAPABILITIES_ACTIVATE;
                ws->handle = wlr_ext_workspace_handle_v1_create(
                    output->server->ext_workspace_manager, ws->name, caps);
                if (ws->handle) {
                    ws->handle->data = ws;
                    wlr_ext_workspace_handle_v1_set_group(ws->handle, output->workspace_group);
                    wlr_ext_workspace_handle_v1_set_name(ws->handle, ws->name);
                    wlr_ext_workspace_handle_v1_set_active(ws->handle, false);
                } else {
                    LOG_ERROR("Failed to create workspace handle for workspace %d during resize",
                              i);
                }
            }

            wl_list_insert(output->workspaces.prev, &ws->link);
        }
    } else {
        int last_valid_idx = new_count - 1;

        if (output->active_workspace && output->active_workspace->index >= new_count) {
            workspace_switch(output, last_valid_idx);
        }

        struct north_view *v, *tmp_v;
        wl_list_for_each_safe(v, tmp_v, &output->server->views, link)
        {
            if (v->workspace && v->workspace->output == output &&
                v->workspace->index >= new_count) {
                workspace_move_view(v, last_valid_idx);
            }
        }

        struct north_workspace *ws, *tmp;
        wl_list_for_each_safe(ws, tmp, &output->workspaces, link)
        {
            if (ws->index >= new_count) {
                wl_list_remove(&ws->link);
                if (ws->handle) {
                    wlr_ext_workspace_handle_v1_destroy(ws->handle);
                    ws->handle = NULL;
                }
                if (ws->tree) {
                    wlr_scene_node_destroy(&ws->tree->node);
                    ws->tree = NULL;
                }
                free(ws);
            }
        }
    }
}

void workspace_migrate_views_to_output(struct north_output *src, struct north_output *dst)
{
    if (!src || !dst || src == dst || !dst->active_workspace) {
        return;
    }

    struct north_workspace *target_ws = dst->active_workspace;
    NorthBox usable = output_get_usable_box(dst);

    struct north_view *view;
    wl_list_for_each(view, &src->server->views, link)
    {
        if (view->workspace && view->workspace->output == src) {
            wlr_scene_node_reparent(&view->tree->node, target_ws->tree);
            view->workspace = target_ws;

            if (view->state == VIEW_MAXIMIZED) {
                int tb_h = view->decor.enabled ? view->server->config->decor.titlebar_height : 0;
                view_set_position(view, usable.x, usable.y + tb_h);
                view_set_size(view, usable.width, usable.height - tb_h);
            } else if (view->state == VIEW_FULLSCREEN) {
                struct wlr_box lbox;
                wlr_output_layout_get_box(dst->server->output_layout, dst->wlr_output, &lbox);
                view_set_position(view, lbox.x, lbox.y);
                view_set_size(view, lbox.width, lbox.height);
            } else {
                int vx = view->geometry.x;
                int vy = view->geometry.y;
                bool outside =
                    (vx + view->geometry.width <= usable.x || vx >= usable.x + usable.width ||
                     vy + view->geometry.height <= usable.y || vy >= usable.y + usable.height);
                if (outside) {
                    int new_x = usable.x + (usable.width - view->geometry.width) / 2;
                    int new_y = usable.y + (usable.height - view->geometry.height) / 2;
                    if (new_x < usable.x) {
                        new_x = usable.x;
                    }
                    if (new_y < usable.y) {
                        new_y = usable.y;
                    }
                    view_set_position(view, new_x, new_y);
                }
            }

            if (view->toplevel_handle) {
                if (src->wlr_output) {
                    wlr_foreign_toplevel_handle_v1_output_leave(view->toplevel_handle,
                                                                src->wlr_output);
                }
                if (dst->wlr_output) {
                    wlr_foreign_toplevel_handle_v1_output_enter(view->toplevel_handle,
                                                                dst->wlr_output);
                }
            }

            float scale = dst->wlr_output ? dst->wlr_output->scale : 1.0f;
            decoration_update(&view->decor, view->geometry.width, view->geometry.height, scale);
        }
    }
}

static void workspace_handle_commit(struct wl_listener *listener, void *data)
{
    struct north_server *server = wl_container_of(listener, server, workspace_commit);
    struct wlr_ext_workspace_v1_commit_event *event = data;

    if (!event || !event->requests) {
        return;
    }

    struct wlr_ext_workspace_v1_request *request;
    wl_list_for_each(request, event->requests, link)
    {
        switch (request->type) {
        case WLR_EXT_WORKSPACE_V1_REQUEST_ACTIVATE: {
            if (!request->activate.workspace) {
                break;
            }
            struct north_workspace *ws = request->activate.workspace->data;
            if (!ws) {
                struct north_output *out;
                wl_list_for_each(out, &server->outputs, link)
                {
                    struct north_workspace *w;
                    wl_list_for_each(w, &out->workspaces, link)
                    {
                        if (w->handle == request->activate.workspace) {
                            ws = w;
                            break;
                        }
                    }
                    if (ws) {
                        break;
                    }
                }
            }
            if (ws && ws->output) {
                workspace_switch(ws->output, ws->index);
            }
            break;
        }
        case WLR_EXT_WORKSPACE_V1_REQUEST_ASSIGN:
            LOG_DEBUG("ext-workspace-v1: workspace group reassignment not supported");
            break;
        case WLR_EXT_WORKSPACE_V1_REQUEST_DEACTIVATE:
            LOG_DEBUG("ext-workspace-v1: workspace deactivation request ignored");
            break;
        case WLR_EXT_WORKSPACE_V1_REQUEST_CREATE_WORKSPACE:
            LOG_DEBUG("ext-workspace-v1: dynamic workspace creation not supported");
            break;
        case WLR_EXT_WORKSPACE_V1_REQUEST_REMOVE:
            LOG_DEBUG("ext-workspace-v1: dynamic workspace removal not supported");
            break;
        }
    }
}

void workspace_manager_init(struct north_server *server)
{
    if (!server || !server->display) {
        return;
    }

    server->ext_workspace_manager = wlr_ext_workspace_manager_v1_create(server->display, 1);
    if (!server->ext_workspace_manager) {
        LOG_ERROR("Failed to create ext-workspace-v1 manager");
        return;
    }

    server->workspace_commit.notify = workspace_handle_commit;
    wl_signal_add(&server->ext_workspace_manager->events.commit, &server->workspace_commit);
}

void workspace_manager_finish(struct north_server *server)
{
    if (!server || !server->ext_workspace_manager) {
        return;
    }

    wl_list_remove(&server->workspace_commit.link);
    server->ext_workspace_manager = NULL;
}
