#include "protocol.h"
#include "fifo_ipc.h"
#include "msg_queue.h"
#include "shared_memory.h"
#include "semaphore.h"
#include "worker.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>

static volatile sig_atomic_t g_running = 1;
static int g_server_read_fd = -1;

/* IPC Resources */
static int g_msqid = -1;
static int g_shmid = -1;
static int g_semid = -1;
static shared_state_t *g_shm_state = NULL;

/* Worker tracking */
static pid_t g_worker_pids[WORKER_COUNT];

static void handle_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

static void handle_sigchld(int sig)
{
    (void)sig;
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        /* reap zombie children */
    }
    errno = saved_errno;
}

static void server_broadcast_shutdown(void)
{
    if (g_shm_state == NULL) return;

    if (g_semid >= 0) sem_lock(g_semid);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_shm_state->clients[i].is_online) {
            chat_msg_t notice;
            memset(&notice, 0, sizeof(notice));
            notice.msg_type = MSG_SERVER_NOTICE;
            notice.sender_id = 0;
            strncpy(notice.sender_nick, "SERVER", sizeof(notice.sender_nick) - 1);
            strncpy(notice.content, "*** SERVER SHUTDOWN: The chat server is stopping ***", sizeof(notice.content) - 1);
            notice.content_len = strlen(notice.content);

            int fd = fifo_open_write(g_shm_state->clients[i].response_fifo, 1);
            if (fd >= 0) {
                fifo_send_msg(fd, &notice);
                close(fd);
            }
        }
    }
    if (g_semid >= 0) sem_unlock(g_semid);
}

int main(void)
{
    logger_init("logs/server.log");
    LOG_INFO("=====================================================");
    LOG_INFO("=== Starting Multi-Process IPC Chat Server (Linux) ===");
    LOG_INFO("=====================================================");

    /* Setup signal handlers */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Signal handler for reaping dead child workers (SIGCHLD) */
    struct sigaction sa_chld;
    memset(&sa_chld, 0, sizeof(sa_chld));
    sa_chld.sa_handler = handle_sigchld;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa_chld, NULL);

    /* Ignore SIGPIPE when client disconnects while writing */
    signal(SIGPIPE, SIG_IGN);

    /* 1. Create Server FIFO */
    if (fifo_create(SERVER_FIFO_PATH) < 0) {
        LOG_ERROR("Could not create server FIFO: %s", SERVER_FIFO_PATH);
        logger_close();
        return 1;
    }
    LOG_INFO("[IPC: FIFO] Created server FIFO: %s", SERVER_FIFO_PATH);

    /* Generate IPC key for System V resources */
    key_t ipc_key = ftok(SERVER_FIFO_PATH, PROJECT_ID);
    if (ipc_key == (key_t)-1) {
        LOG_WARN("ftok failed, falling back to static key");
        ipc_key = 0x53455256; /* 'SERV' in hex */
    }
    LOG_INFO("[IPC: Key] Generated IPC Key: 0x%08x", (unsigned int)ipc_key);

    /* 2. Create System V Message Queue */
    g_msqid = msg_queue_get_or_create(ipc_key);
    if (g_msqid < 0) {
        LOG_ERROR("Failed to initialize Message Queue");
        fifo_destroy(SERVER_FIFO_PATH);
        logger_close();
        return 1;
    }
    LOG_INFO("[IPC: Message Queue] Initialized msqid = %d", g_msqid);

    /* 3. Create System V Semaphore */
    g_semid = sem_create_and_init(ipc_key);
    if (g_semid < 0) {
        LOG_ERROR("Failed to initialize Semaphore");
        msg_queue_destroy(g_msqid);
        fifo_destroy(SERVER_FIFO_PATH);
        logger_close();
        return 1;
    }
    LOG_INFO("[IPC: Semaphore] Initialized binary mutex semid = %d", g_semid);

    /* 4. Create System V Shared Memory */
    g_shm_state = shm_create_and_attach(ipc_key, &g_shmid);
    if (g_shm_state == NULL) {
        LOG_ERROR("Failed to initialize Shared Memory");
        sem_destroy(g_semid);
        msg_queue_destroy(g_msqid);
        fifo_destroy(SERVER_FIFO_PATH);
        logger_close();
        return 1;
    }
    LOG_INFO("[IPC: Shared Memory] Initialized shmid = %d (size: %zu bytes)",
             g_shmid, sizeof(shared_state_t));

    /* Initialize shared state under semaphore protection */
    sem_lock(g_semid);
    memset(g_shm_state, 0, sizeof(shared_state_t));
    g_shm_state->server_status = 1;
    g_shm_state->server_pid = getpid();
    g_shm_state->active_client_count = 0;
    g_shm_state->next_client_id = 0;
    sem_unlock(g_semid);

    /* 5. Spawn Worker processes via fork() */
    for (int i = 0; i < WORKER_COUNT; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            LOG_ERROR("fork() failed for Worker %d: %s", i + 1, strerror(errno));
            break;
        } else if (pid == 0) {
            /* Child process: Worker */
            worker_run(i + 1, g_msqid, g_semid, g_shm_state);
            exit(0);
        } else {
            /* Parent process: Dispatcher Server */
            g_worker_pids[i] = pid;
            LOG_INFO("[Process Management] Spawned Worker %d (PID: %d)", i + 1, (int)pid);
        }
    }

    /* 6. Open Server FIFO for reading/writing */
    g_server_read_fd = fifo_open_rdwr(SERVER_FIFO_PATH);
    if (g_server_read_fd < 0) {
        LOG_ERROR("Failed to open server FIFO");
        g_running = 0;
    }

    LOG_INFO("[Server Dispatcher] Listening for client requests. Press Ctrl+C to stop.");

    /* Main Dispatcher Loop: reads from FIFO, pushes to Message Queue */
    chat_msg_t req;
    while (g_running) {
        int ret = fifo_recv_msg(g_server_read_fd, &req);
        if (!g_running) break;

        if (ret > 0) {
            worker_msg_t wmsg;
            memset(&wmsg, 0, sizeof(wmsg));
            wmsg.mtype = 1;
            wmsg.chat_msg = req;

            LOG_DEBUG("[Dispatcher] Enqueuing request (type: %d) from PID %d to Message Queue",
                      req.msg_type, (int)req.sender_pid);

            if (msg_queue_send(g_msqid, &wmsg, 0) < 0) {
                LOG_ERROR("[Dispatcher] Failed to send to Message Queue");
            }
        } else if (ret < 0 && errno != EINTR) {
            LOG_ERROR("Error reading from server FIFO");
            break;
        }
    }

    LOG_INFO("Shutting down Server: terminating workers and cleaning IPC resources...");

    /* Notify active clients */
    server_broadcast_shutdown();

    /* Terminate Worker processes gracefully */
    for (int i = 0; i < WORKER_COUNT; i++) {
        if (g_worker_pids[i] > 0) {
            LOG_INFO("Sending SIGTERM to Worker %d (PID: %d)", i + 1, (int)g_worker_pids[i]);
            kill(g_worker_pids[i], SIGTERM);
            waitpid(g_worker_pids[i], NULL, 0);
        }
    }

    /* Close and remove Server FIFO */
    if (g_server_read_fd >= 0) close(g_server_read_fd);
    fifo_destroy(SERVER_FIFO_PATH);
    LOG_INFO("[Cleanup] Removed Server FIFO: %s", SERVER_FIFO_PATH);

    /* Destroy System V Message Queue */
    if (g_msqid >= 0) {
        msg_queue_destroy(g_msqid);
        LOG_INFO("[Cleanup] Destroyed Message Queue (msqid: %d)", g_msqid);
    }

    /* Detach and destroy System V Shared Memory */
    if (g_shm_state != NULL) {
        shm_detach(g_shm_state);
    }
    if (g_shmid >= 0) {
        shm_destroy(g_shmid);
        LOG_INFO("[Cleanup] Destroyed Shared Memory (shmid: %d)", g_shmid);
    }

    /* Destroy System V Semaphore */
    if (g_semid >= 0) {
        sem_destroy(g_semid);
        LOG_INFO("[Cleanup] Destroyed Semaphore (semid: %d)", g_semid);
    }

    LOG_INFO("Server terminated cleanly. All IPC resources freed.");
    logger_close();
    return 0;
}
