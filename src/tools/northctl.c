#define _GNU_SOURCE
#include "ipc/protocol.h"

#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static void print_usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [options] <command> [args...]\n\n"
            "Commands:\n"
            "  get <workspaces|views|outputs|version>   Query compositor state (returns JSON)\n"
            "  msg <command>                            Send command (e.g. \"workspace 2\", "
            "\"focus next\", \"close\")\n"
            "  subscribe [workspace|window|output|config|all...] Stream event JSON notifications\n"
            "  reload                                   Trigger configuration reload\n\n"
            "Options:\n"
            "  -s, --socket <path>   Explicit IPC socket path\n"
            "  -h, --help            Show this help message\n",
            prog);
}

static char *resolve_socket_path(const char *override_path)
{
    if (override_path) {
        return strdup(override_path);
    }

    const char *env_sock = getenv("NORTH_IPC_SOCK");
    if (env_sock && access(env_sock, R_OK | W_OK) == 0) {
        return strdup(env_sock);
    }

    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    const char *wayland_display = getenv("WAYLAND_DISPLAY");
    if (!wayland_display) {
        wayland_display = "wayland-0";
    }

    char buf[512];
    if (runtime_dir) {
        snprintf(buf, sizeof(buf), "%s/north-ipc-%s.sock", runtime_dir, wayland_display);
        if (access(buf, R_OK | W_OK) == 0) {
            return strdup(buf);
        }
    }

    snprintf(buf, sizeof(buf), "/tmp/north-runtime-%d/north-ipc-%s.sock", (int)getuid(),
             wayland_display);
    return strdup(buf);
}

static int connect_ipc(const char *sock_path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "Error: failed to create Unix socket: %s\n", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "Error: cannot connect to north-compositor IPC at '%s': %s\n", sock_path,
                strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

static bool send_ipc_msg(int fd, uint32_t type, const char *payload, size_t payload_len)
{
    struct north_ipc_header hdr;
    memcpy(hdr.magic, NORTH_IPC_MAGIC, 4);
    hdr.type = type;
    hdr.payload_len = (uint32_t)payload_len;

    size_t total = NORTH_IPC_HEADER_SIZE + payload_len;
    char *buf = malloc(total);
    if (!buf) {
        return false;
    }

    memcpy(buf, &hdr, NORTH_IPC_HEADER_SIZE);
    if (payload && payload_len > 0) {
        memcpy(buf + NORTH_IPC_HEADER_SIZE, payload, payload_len);
    }

    size_t sent = 0;
    while (sent < total) {
        ssize_t ret = write(fd, buf + sent, total - sent);
        if (ret <= 0) {
            if (ret < 0 && errno == EINTR) {
                continue;
            }
            free(buf);
            return false;
        }
        sent += (size_t)ret;
    }

    free(buf);
    return true;
}

static bool read_exact(int fd, void *buf, size_t count)
{
    size_t received = 0;
    char *p = buf;
    while (received < count) {
        ssize_t ret = read(fd, p + received, count - received);
        if (ret <= 0) {
            if (ret < 0 && errno == EINTR) {
                continue;
            }
            return false;
        }
        received += (size_t)ret;
    }
    return true;
}

static bool read_ipc_response(int fd, uint32_t *out_type, char **out_payload, size_t *out_len)
{
    struct north_ipc_header hdr;
    if (!read_exact(fd, &hdr, NORTH_IPC_HEADER_SIZE)) {
        return false;
    }

    if (memcmp(hdr.magic, NORTH_IPC_MAGIC, 4) != 0) {
        fprintf(stderr, "Error: received invalid magic from IPC server\n");
        return false;
    }

    char *payload = NULL;
    if (hdr.payload_len > 0) {
        payload = malloc(hdr.payload_len + 1);
        if (!payload) {
            return false;
        }
        if (!read_exact(fd, payload, hdr.payload_len)) {
            free(payload);
            return false;
        }
        payload[hdr.payload_len] = '\0';
    }

    if (out_type) {
        *out_type = hdr.type;
    }
    if (out_payload) {
        *out_payload = payload;
    } else {
        free(payload);
    }
    if (out_len) {
        *out_len = hdr.payload_len;
    }
    return true;
}

int main(int argc, char **argv)
{
    const char *socket_override = NULL;

    static struct option long_opts[] = {
        {"socket", required_argument, NULL, 's'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "+s:h", long_opts, NULL)) != -1) {
        switch (opt) {
        case 's':
            socket_override = optarg;
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    if (optind >= argc) {
        print_usage(argv[0]);
        return 1;
    }

    const char *subcmd = argv[optind++];

    char *sock_path = resolve_socket_path(socket_override);
    int fd = connect_ipc(sock_path);
    free(sock_path);
    if (fd < 0) {
        return 1;
    }

    if (strcmp(subcmd, "get") == 0) {
        if (optind >= argc) {
            fprintf(stderr,
                    "Error: 'get' requires a target (workspaces, views, outputs, version)\n");
            close(fd);
            return 1;
        }
        const char *target = argv[optind++];
        uint32_t type = NORTH_IPC_MSG_GET_VERSION;
        if (strcmp(target, "workspaces") == 0) {
            type = NORTH_IPC_MSG_GET_WORKSPACES;
        } else if (strcmp(target, "views") == 0 || strcmp(target, "windows") == 0 ||
                   strcmp(target, "tree") == 0) {
            type = NORTH_IPC_MSG_GET_VIEWS;
        } else if (strcmp(target, "outputs") == 0) {
            type = NORTH_IPC_MSG_GET_OUTPUTS;
        } else if (strcmp(target, "version") == 0) {
            type = NORTH_IPC_MSG_GET_VERSION;
        } else {
            fprintf(stderr, "Error: unknown get target '%s'\n", target);
            close(fd);
            return 1;
        }

        if (!send_ipc_msg(fd, type, NULL, 0)) {
            fprintf(stderr, "Error: failed to send query\n");
            close(fd);
            return 1;
        }

        uint32_t resp_type;
        char *resp = NULL;
        if (!read_ipc_response(fd, &resp_type, &resp, NULL)) {
            fprintf(stderr, "Error: failed reading server reply\n");
            close(fd);
            return 1;
        }

        if (resp) {
            printf("%s\n", resp);
            free(resp);
        }
        close(fd);
        return (resp_type == NORTH_IPC_REPLY_SUCCESS) ? 0 : 1;
    }

    if (strcmp(subcmd, "msg") == 0) {
        if (optind >= argc) {
            fprintf(stderr, "Error: 'msg' requires a command string\n");
            close(fd);
            return 1;
        }

        char cmd_buf[1024] = {0};
        size_t off = 0;
        for (int i = optind; i < argc; i++) {
            size_t arg_len = strlen(argv[i]);
            if (off + arg_len + 2 < sizeof(cmd_buf)) {
                if (off > 0) {
                    cmd_buf[off++] = ' ';
                }
                memcpy(cmd_buf + off, argv[i], arg_len);
                off += arg_len;
                cmd_buf[off] = '\0';
            }
        }

        if (!send_ipc_msg(fd, NORTH_IPC_MSG_COMMAND, cmd_buf, off)) {
            fprintf(stderr, "Error: failed sending command\n");
            close(fd);
            return 1;
        }

        uint32_t resp_type;
        char *resp = NULL;
        if (!read_ipc_response(fd, &resp_type, &resp, NULL)) {
            fprintf(stderr, "Error: failed reading server reply\n");
            close(fd);
            return 1;
        }

        if (resp) {
            if (resp_type == NORTH_IPC_REPLY_ERROR) {
                fprintf(stderr, "%s\n", resp);
            } else {
                printf("%s\n", resp);
            }
            free(resp);
        }
        close(fd);
        return (resp_type == NORTH_IPC_REPLY_SUCCESS) ? 0 : 1;
    }

    if (strcmp(subcmd, "reload") == 0) {
        if (!send_ipc_msg(fd, NORTH_IPC_MSG_COMMAND, "reload", 6)) {
            fprintf(stderr, "Error: failed sending reload command\n");
            close(fd);
            return 1;
        }
        uint32_t resp_type;
        char *resp = NULL;
        if (!read_ipc_response(fd, &resp_type, &resp, NULL)) {
            fprintf(stderr, "Error: failed reading server reply\n");
            close(fd);
            return 1;
        }
        if (resp) {
            printf("%s\n", resp);
            free(resp);
        }
        close(fd);
        return (resp_type == NORTH_IPC_REPLY_SUCCESS) ? 0 : 1;
    }

    if (strcmp(subcmd, "subscribe") == 0) {
        char json_sub[512] = "[";
        size_t off = 1;
        if (optind < argc) {
            for (int i = optind; i < argc; i++) {
                if (i > optind) {
                    json_sub[off++] = ',';
                }
                off += snprintf(json_sub + off, sizeof(json_sub) - off, "\"%s\"", argv[i]);
            }
        } else {
            off += snprintf(json_sub + off, sizeof(json_sub) - off,
                            "\"workspace\",\"window\",\"output\",\"config\"");
        }
        json_sub[off++] = ']';
        json_sub[off] = '\0';

        if (!send_ipc_msg(fd, NORTH_IPC_MSG_SUBSCRIBE, json_sub, off)) {
            fprintf(stderr, "Error: failed sending subscribe request\n");
            close(fd);
            return 1;
        }

        uint32_t ack_type;
        char *ack = NULL;
        if (!read_ipc_response(fd, &ack_type, &ack, NULL) || ack_type != NORTH_IPC_REPLY_SUCCESS) {
            fprintf(stderr, "Error: subscription rejected by server: %s\n", ack ? ack : "");
            free(ack);
            close(fd);
            return 1;
        }
        free(ack);

        while (1) {
            uint32_t evt_type;
            char *evt = NULL;
            if (!read_ipc_response(fd, &evt_type, &evt, NULL)) {
                break;
            }
            if (evt) {
                printf("%s\n", evt);
                fflush(stdout);
                free(evt);
            }
        }

        close(fd);
        return 0;
    }

    fprintf(stderr, "Error: unknown subcommand '%s'\n", subcmd);
    print_usage(argv[0]);
    close(fd);
    return 1;
}
