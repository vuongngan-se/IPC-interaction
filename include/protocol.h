#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>

/* =========================================================================
 * IPC Multi-Process Chat Protocol Definitions
 * ========================================================================= */

#define MAX_CLIENTS             32
#define MAX_NICKNAME_LEN        32
#define MAX_MSG_LEN             512
#define FIFO_PATH_LEN           128

#define SERVER_FIFO_PATH        "/tmp/ipc_chat_srv.fifo"
#define CLIENT_FIFO_TEMPLATE    "/tmp/ipc_chat_cli_%d.fifo"
#define IPC_KEY_PATH            "/tmp/ipc_chat_srv.fifo"
#define PROJECT_ID              'C'

#define WORKER_COUNT            2

/* Message Types */
typedef enum {
    MSG_REGISTER        = 1,    /* Client requests registration */
    MSG_REGISTER_RESP   = 2,    /* Server replies with assigned ID */
    MSG_CHAT_ALL        = 3,    /* Public message to all clients */
    MSG_CHAT_PRIVATE    = 4,    /* Private message to specific client */
    MSG_LIST_USERS      = 5,    /* Request online user list */
    MSG_LIST_RESP       = 6,    /* Response with online user list */
    MSG_DISCONNECT      = 7,    /* Client requests clean disconnect */
    MSG_SERVER_NOTICE   = 8,    /* System announcement (join/leave/shutdown) */
    MSG_ERROR           = 9     /* Error notification */
} MessageType;

/* Status Codes */
typedef enum {
    STATUS_SUCCESS             =  0,
    STATUS_ERR_FULL            = -1,    /* Server capacity reached */
    STATUS_ERR_NICK_TAKEN      = -2,    /* Nickname is already in use */
    STATUS_ERR_INVALID_TARGET  = -3,    /* Target client not found */
    STATUS_ERR_NOT_REGISTERED  = -4,    /* Client is not registered */
    STATUS_ERR_INTERNAL        = -5     /* Internal server error */
} StatusCode;

/*
 * chat_msg_t: Fixed-size packet passed across FIFOs.
 * Size is intentionally kept well below PIPE_BUF (4096 bytes on Linux)
 * to guarantee atomic write and read across multiple concurrent processes.
 */
typedef struct {
    int msg_type;                       /* MessageType */
    int sender_id;                      /* Client ID assigned by server */
    pid_t sender_pid;                   /* Sender Process ID */
    char sender_nick[MAX_NICKNAME_LEN]; /* Nickname */
    int target_id;                      /* Target client ID (-1 if broadcast/server) */
    char target_nick[MAX_NICKNAME_LEN]; /* Target nickname (for private messages) */
    char client_fifo[FIFO_PATH_LEN];    /* Path to client's dedicated response FIFO */
    int status_code;                    /* StatusCode (for responses) */
    size_t content_len;                 /* Length of content */
    char content[MAX_MSG_LEN];          /* Message payload or payload text */
} chat_msg_t;

/*
 * worker_msg_t: Message struct for System V Message Queue (msgsnd / msgrcv).
 * First field MUST be `long mtype` > 0.
 */
typedef struct {
    long mtype;                         /* Priority / destination channel (e.g. 1) */
    chat_msg_t chat_msg;                /* Payload passed from Dispatcher to Worker */
} worker_msg_t;

/* Client record inside Shared Memory */
typedef struct {
    int client_id;
    pid_t pid;
    char nickname[MAX_NICKNAME_LEN];
    int is_online;
    char response_fifo[FIFO_PATH_LEN];
    time_t join_time;
} client_info_t;

/*
 * shared_state_t: Layout of the System V Shared Memory segment.
 * Protected by System V Semaphore (binary mutex).
 */
typedef struct {
    int server_status;                  /* 1 = running, 0 = shutting down */
    pid_t server_pid;                   /* PID of main server process */
    int active_client_count;            /* Current number of connected clients */
    int next_client_id;                 /* Incrementing client ID counter */
    client_info_t clients[MAX_CLIENTS]; /* Table of registered clients */
} shared_state_t;

#endif /* PROTOCOL_H */
