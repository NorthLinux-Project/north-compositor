#include "shell/lock.h"

#include "core/server.h"
#include "output/output.h"
#include "shell/view.h"
#include "util/log.h"

#include <stdlib.h>

static bool lock_all_outputs_committed(struct north_session_lock *lock)
{
    struct north_server *server = lock->server;
    struct north_output *output;
    wl_list_for_each(output, &server->outputs, link)
    {
        bool found = false;
        struct north_session_lock_surface *surf;
        wl_list_for_each(surf, &lock->surfaces, link)
        {
            if (surf->output == output && surf->committed) {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    return true;
}

static void lock_surface_handle_commit(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_session_lock_surface *surf = wl_container_of(listener, surf, commit);
    if (!surf->committed && wlr_surface_has_buffer(surf->wlr_surface->surface)) {
        surf->committed = true;
    }

    if (!surf->lock->locked_sent && lock_all_outputs_committed(surf->lock)) {
        LOG_INFO("All outputs locked, sending session_lock.locked event");
        wlr_session_lock_v1_send_locked(surf->lock->wlr_lock);
        surf->lock->locked_sent = true;
    }
}

static void lock_surface_handle_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_session_lock_surface *surf = wl_container_of(listener, surf, destroy);
    wl_list_remove(&surf->commit.link);
    wl_list_remove(&surf->destroy.link);
    wl_list_remove(&surf->link);
    free(surf);
}

static void lock_handle_new_surface(struct wl_listener *listener, void *data)
{
    struct north_session_lock *lock = wl_container_of(listener, lock, new_surface);
    struct wlr_session_lock_surface_v1 *wlr_surface = data;
    struct north_server *server = lock->server;

    struct north_output *output = NULL;
    struct north_output *out;
    wl_list_for_each(out, &server->outputs, link)
    {
        if (out->wlr_output == wlr_surface->output) {
            output = out;
            break;
        }
    }

    if (!output) {
        LOG_WARN("Session lock surface requested for unknown output");
        return;
    }

    struct north_session_lock_surface *surf = calloc(1, sizeof(*surf));
    if (!surf)
        return;

    surf->lock = lock;
    surf->output = output;
    surf->wlr_surface = wlr_surface;

    surf->scene_tree = wlr_scene_subsurface_tree_create(server->layers.lock, wlr_surface->surface);

    struct wlr_box box;
    wlr_output_layout_get_box(server->output_layout, output->wlr_output, &box);
    if (surf->scene_tree) {
        wlr_scene_node_set_position(&surf->scene_tree->node, box.x, box.y);
    }

    surf->commit.notify = lock_surface_handle_commit;
    wl_signal_add(&wlr_surface->surface->events.commit, &surf->commit);

    surf->destroy.notify = lock_surface_handle_destroy;
    wl_signal_add(&wlr_surface->events.destroy, &surf->destroy);

    wl_list_insert(&lock->surfaces, &surf->link);

    wlr_session_lock_surface_v1_configure(wlr_surface, box.width, box.height);

    wlr_seat_keyboard_notify_enter(server->seat.wlr_seat, wlr_surface->surface, NULL, 0, NULL);
}

static void lock_handle_unlock(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_session_lock *lock = wl_container_of(listener, lock, unlock);
    struct north_server *server = lock->server;

    LOG_INFO("Session lock released by client, restoring desktop layers");
    lock->unlocked = true;

    server_set_session_locked(server, false);

    if (!wl_list_empty(&server->views)) {
        struct north_view *top = wl_container_of(server->views.next, top, link);
        view_focus(top);
    }
}

static void lock_handle_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct north_session_lock *lock = wl_container_of(listener, lock, destroy);
    struct north_server *server = lock->server;

    if (!lock->unlocked) {
        LOG_FATAL(
            "Lock screen client terminated without unlocking! Terminating session for security.");
        wl_display_terminate(server->display);
    }

    wl_list_remove(&lock->new_surface.link);
    wl_list_remove(&lock->unlock.link);
    wl_list_remove(&lock->destroy.link);

    server->session_lock = NULL;
    free(lock);
}

void session_lock_handle_new_lock(struct wl_listener *listener, void *data)
{
    struct north_server *server = wl_container_of(listener, server, new_session_lock);
    struct wlr_session_lock_v1 *wlr_lock = data;

    if (server->session_lock != NULL) {
        LOG_WARN("Session lock already active or pending, rejecting request");
        wlr_session_lock_v1_destroy(wlr_lock);
        return;
    }

    LOG_INFO("New session lock requested");
    struct north_session_lock *lock = calloc(1, sizeof(*lock));
    if (!lock) {
        wlr_session_lock_v1_destroy(wlr_lock);
        return;
    }

    lock->server = server;
    lock->wlr_lock = wlr_lock;
    wl_list_init(&lock->surfaces);

    lock->new_surface.notify = lock_handle_new_surface;
    wl_signal_add(&wlr_lock->events.new_surface, &lock->new_surface);

    lock->unlock.notify = lock_handle_unlock;
    wl_signal_add(&wlr_lock->events.unlock, &lock->unlock);

    lock->destroy.notify = lock_handle_destroy;
    wl_signal_add(&wlr_lock->events.destroy, &lock->destroy);

    server->session_lock = lock;

    server_set_session_locked(server, true);

    if (wl_list_empty(&server->outputs)) {
        wlr_session_lock_v1_send_locked(wlr_lock);
        lock->locked_sent = true;
    }
}

void session_lock_init(struct north_server *server)
{
    server->session_lock_manager = wlr_session_lock_manager_v1_create(server->display);
    if (!server->session_lock_manager) {
        LOG_ERROR("Failed to create ext_session_lock_manager_v1");
        return;
    }

    server->new_session_lock.notify = session_lock_handle_new_lock;
    wl_signal_add(&server->session_lock_manager->events.new_lock, &server->new_session_lock);
}

void session_lock_finish(struct north_server *server)
{
    if (server->session_lock_manager) {
        wl_list_remove(&server->new_session_lock.link);
        server->session_lock_manager = NULL;
    }
}
