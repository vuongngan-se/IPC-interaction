#include "protocol.h"
#include "fifo_ipc.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

static volatile sig_atomic_t g_running = 1;
static int g_server_read_fd = -1;
static int g_dummy_write_fd = -1;

/* Simple client tracker for Phase 2 */
static client_info_t g_clients[MAX_CLIENTS];
static int g_client_count = 0;
static int g_next_id = 1;

static void handle_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

static void broadcast_notice(const char *text)
{
    chat_msg_t notice;
    memset(&notice, 0, sizeof(notice));
    notice.msg_type = MSG_SERVER_NOTICE;
    notice.sender_id = 0;
    strncpy(notice.sender_nick, "SERVER", sizeof(notice.sender_nick) - 1);
    strncpy(notice.content, text, sizeof(notice.content) - 1);
    notice.content_len = strlen(notice.content);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].is_online) {
            int cli_fd = fifo_open_write(g_clients[i].response_fifo, 1);
            if (cli_fd >= 0) {
                fifo_send_msg(cli_fd, &notice);
                close(cli_fd);
            }
        }
    }
}

static void handle_register(const chat_msg_t *req)
{
    LOG_INFO("Registration request received from PID %d (nick: '%s', fifo: %s)",
             (int)req->sender_pid, req->sender_nick, req->client_fifo);

    chat_msg_t resp;
    memset(&resp, 0, sizeof(resp));
    resp.msg_type = MSG_REGISTER_RESP;
    resp.target_id = req->sender_id;

    /* Check if full */
    if (g_client_count >= MAX_CLIENTS) {
        resp.status_code = STATUS_ERR_FULL;
        snprintf(resp.content, sizeof(resp.content), "Server is full (max %d clients)", MAX_CLIENTS);
        int cli_fd = fifo_open_write(req->client_fifo, 0);
        if (cli_fd >= 0) {
            fifo_send_msg(cli_fd, &resp);
            close(cli_fd);
        }
        LOG_WARN("Rejected client PID %d: Server full", (int)req->sender_pid);
        return;
    }

    /* Check duplicate nickname */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].is_online && strcmp(g_clients[i].nickname, req->sender_nick) == 0) {
            resp.status_code = STATUS_ERR_NICK_TAKEN;
            snprintf(resp.content, sizeof(resp.content), "Nickname '%s' already taken", req->sender_nick);
            int cli_fd = fifo_open_write(req->client_fifo, 0);
            if (cli_fd >= 0) {
                fifo_send_msg(cli_fd, &resp);
                close(cli_fd);
            }
            LOG_WARN("Rejected client PID %d: Nickname '%s' already taken",
                     (int)req->sender_pid, req->sender_nick);
            return;
        }
    }

    /* Find empty slot */
    int slot = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!g_clients[i].is_online) {
            slot = i;
            break;
        }
    }

    int assigned_id = g_next_id++;
    g_clients[slot].client_id = assigned_id;
    g_clients[slot].pid = req->sender_pid;
    strncpy(g_clients[slot].nickname, req->sender_nick, sizeof(g_clients[slot].nickname) - 1);
    g_clients[slot].is_online = 1;
    strncpy(g_clients[slot].response_fifo, req->client_fifo, sizeof(g_clients[slot].response_fifo) - 1);
    g_clients[slot].join_time = time(NULL);
    g_client_count++;

    resp.status_code = STATUS_SUCCESS;
    resp.sender_id = assigned_id;
    resp.target_id = assigned_id;
    snprintf(resp.content, sizeof(resp.content),
             "Welcome '%s' to IPC Chat! Assigned Client ID: #%d (Online: %d)",
             req->sender_nick, assigned_id, g_client_count);

    int cli_fd = fifo_open_write(req->client_fifo, 0);
    if (cli_fd >= 0) {
        fifo_send_msg(cli_fd, &resp);
        close(cli_fd);
        LOG_INFO("Client registered successfully: [ID: %d] [Nick: %s] [PID: %d]",
                 assigned_id, req->sender_nick, (int)req->sender_pid);

        /* Broadcast join notice */
        char join_buf[256];
        snprintf(join_buf, sizeof(join_buf), "*** User '%s' (ID #%d) joined the chat ***",
                 req->sender_nick, assigned_id);
        broadcast_notice(join_buf);
    } else {
        LOG_ERROR("Failed to open client response FIFO %s", req->client_fifo);
        g_clients[slot].is_online = 0;
        g_client_count--;
    }
}

static void handle_chat_all(const chat_msg_t *msg)
{
    LOG_INFO("Broadcast message from [%s (ID #%d)]: %s",
             msg->sender_nick, msg->sender_id, msg->content);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].is_online) {
            int cli_fd = fifo_open_write(g_clients[i].response_fifo, 1);
            if (cli_fd >= 0) {
                fifo_send_msg(cli_fd, msg);
                close(cli_fd);
            }
        }
    }
}

static void handle_disconnect(const chat_msg_t *msg)
{
    LOG_INFO("Disconnect request from client ID #%d (Nick: %s)",
             msg->sender_id, msg->sender_nick);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].is_online && g_clients[i].client_id == msg->sender_id) {
            g_clients[i].is_online = 0;
            g_client_count--;

            char leave_buf[256];
            snprintf(leave_buf, sizeof(leave_buf), "*** User '%s' (ID #%d) left the chat ***",
                     msg->sender_nick, msg->sender_id);
            broadcast_notice(leave_buf);
            break;
        }
    }
}

int main(void)
{
    logger_init("logs/server.log");
    LOG_INFO("=== Starting IPC Multi-Process Chat Server ===");

    /* Setup signal handling */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Ignore SIGPIPE when client disconnects while server is writing */
    signal(SIGPIPE, SIG_IGN);

    /* Create Server FIFO */
    if (fifo_create(SERVER_FIFO_PATH) < 0) {
        LOG_ERROR("Could not create server FIFO: %s", SERVER_FIFO_PATH);
        logger_close();
        return 1;
    }
    LOG_INFO("Created server FIFO: %s", SERVER_FIFO_PATH);

    /* Open Server FIFO with O_RDWR (non-blocking wait, self-contained writer) */
    g_server_read_fd = fifo_open_rdwr(SERVER_FIFO_PATH);
    if (g_server_read_fd < 0) {
        LOG_ERROR("Failed to open server FIFO for reading/writing");
        fifo_destroy(SERVER_FIFO_PATH);
        logger_close();
        return 1;
    }

    LOG_INFO("Server listening for client requests on FIFO. Press Ctrl+C to stop.");

    chat_msg_t msg;
    while (g_running) {
        int ret = fifo_recv_msg(g_server_read_fd, &msg);
        if (!g_running) {
            break;
        }
        if (ret > 0) {
            switch (msg.msg_type) {
                case MSG_REGISTER:
                    handle_register(&msg);
                    break;
                case MSG_CHAT_ALL:
                    handle_chat_all(&msg);
                    break;
                case MSG_DISCONNECT:
                    handle_disconnect(&msg);
                    break;
                default:
                    LOG_WARN("Unknown message type: %d", msg.msg_type);
                    break;
            }
        } else if (ret < 0 && errno != EINTR) {
            LOG_ERROR("Error reading from server FIFO");
            break;
        }
    }

    LOG_INFO("Shutting down Server and cleaning up resources...");

    /* Broadcast server shutdown notice to all active clients */
    broadcast_notice("*** SERVER SHUTDOWN: The chat server is stopping ***");

    if (g_dummy_write_fd >= 0) close(g_dummy_write_fd);
    if (g_server_read_fd >= 0) close(g_server_read_fd);
    fifo_destroy(SERVER_FIFO_PATH);

    LOG_INFO("Server terminated cleanly.");
    logger_close();
    return 0;
}
