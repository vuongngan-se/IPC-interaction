#include "worker.h"
#include "protocol.h"
#include "fifo_ipc.h"
#include "msg_queue.h"
#include "semaphore.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

static volatile sig_atomic_t g_worker_running = 1;

static void handle_worker_signal(int sig)
{
    (void)sig;
    g_worker_running = 0;
}

static void worker_broadcast(const chat_msg_t *msg, shared_state_t *shm, int semid)
{
    if (shm == NULL) return;

    if (semid >= 0) sem_lock(semid);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (shm->clients[i].is_online) {
            /* If room filter is specified, only deliver to clients in that room */
            if (strlen(msg->room) > 0 && strcmp(shm->clients[i].current_room, msg->room) != 0) {
                continue;
            }

            /* Check if client process is still alive on OS level */
            if (kill(shm->clients[i].pid, 0) < 0 && errno == ESRCH) {
                LOG_WARN("[Worker] Detected terminated client PID %d (ID #%d, '%s'). Removing.",
                         (int)shm->clients[i].pid, shm->clients[i].client_id, shm->clients[i].nickname);
                shm->clients[i].is_online = 0;
                shm->active_client_count--;
                continue;
            }

            int fd = fifo_open_write(shm->clients[i].response_fifo, 1);
            if (fd >= 0) {
                int send_res = fifo_send_msg(fd, msg);
                close(fd);
                if (send_res < 0) {
                    LOG_WARN("[Worker] Broken pipe to client ID #%d. Marking offline.",
                             shm->clients[i].client_id);
                    shm->clients[i].is_online = 0;
                    shm->active_client_count--;
                }
            }
        }
    }

    if (semid >= 0) sem_unlock(semid);
}

static void worker_handle_register(int worker_id, const chat_msg_t *req, shared_state_t *shm, int semid)
{
    LOG_INFO("[Worker %d] Processing registration for '%s' (PID %d)",
             worker_id, req->sender_nick, (int)req->sender_pid);

    chat_msg_t resp;
    memset(&resp, 0, sizeof(resp));
    resp.msg_type = MSG_REGISTER_RESP;

    if (shm == NULL) {
        resp.status_code = STATUS_ERR_INTERNAL;
        snprintf(resp.content, sizeof(resp.content), "Internal server error: Shared memory not available");
        int fd = fifo_open_write(req->client_fifo, 0);
        if (fd >= 0) {
            fifo_send_msg(fd, &resp);
            close(fd);
        }
        return;
    }

    if (semid >= 0) sem_lock(semid);

    /* Check capacity */
    if (shm->active_client_count >= MAX_CLIENTS) {
        if (semid >= 0) sem_unlock(semid);
        resp.status_code = STATUS_ERR_FULL;
        snprintf(resp.content, sizeof(resp.content), "Server is full (max %d clients)", MAX_CLIENTS);
        int fd = fifo_open_write(req->client_fifo, 0);
        if (fd >= 0) {
            fifo_send_msg(fd, &resp);
            close(fd);
        }
        LOG_WARN("[Worker %d] Registration rejected: Server full", worker_id);
        return;
    }

    /* Check duplicate nickname */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (shm->clients[i].is_online && strcmp(shm->clients[i].nickname, req->sender_nick) == 0) {
            if (semid >= 0) sem_unlock(semid);
            resp.status_code = STATUS_ERR_NICK_TAKEN;
            snprintf(resp.content, sizeof(resp.content), "Nickname '%s' already taken", req->sender_nick);
            int fd = fifo_open_write(req->client_fifo, 0);
            if (fd >= 0) {
                fifo_send_msg(fd, &resp);
                close(fd);
            }
            LOG_WARN("[Worker %d] Registration rejected: Nick '%s' taken", worker_id, req->sender_nick);
            return;
        }
    }

    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!shm->clients[i].is_online) {
            slot = i;
            break;
        }
    }

    int assigned_id = ++(shm->next_client_id);
    shm->clients[slot].client_id = assigned_id;
    shm->clients[slot].pid = req->sender_pid;
    strncpy(shm->clients[slot].nickname, req->sender_nick, sizeof(shm->clients[slot].nickname) - 1);
    strncpy(shm->clients[slot].current_room, DEFAULT_ROOM, sizeof(shm->clients[slot].current_room) - 1);
    shm->clients[slot].is_online = 1;
    strncpy(shm->clients[slot].response_fifo, req->client_fifo, sizeof(shm->clients[slot].response_fifo) - 1);
    shm->clients[slot].join_time = time(NULL);
    shm->active_client_count++;

    int current_count = shm->active_client_count;
    if (semid >= 0) sem_unlock(semid);

    /* Send success response to client */
    resp.status_code = STATUS_SUCCESS;
    resp.sender_id = assigned_id;
    resp.target_id = assigned_id;
    snprintf(resp.content, sizeof(resp.content),
             "Welcome '%s' to IPC Chat! Assigned Client ID: #%d (Online: %d)",
             req->sender_nick, assigned_id, current_count);

    int fd = fifo_open_write(req->client_fifo, 0);
    if (fd >= 0) {
        fifo_send_msg(fd, &resp);
        close(fd);
        LOG_INFO("[Worker %d] Client registered: [ID #%d] [Nick: %s] [PID: %d]",
                 worker_id, assigned_id, req->sender_nick, (int)req->sender_pid);

        /* Broadcast announcement */
        chat_msg_t notice;
        memset(&notice, 0, sizeof(notice));
        notice.msg_type = MSG_SERVER_NOTICE;
        notice.sender_id = 0;
        strncpy(notice.sender_nick, "SERVER", sizeof(notice.sender_nick) - 1);
        snprintf(notice.content, sizeof(notice.content),
                 "*** User '%s' (ID #%d) joined the chat ***", req->sender_nick, assigned_id);
        notice.content_len = strlen(notice.content);
        worker_broadcast(&notice, shm, semid);
    } else {
        LOG_ERROR("[Worker %d] Failed to open response FIFO %s", worker_id, req->client_fifo);
        if (semid >= 0) sem_lock(semid);
        shm->clients[slot].is_online = 0;
        shm->active_client_count--;
        if (semid >= 0) sem_unlock(semid);
    }
}

static void worker_handle_chat_all(int worker_id, const chat_msg_t *msg, shared_state_t *shm, int semid)
{
    LOG_INFO("[Worker %d] Broadcast from [%s (#%d)]: %s",
             worker_id, msg->sender_nick, msg->sender_id, msg->content);
    worker_broadcast(msg, shm, semid);
}

static void worker_handle_chat_private(int worker_id, const chat_msg_t *msg, shared_state_t *shm, int semid)
{
    LOG_INFO("[Worker %d] Direct/file msg (type %d) from [%s (#%d)] to [target_nick: '%s' / id: %d]",
             worker_id, msg->msg_type, msg->sender_nick, msg->sender_id, msg->target_nick, msg->target_id);

    int found = 0;
    char target_fifo[FIFO_PATH_LEN] = {0};
    char sender_fifo[FIFO_PATH_LEN] = {0};

    if (semid >= 0) sem_lock(semid);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (shm->clients[i].is_online) {
            if (shm->clients[i].client_id == msg->sender_id) {
                strncpy(sender_fifo, shm->clients[i].response_fifo, sizeof(sender_fifo) - 1);
            }
            if ((msg->target_id > 0 && shm->clients[i].client_id == msg->target_id) ||
                (strlen(msg->target_nick) > 0 && strcmp(shm->clients[i].nickname, msg->target_nick) == 0)) {
                strncpy(target_fifo, shm->clients[i].response_fifo, sizeof(target_fifo) - 1);
                found = 1;
            }
        }
    }
    if (semid >= 0) sem_unlock(semid);

    if (found) {
        int fd = fifo_open_write(target_fifo, 1);
        if (fd >= 0) {
            fifo_send_msg(fd, msg);
            close(fd);
        }
    } else {
        /* Send error notice back to sender */
        if (strlen(sender_fifo) > 0) {
            chat_msg_t err_msg;
            memset(&err_msg, 0, sizeof(err_msg));
            err_msg.msg_type = MSG_ERROR;
            snprintf(err_msg.content, sizeof(err_msg.content),
                     "Target user not found or offline.");
            err_msg.content_len = strlen(err_msg.content);
            int fd = fifo_open_write(sender_fifo, 1);
            if (fd >= 0) {
                fifo_send_msg(fd, &err_msg);
                close(fd);
            }
        }
    }
}

static void worker_handle_list(int worker_id, const chat_msg_t *msg, shared_state_t *shm, int semid)
{
    LOG_INFO("[Worker %d] List users requested by [%s (#%d)]",
             worker_id, msg->sender_nick, msg->sender_id);

    chat_msg_t resp;
    memset(&resp, 0, sizeof(resp));
    resp.msg_type = MSG_LIST_RESP;

    char sender_fifo[FIFO_PATH_LEN] = {0};
    int offset = 0;
    offset += snprintf(resp.content + offset, sizeof(resp.content) - offset,
                       "=== Online Users ===\n");

    if (semid >= 0) sem_lock(semid);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (shm->clients[i].is_online) {
            if (kill(shm->clients[i].pid, 0) < 0 && errno == ESRCH) {
                shm->clients[i].is_online = 0;
                shm->active_client_count--;
                continue;
            }
            if (shm->clients[i].client_id == msg->sender_id) {
                strncpy(sender_fifo, shm->clients[i].response_fifo, sizeof(sender_fifo) - 1);
            }
            offset += snprintf(resp.content + offset, sizeof(resp.content) - offset,
                               "  * ID #%d: %s (PID: %d)\n",
                               shm->clients[i].client_id,
                               shm->clients[i].nickname,
                               (int)shm->clients[i].pid);
            if (offset >= (int)sizeof(resp.content) - 40) break;
        }
    }
    if (semid >= 0) sem_unlock(semid);

    if (strlen(sender_fifo) > 0) {
        resp.content_len = strlen(resp.content);
        int fd = fifo_open_write(sender_fifo, 1);
        if (fd >= 0) {
            fifo_send_msg(fd, &resp);
            close(fd);
        }
    }
}

static void worker_handle_disconnect(int worker_id, const chat_msg_t *msg, shared_state_t *shm, int semid)
{
    LOG_INFO("[Worker %d] Disconnect request from [%s (#%d)]",
             worker_id, msg->sender_nick, msg->sender_id);

    char nick[MAX_NICKNAME_LEN] = {0};
    int cid = msg->sender_id;

    if (semid >= 0) sem_lock(semid);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (shm->clients[i].is_online && shm->clients[i].client_id == cid) {
            shm->clients[i].is_online = 0;
            shm->active_client_count--;
            strncpy(nick, shm->clients[i].nickname, sizeof(nick) - 1);
            break;
        }
    }
    if (semid >= 0) sem_unlock(semid);

    /* Broadcast leave notice */
    chat_msg_t notice;
    memset(&notice, 0, sizeof(notice));
    notice.msg_type = MSG_SERVER_NOTICE;
    notice.sender_id = 0;
    strncpy(notice.sender_nick, "SERVER", sizeof(notice.sender_nick) - 1);
    snprintf(notice.content, sizeof(notice.content),
             "*** User '%s' (ID #%d) left the chat ***", nick, cid);
    notice.content_len = strlen(notice.content);
    worker_broadcast(&notice, shm, semid);
}

static void worker_handle_room_join(int worker_id, const chat_msg_t *msg, shared_state_t *shm, int semid)
{
    LOG_INFO("[Worker %d] Client [%s (#%d)] joining room '%s'",
             worker_id, msg->sender_nick, msg->sender_id, msg->room);

    char sender_fifo[FIFO_PATH_LEN] = {0};
    char old_room[MAX_ROOM_NAME_LEN] = {0};
    int found = 0;

    if (semid >= 0) sem_lock(semid);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (shm->clients[i].is_online && shm->clients[i].client_id == msg->sender_id) {
            strncpy(sender_fifo, shm->clients[i].response_fifo, sizeof(sender_fifo) - 1);
            strncpy(old_room, shm->clients[i].current_room, sizeof(old_room) - 1);
            strncpy(shm->clients[i].current_room, msg->room, sizeof(shm->clients[i].current_room) - 1);
            found = 1;
            break;
        }
    }
    if (semid >= 0) sem_unlock(semid);

    if (found && strlen(sender_fifo) > 0) {
        /* Send response to client */
        chat_msg_t resp;
        memset(&resp, 0, sizeof(resp));
        resp.msg_type = MSG_ROOM_RESP;
        resp.status_code = STATUS_SUCCESS;
        strncpy(resp.room, msg->room, sizeof(resp.room) - 1);
        snprintf(resp.content, sizeof(resp.content),
                 "Switched to room '%s' (Previous: %s)", msg->room, old_room);
        resp.content_len = strlen(resp.content);

        int fd = fifo_open_write(sender_fifo, 1);
        if (fd >= 0) {
            fifo_send_msg(fd, &resp);
            close(fd);
        }

        /* Broadcast announcement to users in the new room */
        chat_msg_t notice;
        memset(&notice, 0, sizeof(notice));
        notice.msg_type = MSG_SERVER_NOTICE;
        strncpy(notice.room, msg->room, sizeof(notice.room) - 1);
        snprintf(notice.content, sizeof(notice.content),
                 "*** User '%s' joined room %s ***", msg->sender_nick, msg->room);
        notice.content_len = strlen(notice.content);
        worker_broadcast(&notice, shm, semid);
    }
}

static void worker_handle_room_leave(int worker_id, const chat_msg_t *msg, shared_state_t *shm, int semid)
{
    LOG_INFO("[Worker %d] Client [%s (#%d)] leaving room, returning to %s",
             worker_id, msg->sender_nick, msg->sender_id, DEFAULT_ROOM);

    char sender_fifo[FIFO_PATH_LEN] = {0};
    char old_room[MAX_ROOM_NAME_LEN] = {0};

    if (semid >= 0) sem_lock(semid);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (shm->clients[i].is_online && shm->clients[i].client_id == msg->sender_id) {
            strncpy(sender_fifo, shm->clients[i].response_fifo, sizeof(sender_fifo) - 1);
            strncpy(old_room, shm->clients[i].current_room, sizeof(old_room) - 1);
            strncpy(shm->clients[i].current_room, DEFAULT_ROOM, sizeof(shm->clients[i].current_room) - 1);
            break;
        }
    }
    if (semid >= 0) sem_unlock(semid);

    if (strlen(sender_fifo) > 0) {
        chat_msg_t resp;
        memset(&resp, 0, sizeof(resp));
        resp.msg_type = MSG_ROOM_RESP;
        resp.status_code = STATUS_SUCCESS;
        strncpy(resp.room, DEFAULT_ROOM, sizeof(resp.room) - 1);
        snprintf(resp.content, sizeof(resp.content),
                 "Left room '%s'. Returned to '%s'", old_room, DEFAULT_ROOM);
        resp.content_len = strlen(resp.content);

        int fd = fifo_open_write(sender_fifo, 1);
        if (fd >= 0) {
            fifo_send_msg(fd, &resp);
            close(fd);
        }

        /* Broadcast leave announcement to old room */
        if (strcmp(old_room, DEFAULT_ROOM) != 0) {
            chat_msg_t notice;
            memset(&notice, 0, sizeof(notice));
            notice.msg_type = MSG_SERVER_NOTICE;
            strncpy(notice.room, old_room, sizeof(notice.room) - 1);
            snprintf(notice.content, sizeof(notice.content),
                     "*** User '%s' left room %s ***", msg->sender_nick, old_room);
            notice.content_len = strlen(notice.content);
            worker_broadcast(&notice, shm, semid);
        }
    }
}

static void worker_handle_room_list(int worker_id, const chat_msg_t *msg, shared_state_t *shm, int semid)
{
    LOG_INFO("[Worker %d] Rooms list requested by [%s (#%d)]",
             worker_id, msg->sender_nick, msg->sender_id);

    chat_msg_t resp;
    memset(&resp, 0, sizeof(resp));
    resp.msg_type = MSG_ROOM_RESP;

    char sender_fifo[FIFO_PATH_LEN] = {0};
    int offset = 0;
    offset += snprintf(resp.content + offset, sizeof(resp.content) - offset,
                       "=== Active Channels & Rooms ===\n");

    /* Structure to collect distinct rooms */
    typedef struct {
        char name[MAX_ROOM_NAME_LEN];
        int count;
    } room_entry_t;
    room_entry_t rooms[MAX_CLIENTS];
    int room_count = 0;

    if (semid >= 0) sem_lock(semid);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (shm->clients[i].is_online) {
            if (shm->clients[i].client_id == msg->sender_id) {
                strncpy(sender_fifo, shm->clients[i].response_fifo, sizeof(sender_fifo) - 1);
            }
            const char *rname = strlen(shm->clients[i].current_room) > 0 ? shm->clients[i].current_room : DEFAULT_ROOM;
            int found_room = 0;
            for (int r = 0; r < room_count; r++) {
                if (strcmp(rooms[r].name, rname) == 0) {
                    rooms[r].count++;
                    found_room = 1;
                    break;
                }
            }
            if (!found_room && room_count < MAX_CLIENTS) {
                strncpy(rooms[room_count].name, rname, sizeof(rooms[room_count].name) - 1);
                rooms[room_count].count = 1;
                room_count++;
            }
        }
    }
    if (semid >= 0) sem_unlock(semid);

    for (int r = 0; r < room_count; r++) {
        offset += snprintf(resp.content + offset, sizeof(resp.content) - offset,
                           "  * %-16s (%d %s online)\n",
                           rooms[r].name,
                           rooms[r].count,
                           rooms[r].count == 1 ? "user" : "users");
        if (offset >= (int)sizeof(resp.content) - 40) break;
    }

    if (strlen(sender_fifo) > 0) {
        resp.content_len = strlen(resp.content);
        int fd = fifo_open_write(sender_fifo, 1);
        if (fd >= 0) {
            fifo_send_msg(fd, &resp);
            close(fd);
        }
    }
}

void worker_run(int worker_id, int msqid, int semid, shared_state_t *shm_state)
{
    /* Setup signal handling for Worker process */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_worker_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    LOG_INFO("Worker %d started (PID: %d). Listening on Message Queue (msqid: %d)...",
             worker_id, (int)getpid(), msqid);

    worker_msg_t wmsg;
    while (g_worker_running) {
        int ret = msg_queue_recv(msqid, &wmsg, 1, 0);
        if (!g_worker_running) break;

        if (ret > 0) {
            chat_msg_t *req = &wmsg.chat_msg;
            switch (req->msg_type) {
                case MSG_REGISTER:
                    worker_handle_register(worker_id, req, shm_state, semid);
                    break;
                case MSG_CHAT_ALL:
                    worker_handle_chat_all(worker_id, req, shm_state, semid);
                    break;
                case MSG_CHAT_PRIVATE:
                case MSG_FILE_OFFER:
                case MSG_FILE_ACCEPT:
                case MSG_FILE_REJECT:
                    worker_handle_chat_private(worker_id, req, shm_state, semid);
                    break;
                case MSG_LIST_USERS:
                    worker_handle_list(worker_id, req, shm_state, semid);
                    break;
                case MSG_DISCONNECT:
                    worker_handle_disconnect(worker_id, req, shm_state, semid);
                    break;
                case MSG_ROOM_JOIN:
                    worker_handle_room_join(worker_id, req, shm_state, semid);
                    break;
                case MSG_ROOM_LEAVE:
                    worker_handle_room_leave(worker_id, req, shm_state, semid);
                    break;
                case MSG_ROOM_LIST:
                    worker_handle_room_list(worker_id, req, shm_state, semid);
                    break;
                default:
                    LOG_WARN("[Worker %d] Unknown message type: %d", worker_id, req->msg_type);
                    break;
            }
        } else if (ret < 0 && errno != EINTR) {
            /* Queue destroyed or error */
            break;
        }
    }

    LOG_INFO("Worker %d (PID: %d) exiting gracefully.", worker_id, (int)getpid());
    exit(0);
}
