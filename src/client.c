#include "protocol.h"
#include "fifo_ipc.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>

static volatile sig_atomic_t g_running = 1;
static char g_client_fifo[FIFO_PATH_LEN];
static int g_server_fd = -1;
static int g_client_read_fd = -1;
static int g_client_dummy_fd = -1;
static int g_client_id = -1;
static char g_nickname[MAX_NICKNAME_LEN];

static void cleanup(void)
{
    if (g_client_dummy_fd >= 0) {
        close(g_client_dummy_fd);
        g_client_dummy_fd = -1;
    }
    if (g_client_read_fd >= 0) {
        close(g_client_read_fd);
        g_client_read_fd = -1;
    }
    if (g_server_fd >= 0) {
        close(g_server_fd);
        g_server_fd = -1;
    }
    fifo_destroy(g_client_fifo);
}

static void handle_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

static void *receiver_thread(void *arg)
{
    (void)arg;
    chat_msg_t incoming;

    while (g_running) {
        int res = fifo_recv_msg(g_client_read_fd, &incoming);
        if (res > 0) {
            if (incoming.msg_type == MSG_SERVER_NOTICE) {
                printf("\r\033[K\033[33m[NOTICE] %s\033[0m\n", incoming.content);
            } else if (incoming.msg_type == MSG_CHAT_ALL) {
                if (incoming.sender_id != g_client_id) {
                    printf("\r\033[K\033[36m[%s (#%d)]:\033[0m %s\n",
                           incoming.sender_nick, incoming.sender_id, incoming.content);
                }
            } else if (incoming.msg_type == MSG_CHAT_PRIVATE) {
                printf("\r\033[K\033[35m[PM from %s (#%d)]:\033[0m %s\n",
                       incoming.sender_nick, incoming.sender_id, incoming.content);
            } else if (incoming.msg_type == MSG_LIST_RESP) {
                printf("\r\033[K\033[32m%s\033[0m\n", incoming.content);
            } else if (incoming.msg_type == MSG_ERROR) {
                printf("\r\033[K\033[31m[ERROR] %s\033[0m\n", incoming.content);
            }
            printf("> ");
            fflush(stdout);
        } else if (res < 0 && errno != EINTR) {
            break;
        }
    }
    return NULL;
}

static void print_help(void)
{
    printf("\n\033[1;34m=== Multi-Process IPC Chat Commands ===\033[0m\n");
    printf("  \033[33m/help\033[0m                  : Display this help message\n");
    printf("  \033[33m/list\033[0m                  : List all currently online users\n");
    printf("  \033[33m/msg <nick|id> <text>\033[0m  : Send private message to a specific user\n");
    printf("  \033[33m/quit\033[0m                  : Disconnect and exit chat\n");
    printf("  \033[32m<message text>\033[0m         : Send public message to all chat participants\n\n");
}

int main(int argc, char *argv[])
{
    /* Signal handling */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    /* Nickname */
    if (argc >= 2) {
        strncpy(g_nickname, argv[1], sizeof(g_nickname) - 1);
    } else {
        printf("Enter your nickname: ");
        if (fgets(g_nickname, sizeof(g_nickname), stdin) == NULL) {
            return 1;
        }
        g_nickname[strcspn(g_nickname, "\r\n")] = '\0';
    }

    if (strlen(g_nickname) == 0) {
        snprintf(g_nickname, sizeof(g_nickname), "User_%d", (int)getpid());
    }

    /* Create unique response FIFO for this client */
    snprintf(g_client_fifo, sizeof(g_client_fifo), CLIENT_FIFO_TEMPLATE, (int)getpid());
    if (fifo_create(g_client_fifo) < 0) {
        fprintf(stderr, "Failed to create client response FIFO %s\n", g_client_fifo);
        return 1;
    }

    /* Open client FIFO with O_RDWR (ready immediately, prevents blocking) */
    g_client_read_fd = fifo_open_rdwr(g_client_fifo);
    if (g_client_read_fd < 0) {
        fprintf(stderr, "Failed to open client response FIFO\n");
        cleanup();
        return 1;
    }

    /* Open server FIFO for writing */
    g_server_fd = fifo_open_write(SERVER_FIFO_PATH, 1);
    if (g_server_fd < 0) {
        fprintf(stderr, "Cannot connect to server. Is the server running? (%s)\n", SERVER_FIFO_PATH);
        cleanup();
        return 1;
    }

    /* Send registration request */
    chat_msg_t reg_req;
    memset(&reg_req, 0, sizeof(reg_req));
    reg_req.msg_type = MSG_REGISTER;
    reg_req.sender_pid = getpid();
    strncpy(reg_req.sender_nick, g_nickname, sizeof(reg_req.sender_nick) - 1);
    strncpy(reg_req.client_fifo, g_client_fifo, sizeof(reg_req.client_fifo) - 1);

    if (fifo_send_msg(g_server_fd, &reg_req) <= 0) {
        fprintf(stderr, "Failed to send registration message to server\n");
        cleanup();
        return 1;
    }

    /* Wait for registration response */
    chat_msg_t reg_resp;
    if (fifo_recv_msg(g_client_read_fd, &reg_resp) <= 0) {
        fprintf(stderr, "No response from server\n");
        cleanup();
        return 1;
    }

    if (reg_resp.status_code != STATUS_SUCCESS) {
        fprintf(stderr, "Registration rejected by server: %s\n", reg_resp.content);
        cleanup();
        return 1;
    }

    g_client_id = reg_resp.sender_id;
    printf("\033[32m%s\033[0m\n", reg_resp.content);
    printf("Type '/help' for commands, '/quit' to exit.\n");

    /* Start background listener thread */
    pthread_t recv_tid;
    pthread_create(&recv_tid, NULL, receiver_thread, NULL);

    /* Main input loop */
    char input_buf[MAX_MSG_LEN];
    while (g_running) {
        printf("> ");
        fflush(stdout);

        if (fgets(input_buf, sizeof(input_buf), stdin) == NULL) {
            break;
        }
        input_buf[strcspn(input_buf, "\r\n")] = '\0';

        if (strlen(input_buf) == 0) {
            continue;
        }

        if (strcmp(input_buf, "/help") == 0) {
            print_help();
            continue;
        }

        if (strcmp(input_buf, "/list") == 0) {
            chat_msg_t list_msg;
            memset(&list_msg, 0, sizeof(list_msg));
            list_msg.msg_type = MSG_LIST_USERS;
            list_msg.sender_id = g_client_id;
            list_msg.sender_pid = getpid();
            strncpy(list_msg.sender_nick, g_nickname, sizeof(list_msg.sender_nick) - 1);
            fifo_send_msg(g_server_fd, &list_msg);
            continue;
        }

        if (strncmp(input_buf, "/msg ", 5) == 0) {
            char *target = input_buf + 5;
            while (*target == ' ') target++;
            char *space = strchr(target, ' ');
            if (!space) {
                printf("\033[31m[Usage] /msg <nickname|client_id> <message>\033[0m\n");
                continue;
            }
            *space = '\0';
            char *pm_text = space + 1;
            while (*pm_text == ' ') pm_text++;

            if (strlen(pm_text) == 0) {
                printf("\033[31m[Error] Message cannot be empty.\033[0m\n");
                continue;
            }

            chat_msg_t pm_msg;
            memset(&pm_msg, 0, sizeof(pm_msg));
            pm_msg.msg_type = MSG_CHAT_PRIVATE;
            pm_msg.sender_id = g_client_id;
            pm_msg.sender_pid = getpid();
            strncpy(pm_msg.sender_nick, g_nickname, sizeof(pm_msg.sender_nick) - 1);

            char *endptr;
            long tid = strtol(target, &endptr, 10);
            if (*endptr == '\0' && tid > 0) {
                pm_msg.target_id = (int)tid;
            } else {
                pm_msg.target_id = -1;
                strncpy(pm_msg.target_nick, target, sizeof(pm_msg.target_nick) - 1);
            }

            strncpy(pm_msg.content, pm_text, sizeof(pm_msg.content) - 1);
            pm_msg.content_len = strlen(pm_msg.content);

            if (fifo_send_msg(g_server_fd, &pm_msg) <= 0) {
                printf("\033[31m[ERROR] Failed to send private message.\033[0m\n");
            } else {
                printf("\033[35m[PM to %s]:\033[0m %s\n", target, pm_text);
            }
            continue;
        }

        if (strcmp(input_buf, "/quit") == 0) {
            /* Disconnect message */
            chat_msg_t disc_msg;
            memset(&disc_msg, 0, sizeof(disc_msg));
            disc_msg.msg_type = MSG_DISCONNECT;
            disc_msg.sender_id = g_client_id;
            disc_msg.sender_pid = getpid();
            strncpy(disc_msg.sender_nick, g_nickname, sizeof(disc_msg.sender_nick) - 1);
            fifo_send_msg(g_server_fd, &disc_msg);
            break;
        }

        /* Send public chat message */
        chat_msg_t chat_msg;
        memset(&chat_msg, 0, sizeof(chat_msg));
        chat_msg.msg_type = MSG_CHAT_ALL;
        chat_msg.sender_id = g_client_id;
        chat_msg.sender_pid = getpid();
        strncpy(chat_msg.sender_nick, g_nickname, sizeof(chat_msg.sender_nick) - 1);
        strncpy(chat_msg.content, input_buf, sizeof(chat_msg.content) - 1);
        chat_msg.content_len = strlen(chat_msg.content);

        if (fifo_send_msg(g_server_fd, &chat_msg) <= 0) {
            printf("\033[31m[ERROR] Failed to send message to server.\033[0m\n");
            break;
        }
    }

    g_running = 0;
    pthread_cancel(recv_tid);
    pthread_join(recv_tid, NULL);

    cleanup();
    printf("Disconnected from chat.\n");
    return 0;
}
