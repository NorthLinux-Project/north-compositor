#ifndef SHELL_WORKSPACE_H
#define SHELL_WORKSPACE_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_ext_workspace_v1.h>
#include <wlr/types/wlr_scene.h>

struct north_output;
struct north_server;
struct north_view;

struct north_workspace {
    struct wl_list link;
    struct north_output *output;
    int index;
    char name[16];
    struct wlr_scene_tree *tree;
    struct wlr_ext_workspace_handle_v1 *handle;
};

void workspace_output_init(struct north_output *output, int count);
void workspace_output_finish(struct north_output *output);
void workspace_switch(struct north_output *output, int index);
void workspace_cycle(struct north_output *output, int step);
void workspace_move_view(struct north_view *view, int target_index);
struct north_workspace *workspace_get(struct north_output *output, int index);
struct north_workspace *workspace_active(struct north_output *output);
void workspace_resize_count(struct north_output *output, int new_count);
void workspace_migrate_views_to_output(struct north_output *src, struct north_output *dst);
bool workspace_view_is_visible(const struct north_view *view);

void workspace_manager_init(struct north_server *server);
void workspace_manager_finish(struct north_server *server);

#endif
