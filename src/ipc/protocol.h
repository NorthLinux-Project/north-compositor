#ifndef NORTH_IPC_PROTOCOL_H
#define NORTH_IPC_PROTOCOL_H

#include <stdint.h>

#define NORTH_IPC_MAGIC "NIPC"
#define NORTH_IPC_HEADER_SIZE 12
#define NORTH_IPC_MAX_PAYLOAD (1024 * 1024)

enum north_ipc_msg_type {
    NORTH_IPC_MSG_COMMAND = 0,
    NORTH_IPC_MSG_GET_WORKSPACES = 1,
    NORTH_IPC_MSG_GET_VIEWS = 2,
    NORTH_IPC_MSG_GET_OUTPUTS = 3,
    NORTH_IPC_MSG_GET_VERSION = 4,
    NORTH_IPC_MSG_SUBSCRIBE = 5,
};

enum north_ipc_reply_type {
    NORTH_IPC_REPLY_SUCCESS = 100,
    NORTH_IPC_REPLY_ERROR = 101,
};

enum north_ipc_event_type {
    NORTH_IPC_EVENT_WORKSPACE = 200,
    NORTH_IPC_EVENT_WINDOW = 201,
    NORTH_IPC_EVENT_OUTPUT = 202,
    NORTH_IPC_EVENT_CONFIG = 203,
};

enum north_ipc_subscription {
    NORTH_IPC_SUB_WORKSPACE = 1 << 0,
    NORTH_IPC_SUB_WINDOW = 1 << 1,
    NORTH_IPC_SUB_OUTPUT = 1 << 2,
    NORTH_IPC_SUB_CONFIG = 1 << 3,
};

struct north_ipc_header {
    char magic[4];
    uint32_t type;
    uint32_t payload_len;
} __attribute__((packed));

#endif
