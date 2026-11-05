#define _GNU_SOURCE
#include "ipc/protocol.h"
#include "unity.h"

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-client.h>

static pid_t server_pid = -1;
static char ipc_socket_path[512] = {0};

void setUp(void)
{
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (!runtime_dir || access(runtime_dir, W_OK) != 0) {
        char fallback[256];
        snprintf(fallback, sizeof(fallback), "/tmp/north-runtime-%d", (int)getuid());
        mkdir(fallback, 0700);
        setenv("XDG_RUNTIME_DIR", fallback, 1);
    }
}

void tearDown(void)
{
    if (server_pid > 0) {
        kill(server_pid, SIGTERM);
        int status = 0;
        waitpid(server_pid, &status, 0);
        server_pid = -1;
    }
    if (ipc_socket_path[0]) {
        unlink(ipc_socket_path);
        ipc_socket_path[0] = '\0';
    }
}

static int connect_unix_socket(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    int attempts = 50;
    while (attempts-- > 0) {
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            return fd;
        }
        usleep(10000); // 10ms
    }

    close(fd);
    return -1;
}

static bool ipc_send(int fd, uint32_t type, const char *payload)
{
    size_t payload_len = payload ? strlen(payload) : 0;
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
            free(buf);
            return false;
        }
        sent += (size_t)ret;
    }
    free(buf);
    return true;
}

static bool ipc_recv(int fd, uint32_t *out_type, char **out_payload)
{
    struct north_ipc_header hdr;
    size_t recvd = 0;
    char *hdr_ptr = (char *)&hdr;
    while (recvd < NORTH_IPC_HEADER_SIZE) {
        ssize_t ret = read(fd, hdr_ptr + recvd, NORTH_IPC_HEADER_SIZE - recvd);
        if (ret <= 0) {
            return false;
        }
        recvd += (size_t)ret;
    }

    if (memcmp(hdr.magic, NORTH_IPC_MAGIC, 4) != 0) {
        return false;
    }

    char *payload = NULL;
    if (hdr.payload_len > 0) {
        payload = malloc(hdr.payload_len + 1);
        if (!payload) {
            return false;
        }
        recvd = 0;
        while (recvd < hdr.payload_len) {
            ssize_t ret = read(fd, payload + recvd, hdr.payload_len - recvd);
            if (ret <= 0) {
                free(payload);
                return false;
            }
            recvd += (size_t)ret;
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
    return true;
}

static void spawn_server(void)
{
    int pipe_fds[2];
    TEST_ASSERT_EQUAL_INT(0, pipe(pipe_fds));

    server_pid = fork();
    TEST_ASSERT_TRUE(server_pid >= 0);

    if (server_pid == 0) {
        close(pipe_fds[0]);
        setenv("WLR_BACKENDS", "headless", 1);
        setenv("WLR_RENDERER", "pixman", 1);
        setenv("WLR_LIBINPUT_NO_DEVICES", "1", 1);

        char ready_str[32];
        snprintf(ready_str, sizeof(ready_str), "%d", pipe_fds[1]);
        execl(NORTH_COMPOSITOR_BIN, "north-compositor", "-R", ready_str, (char *)NULL);
        _exit(1);
    }

    close(pipe_fds[1]);
    char wayland_sock[128] = {0};
    ssize_t r = read(pipe_fds[0], wayland_sock, sizeof(wayland_sock) - 1);
    close(pipe_fds[0]);
    TEST_ASSERT_TRUE(r > 0);

    char *nl = strchr(wayland_sock, '\n');
    if (nl) {
        *nl = '\0';
    }

    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    snprintf(ipc_socket_path, sizeof(ipc_socket_path), "%s/north-ipc-%s.sock", runtime_dir,
             wayland_sock);
}

void test_ipc_queries_and_commands(void)
{
    spawn_server();

    int fd = connect_unix_socket(ipc_socket_path);
    TEST_ASSERT_TRUE(fd >= 0);

    // 1. Version query
    TEST_ASSERT_TRUE(ipc_send(fd, NORTH_IPC_MSG_GET_VERSION, NULL));
    uint32_t type = 0;
    char *payload = NULL;
    TEST_ASSERT_TRUE(ipc_recv(fd, &type, &payload));
    TEST_ASSERT_EQUAL_UINT32(NORTH_IPC_REPLY_SUCCESS, type);
    TEST_ASSERT_NOT_NULL(payload);
    TEST_ASSERT_NOT_NULL(strstr(payload, "north-compositor"));
    free(payload);
    payload = NULL;

    // 2. Workspaces query
    TEST_ASSERT_TRUE(ipc_send(fd, NORTH_IPC_MSG_GET_WORKSPACES, NULL));
    TEST_ASSERT_TRUE(ipc_recv(fd, &type, &payload));
    TEST_ASSERT_EQUAL_UINT32(NORTH_IPC_REPLY_SUCCESS, type);
    TEST_ASSERT_NOT_NULL(payload);
    TEST_ASSERT_NOT_NULL(strstr(payload, "index"));
    free(payload);
    payload = NULL;

    // 3. Views query
    TEST_ASSERT_TRUE(ipc_send(fd, NORTH_IPC_MSG_GET_VIEWS, NULL));
    TEST_ASSERT_TRUE(ipc_recv(fd, &type, &payload));
    TEST_ASSERT_EQUAL_UINT32(NORTH_IPC_REPLY_SUCCESS, type);
    TEST_ASSERT_NOT_NULL(payload);
    TEST_ASSERT_EQUAL_STRING("[]", payload);
    free(payload);
    payload = NULL;

    // 4. Command execution
    TEST_ASSERT_TRUE(ipc_send(fd, NORTH_IPC_MSG_COMMAND, "workspace 2"));
    TEST_ASSERT_TRUE(ipc_recv(fd, &type, &payload));
    TEST_ASSERT_EQUAL_UINT32(NORTH_IPC_REPLY_SUCCESS, type);
    free(payload);
    payload = NULL;

    // 5. Unknown command returns error
    TEST_ASSERT_TRUE(ipc_send(fd, NORTH_IPC_MSG_COMMAND, "non_existent_command_xyz"));
    TEST_ASSERT_TRUE(ipc_recv(fd, &type, &payload));
    TEST_ASSERT_EQUAL_UINT32(NORTH_IPC_REPLY_ERROR, type);
    TEST_ASSERT_NOT_NULL(strstr(payload, "unknown command"));
    free(payload);
    payload = NULL;

    close(fd);
}

void test_ipc_event_subscription(void)
{
    spawn_server();

    // Client 1: subscriber
    int sub_fd = connect_unix_socket(ipc_socket_path);
    TEST_ASSERT_TRUE(sub_fd >= 0);

    TEST_ASSERT_TRUE(ipc_send(sub_fd, NORTH_IPC_MSG_SUBSCRIBE, "[\"workspace\"]"));
    uint32_t type = 0;
    char *payload = NULL;
    TEST_ASSERT_TRUE(ipc_recv(sub_fd, &type, &payload));
    TEST_ASSERT_EQUAL_UINT32(NORTH_IPC_REPLY_SUCCESS, type);
    free(payload);
    payload = NULL;

    // Client 2: command sender
    int cmd_fd = connect_unix_socket(ipc_socket_path);
    TEST_ASSERT_TRUE(cmd_fd >= 0);

    TEST_ASSERT_TRUE(ipc_send(cmd_fd, NORTH_IPC_MSG_COMMAND, "workspace 3"));
    TEST_ASSERT_TRUE(ipc_recv(cmd_fd, &type, &payload));
    TEST_ASSERT_EQUAL_UINT32(NORTH_IPC_REPLY_SUCCESS, type);
    free(payload);
    payload = NULL;

    // Client 1 should receive broadcasted workspace event
    TEST_ASSERT_TRUE(ipc_recv(sub_fd, &type, &payload));
    TEST_ASSERT_EQUAL_UINT32(NORTH_IPC_EVENT_WORKSPACE, type);
    TEST_ASSERT_NOT_NULL(payload);
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"change\":\"focus\""));
    free(payload);
    payload = NULL;

    close(cmd_fd);
    close(sub_fd);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ipc_queries_and_commands);
    RUN_TEST(test_ipc_event_subscription);
    return UNITY_END();
}
