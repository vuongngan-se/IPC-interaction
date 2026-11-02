#include "protocol.h"
#include "shared_memory.h"
#include "semaphore.h"
#include "msg_queue.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/msg.h>
#include <sys/sem.h>

static volatile sig_atomic_t g_keep_running = 1;

static void handle_sig(int sig)
{
    (void)sig;
    g_keep_running = 0;
}

static void render_dashboard(shared_state_t *shm, int msqid, int semid, key_t ipc_key)
{
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_info);

    /* Fetch Message Queue stats */
    struct msqid_ds mq_ds;
    memset(&mq_ds, 0, sizeof(mq_ds));
    int mq_ok = 0;
    if (msqid >= 0 && msgctl(msqid, IPC_STAT, &mq_ds) == 0) {
        mq_ok = 1;
    }

    /* Fetch Semaphore value */
    int sem_val = -1;
    if (semid >= 0) {
        sem_val = semctl(semid, 0, GETVAL);
    }

    /* Check if server process is alive */
    int server_alive = 0;
    if (shm->server_pid > 0 && kill(shm->server_pid, 0) == 0) {
        server_alive = 1;
    }

    /* Clear screen and move cursor to top-left */
    printf("\033[2J\033[H");

    /* Header */
    printf("\033[1;37;44m========================================================================================\033[0m\n");
    printf("\033[1;37;44m                      LINUX MULTI-PROCESS IPC CHAT MONITOR (IPC-TOP)                    \033[0m\n");
    printf("\033[1;37;44m========================================================================================\033[0m\n");
    printf(" \033[1;33mCurrent Time:\033[0m %s  |  \033[1;33mIPC Key:\033[0m 0x%08x  |  \033[1;33mRefresh:\033[0m 1.0s (Press Ctrl+C to exit)\n\n",
           time_str, (unsigned int)ipc_key);

    /* Section 1: Server & Process Status */
    printf("\033[1;36m[1. SERVER & SYSTEM STATUS]\033[0m\n");
    printf("  * Server Status:      %s\n",
           server_alive ? "\033[1;32m● RUNNING (ACTIVE)\033[0m" : "\033[1;31m○ STOPPED / INACTIVE\033[0m");
    printf("  * Server PID:         \033[1;37m%d\033[0m\n", (int)shm->server_pid);
    printf("  * Active Clients:     \033[1;32m%d\033[0m / %d (Max)\n",
           shm->active_client_count, MAX_CLIENTS);
    printf("  * Next Client ID:     #%d\n", shm->next_client_id);
    printf("\n");

    /* Section 2: IPC Primitives Telemetry */
    printf("\033[1;36m[2. IPC PRIMITIVES TELEMETRY]\033[0m\n");
    printf("  ┌─────────────────────────────────┬─────────────────────────────────┬─────────────────────────────────┐\n");
    printf("  │ \033[1;33mSYSTEM V MESSAGE QUEUE\033[0m          │ \033[1;33mSYSTEM V SHARED MEMORY\033[0m          │ \033[1;33mSYSTEM V SEMAPHORE (MUTEX)\033[0m      │\n");
    printf("  ├─────────────────────────────────┼─────────────────────────────────┼─────────────────────────────────┤\n");
    printf("  │ MSQID:         %-16d │ SHMID:         %-16s │ SEMID:         %-16d │\n",
           msqid, "Attached (shmat)", semid);
    if (mq_ok) {
        printf("  │ Queued Msgs:   \033[1;32m%-16lu\033[0m │ Segment Size:  %-12zu B │ Sem Value:     %-16s │\n",
               (unsigned long)mq_ds.msg_qnum, sizeof(shared_state_t),
               sem_val == 1 ? "\033[1;32m1 (UNLOCKED)\033[0m" : (sem_val == 0 ? "\033[1;31m0 (LOCKED)\033[0m" : "N/A"));
        printf("  │ Queue Bytes:   %-12lu B │ Struct Version:%-16s │ Semaphore Flag:SEM_UNDO Enabled  │\n",
               (unsigned long)mq_ds.msg_cbytes, "shared_state_t");
        printf("  │ Max Queue:     %-12lu B │ Atomic Lock:   System V Sem     │ Last Oper PID: %-16d │\n",
               (unsigned long)mq_ds.msg_qbytes, (int)mq_ds.msg_lspid);
    } else {
        printf("  │ Status:        Unavailable      │ Segment Size:  %-12zu B │ Sem Value:     %-16d │\n",
               sizeof(shared_state_t), sem_val);
    }
    printf("  └─────────────────────────────────┴─────────────────────────────────┴─────────────────────────────────┘\n");
    printf("  * \033[1;33mServer FIFO:\033[0m %s (%s)\n",
           SERVER_FIFO_PATH,
           access(SERVER_FIFO_PATH, F_OK) == 0 ? "\033[1;32mActive Named Pipe\033[0m" : "\033[1;31mNot Found\033[0m");
    printf("\n");

    /* Section 3: Registered Clients Table */
    printf("\033[1;36m[3. ONLINE CLIENTS DIRECTORY]\033[0m\n");
    printf("  +----+----------+--------+--------+------------+-------------------------------+---------------------+\n");
    printf("  | ID | NICKNAME | PID    | STATUS | UPTIME     | RESPONSE FIFO PATH            | PROCESS HEALTH      |\n");
    printf("  +----+----------+--------+--------+------------+-------------------------------+---------------------+\n");

    int displayed_count = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (shm->clients[i].is_online) {
            displayed_count++;
            long uptime = (long)(now - shm->clients[i].join_time);
            int is_alive = (kill(shm->clients[i].pid, 0) == 0);

            printf("  | #%-2d| %-9s| %-7d| \033[1;32mONLINE\033[0m | %02ldm %02lds     | %-30s| %-20s|\n",
                   shm->clients[i].client_id,
                   shm->clients[i].nickname,
                   (int)shm->clients[i].pid,
                   uptime / 60, uptime % 60,
                   shm->clients[i].response_fifo,
                   is_alive ? "\033[1;32mAlive (OS Ok)\033[0m" : "\033[1;31mDead / Orphaned\033[0m");
        }
    }

    if (displayed_count == 0) {
        printf("  |                       No clients currently connected to the server                                 |\n");
    }
    printf("  +----+----------+--------+--------+------------+-------------------------------+---------------------+\n");
    fflush(stdout);
}

int main(int argc, char *argv[])
{
    int run_once = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--once") == 0) {
            run_once = 1;
        }
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sig;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    key_t ipc_key = ftok(SERVER_FIFO_PATH, PROJECT_ID);
    if (ipc_key == (key_t)-1) {
        ipc_key = 0x53455256;
    }

    int shmid = -1;
    shared_state_t *shm = shm_attach_existing(ipc_key, &shmid);
    if (!shm) {
        fprintf(stderr, "\033[31m[Error] Cannot attach to Shared Memory (Key: 0x%08x).\033[0m\n", (unsigned int)ipc_key);
        fprintf(stderr, "Is the IPC Chat Server running? Run './bin/server' first.\n");
        return 1;
    }

    int msqid = msgget(ipc_key, 0666);
    int semid = sem_get_existing(ipc_key);

    while (g_keep_running) {
        render_dashboard(shm, msqid, semid, ipc_key);
        if (run_once) {
            break;
        }
        sleep(1);
    }

    shm_detach(shm);
    printf("\n\033[0mMonitor stopped cleanly.\n");
    return 0;
}
