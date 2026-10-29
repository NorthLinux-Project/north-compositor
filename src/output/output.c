#include "output/output.h"
#include "core/server.h"
#include "shell/placement.h"
#include "util/log.h"
#include <wlr/backend/wayland.h>
#include <stdlib.h>
#include <time.h>

static void output_frame(struct wl_listener *listener, void *data) {
    (void)data;
    struct north_output *output = wl_container_of(listener, output, frame);
    struct wlr_scene_output *scene_output = output->scene_output;

    wlr_scene_output_commit(scene_output, NULL);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_request_state(struct wl_listener *listener, void *data) {
    struct north_output *output = wl_container_of(listener, output, request_state);
    const struct wlr_output_event_request_state *event = data;
    wlr_output_commit_state(output->wlr_output, event->state);
}

static void output_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct north_output *output = wl_container_of(listener, output, destroy);
    struct north_server *server = output->server;
    bool is_nested = wlr_output_is_wl(output->wlr_output);

    LOG_INFO("Output '%s' disconnected", output->wlr_output->name);

    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->request_state.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);
    free(output);

    if (is_nested && wl_list_empty(&server->outputs)) {
        LOG_INFO("All nested outputs closed, terminating compositor");
        wl_display_terminate(server->display);
    }
}

void output_handle_new(struct north_server *server, struct wlr_output *wlr_output) {
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

    LOG_INFO("Output '%s' configured: mode %dx%d, scale %.2f",
             wlr_output->name, wlr_output->width, wlr_output->height, scale);
}

NorthBox output_get_usable_box(struct north_output *output) {
    if (!output || !output->wlr_output) {
        return (NorthBox){0};
    }
    struct wlr_box box;
    wlr_output_layout_get_box(output->server->output_layout, output->wlr_output, &box);
    return (NorthBox){
        .x = box.x,
        .y = box.y,
        .width = box.width,
        .height = box.height
    };
}

struct north_output *output_at(struct north_server *server, double lx, double ly) {
    struct wlr_output *wlr_out = wlr_output_layout_output_at(server->output_layout, lx, ly);
    if (!wlr_out) {
        return NULL;
    }

    struct north_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (output->wlr_output == wlr_out) {
            return output;
        }
    }
    return NULL;
}
