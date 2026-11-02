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
#include <sys/shm.h>
#include <sys/stat.h>
#include <libgen.h>

static volatile sig_atomic_t g_running = 1;
static char g_client_fifo[FIFO_PATH_LEN];
static int g_server_fd = -1;
static int g_client_read_fd = -1;
static int g_client_dummy_fd = -1;
static int g_client_id = -1;
static char g_nickname[MAX_NICKNAME_LEN];
static char g_current_room[MAX_ROOM_NAME_LEN] = DEFAULT_ROOM;
static int g_last_send_shmid = -1;

static void cleanup(void)
{
    if (g_last_send_shmid > 0) {
        shmctl(g_last_send_shmid, IPC_RMID, NULL);
        g_last_send_shmid = -1;
    }
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
                    const char *r = strlen(incoming.room) > 0 ? incoming.room : DEFAULT_ROOM;
                    printf("\r\033[K\033[36m[%s] [%s (#%d)]:\033[0m %s\n",
                           r, incoming.sender_nick, incoming.sender_id, incoming.content);
                }
            } else if (incoming.msg_type == MSG_CHAT_PRIVATE) {
                printf("\r\033[K\033[35m[PM from %s (#%d)]:\033[0m %s\n",
                       incoming.sender_nick, incoming.sender_id, incoming.content);
            } else if (incoming.msg_type == MSG_LIST_RESP) {
                printf("\r\033[K\033[32m%s\033[0m\n", incoming.content);
            } else if (incoming.msg_type == MSG_ROOM_RESP) {
                if (strlen(incoming.room) > 0) {
                    strncpy(g_current_room, incoming.room, sizeof(g_current_room) - 1);
                }
                printf("\r\033[K\033[1;36m[ROOM] %s\033[0m\n", incoming.content);
            } else if (incoming.msg_type == MSG_FILE_OFFER) {
                printf("\r\033[K\033[1;33m[FILE OFFER] User '%s' offered file '%s' (%zu bytes, shmid: %d)\033[0m\n",
                       incoming.sender_nick, incoming.file_name, incoming.file_size, incoming.file_shmid);

                mkdir("downloads", 0755);
                char out_path[256];
                snprintf(out_path, sizeof(out_path), "downloads/%s", incoming.file_name);

                int success = 0;
                void *shm_ptr = shmat(incoming.file_shmid, NULL, SHM_RDONLY);
                if (shm_ptr != (void *)-1) {
                    FILE *fp = fopen(out_path, "wb");
                    if (fp) {
                        if (incoming.file_size == 0 || fwrite(shm_ptr, 1, incoming.file_size, fp) == incoming.file_size) {
                            success = 1;
                        }
                        fclose(fp);
                    }
                    shmdt(shm_ptr);
                }

                chat_msg_t resp;
                memset(&resp, 0, sizeof(resp));
                resp.sender_id = g_client_id;
                resp.sender_pid = getpid();
                strncpy(resp.sender_nick, g_nickname, sizeof(resp.sender_nick) - 1);
                strncpy(resp.target_nick, incoming.sender_nick, sizeof(resp.target_nick) - 1);
                strncpy(resp.file_name, incoming.file_name, sizeof(resp.file_name) - 1);
                resp.file_size = incoming.file_size;
                resp.file_shmid = incoming.file_shmid;

                if (success) {
                    resp.msg_type = MSG_FILE_ACCEPT;
                    printf("\r\033[K\033[1;32m[FILE] Received '%s' (%zu bytes) -> Saved to %s\033[0m\n",
                           incoming.file_name, incoming.file_size, out_path);
                } else {
                    resp.msg_type = MSG_FILE_REJECT;
                    printf("\r\033[K\033[1;31m[FILE] Failed to receive file '%s'\033[0m\n", incoming.file_name);
                }
                fifo_send_msg(g_server_fd, &resp);

            } else if (incoming.msg_type == MSG_FILE_ACCEPT) {
                printf("\r\033[K\033[1;32m[FILE SUCCESS] %s accepted and saved '%s' (%zu bytes)!\033[0m\n",
                       incoming.sender_nick, incoming.file_name, incoming.file_size);
                if (incoming.file_shmid > 0) {
                    shmctl(incoming.file_shmid, IPC_RMID, NULL);
                    if (g_last_send_shmid == incoming.file_shmid) {
                        g_last_send_shmid = -1;
                    }
                    printf("\r\033[K\033[36m[FILE] Released Shared Memory segment (shmid: %d).\033[0m\n",
                           incoming.file_shmid);
                }
            } else if (incoming.msg_type == MSG_FILE_REJECT) {
                printf("\r\033[K\033[1;31m[FILE REJECTED] %s rejected transfer of '%s'.\033[0m\n",
                       incoming.sender_nick, incoming.file_name);
                if (incoming.file_shmid > 0) {
                    shmctl(incoming.file_shmid, IPC_RMID, NULL);
                    if (g_last_send_shmid == incoming.file_shmid) {
                        g_last_send_shmid = -1;
                    }
                }
            } else if (incoming.msg_type == MSG_ERROR) {
                printf("\r\033[K\033[31m[ERROR] %s\033[0m\n", incoming.content);
                if (g_last_send_shmid > 0) {
                    shmctl(g_last_send_shmid, IPC_RMID, NULL);
                    g_last_send_shmid = -1;
                }
            }
            printf("[%s] > ", g_current_room);
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
    printf("  \033[33m/rooms\033[0m                 : List active channels and rooms\n");
    printf("  \033[33m/join <#room>\033[0m          : Join or switch to a specific channel (e.g. /join #linux)\n");
    printf("  \033[33m/leave\033[0m                 : Leave current channel and return to #general\n");
    printf("  \033[33m/list\033[0m                  : List all currently online users\n");
    printf("  \033[33m/msg <nick|id> <text>\033[0m  : Send private message to a specific user\n");
    printf("  \033[33m/sendfile <nick> <path>\033[0m: Transfer file via zero-copy Shared Memory\n");
    printf("  \033[33m/quit\033[0m                  : Disconnect and exit chat\n");
    printf("  \033[32m<message text>\033[0m         : Send message to current channel\n\n");
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
        printf("[%s] > ", g_current_room);
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

        if (strcmp(input_buf, "/rooms") == 0) {
            chat_msg_t rmsg;
            memset(&rmsg, 0, sizeof(rmsg));
            rmsg.msg_type = MSG_ROOM_LIST;
            rmsg.sender_id = g_client_id;
            rmsg.sender_pid = getpid();
            strncpy(rmsg.sender_nick, g_nickname, sizeof(rmsg.sender_nick) - 1);
            fifo_send_msg(g_server_fd, &rmsg);
            continue;
        }

        if (strncmp(input_buf, "/join ", 6) == 0) {
            char *rname = input_buf + 6;
            while (*rname == ' ') rname++;
            if (strlen(rname) == 0) {
                printf("\033[31m[Usage] /join <#channel_name>\033[0m\n");
                continue;
            }
            char formatted_room[MAX_ROOM_NAME_LEN];
            if (rname[0] != '#') {
                snprintf(formatted_room, sizeof(formatted_room), "#%s", rname);
            } else {
                strncpy(formatted_room, rname, sizeof(formatted_room) - 1);
                formatted_room[sizeof(formatted_room) - 1] = '\0';
            }

            chat_msg_t jmsg;
            memset(&jmsg, 0, sizeof(jmsg));
            jmsg.msg_type = MSG_ROOM_JOIN;
            jmsg.sender_id = g_client_id;
            jmsg.sender_pid = getpid();
            strncpy(jmsg.sender_nick, g_nickname, sizeof(jmsg.sender_nick) - 1);
            strncpy(jmsg.room, formatted_room, sizeof(jmsg.room) - 1);
            fifo_send_msg(g_server_fd, &jmsg);
            continue;
        }

        if (strcmp(input_buf, "/leave") == 0) {
            chat_msg_t lmsg;
            memset(&lmsg, 0, sizeof(lmsg));
            lmsg.msg_type = MSG_ROOM_LEAVE;
            lmsg.sender_id = g_client_id;
            lmsg.sender_pid = getpid();
            strncpy(lmsg.sender_nick, g_nickname, sizeof(lmsg.sender_nick) - 1);
            strncpy(lmsg.room, g_current_room, sizeof(lmsg.room) - 1);
            fifo_send_msg(g_server_fd, &lmsg);
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

        if (strncmp(input_buf, "/sendfile ", 10) == 0) {
            char *args = input_buf + 10;
            while (*args == ' ') args++;
            char *space = strchr(args, ' ');
            if (!space) {
                printf("\033[31m[Usage] /sendfile <nickname|client_id> <filepath>\033[0m\n");
                continue;
            }
            *space = '\0';
            char *target = args;
            char *filepath = space + 1;
            while (*filepath == ' ') filepath++;

            struct stat st;
            if (stat(filepath, &st) < 0 || S_ISDIR(st.st_mode)) {
                printf("\033[31m[ERROR] File '%s' does not exist or is a directory.\033[0m\n", filepath);
                continue;
            }

            size_t fsize = (size_t)st.st_size;
            if (fsize > 16 * 1024 * 1024) {
                printf("\033[31m[ERROR] File exceeds maximum allowed size (16MB).\033[0m\n");
                continue;
            }

            int shmid = shmget(IPC_PRIVATE, fsize > 0 ? fsize : 1, IPC_CREAT | 0666);
            if (shmid < 0) {
                perror("shmget");
                continue;
            }

            void *shm_ptr = shmat(shmid, NULL, 0);
            if (shm_ptr == (void *)-1) {
                perror("shmat");
                shmctl(shmid, IPC_RMID, NULL);
                continue;
            }

            FILE *fp = fopen(filepath, "rb");
            if (!fp) {
                perror("fopen");
                shmdt(shm_ptr);
                shmctl(shmid, IPC_RMID, NULL);
                continue;
            }

            if (fsize > 0 && fread(shm_ptr, 1, fsize, fp) != fsize) {
                printf("\033[31m[ERROR] Failed to read full file content.\033[0m\n");
                fclose(fp);
                shmdt(shm_ptr);
                shmctl(shmid, IPC_RMID, NULL);
                continue;
            }
            fclose(fp);
            shmdt(shm_ptr);

            chat_msg_t fmsg;
            memset(&fmsg, 0, sizeof(fmsg));
            fmsg.msg_type = MSG_FILE_OFFER;
            fmsg.sender_id = g_client_id;
            fmsg.sender_pid = getpid();
            strncpy(fmsg.sender_nick, g_nickname, sizeof(fmsg.sender_nick) - 1);

            char *endptr;
            long tid = strtol(target, &endptr, 10);
            if (*endptr == '\0' && tid > 0) {
                fmsg.target_id = (int)tid;
            } else {
                fmsg.target_id = -1;
                strncpy(fmsg.target_nick, target, sizeof(fmsg.target_nick) - 1);
            }

            char path_copy[256];
            strncpy(path_copy, filepath, sizeof(path_copy) - 1);
            path_copy[sizeof(path_copy) - 1] = '\0';
            char *bname = basename(path_copy);
            strncpy(fmsg.file_name, bname, sizeof(fmsg.file_name) - 1);
            fmsg.file_size = fsize;
            fmsg.file_shmid = shmid;
            snprintf(fmsg.content, sizeof(fmsg.content), "Zero-Copy File: %s (%zu bytes)", bname, fsize);
            fmsg.content_len = strlen(fmsg.content);

            if (fifo_send_msg(g_server_fd, &fmsg) <= 0) {
                printf("\033[31m[ERROR] Failed to send file offer to server.\033[0m\n");
                shmctl(shmid, IPC_RMID, NULL);
            } else {
                g_last_send_shmid = shmid;
                printf("\033[1;34m[FILE] Offering '%s' (%zu bytes) to %s via SHM (shmid: %d)... Waiting for receiver...\033[0m\n",
                       bname, fsize, target, shmid);
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

        /* Send public chat message to current room */
        chat_msg_t chat_msg;
        memset(&chat_msg, 0, sizeof(chat_msg));
        chat_msg.msg_type = MSG_CHAT_ALL;
        chat_msg.sender_id = g_client_id;
        chat_msg.sender_pid = getpid();
        strncpy(chat_msg.sender_nick, g_nickname, sizeof(chat_msg.sender_nick) - 1);
        strncpy(chat_msg.room, g_current_room, sizeof(chat_msg.room) - 1);
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
