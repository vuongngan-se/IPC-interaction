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
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <libgen.h>
#include <time.h>

#define MAX_HISTORY 200
#define SIDEBAR_WIDTH 24
#define INPUT_MAX_LEN 256

typedef struct {
    char text[1024];
    char timestamp[12];
} history_entry_t;

typedef struct {
    int id;
    char nick[MAX_NICKNAME_LEN];
    char room[MAX_ROOM_NAME_LEN];
} online_user_entry_t;

static volatile sig_atomic_t g_running = 1;
static char g_client_fifo[FIFO_PATH_LEN];
static int g_server_fd = -1;
static int g_client_read_fd = -1;
static int g_client_id = -1;
static char g_nickname[MAX_NICKNAME_LEN];
static char g_current_room[MAX_ROOM_NAME_LEN] = DEFAULT_ROOM;
static int g_last_send_shmid = -1;

static struct termios g_orig_termios;
static int g_raw_mode_enabled = 0;
static pthread_mutex_t g_ui_lock = PTHREAD_MUTEX_INITIALIZER;

static history_entry_t g_history[MAX_HISTORY];
static int g_history_count = 0;
static online_user_entry_t g_online_users[MAX_CLIENTS];
static int g_online_user_count = 0;

static char g_input_line[INPUT_MAX_LEN] = {0};
static int g_input_pos = 0;

static void disable_raw_mode(void)
{
    if (g_raw_mode_enabled) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
        g_raw_mode_enabled = 0;
        printf("\033[?25h\033[0m\033[2J\033[H");
        fflush(stdout);
    }
}

static void enable_raw_mode(void)
{
    if (!isatty(STDIN_FILENO)) return;
    if (tcgetattr(STDIN_FILENO, &g_orig_termios) == -1) return;
    atexit(disable_raw_mode);

    struct termios raw = g_orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != -1) {
        g_raw_mode_enabled = 1;
        printf("\033[?25h");
        fflush(stdout);
    }
}

static void get_term_size(int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        *cols = 80;
        *rows = 24;
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
    }
}

static void get_current_time_str(char *buf, size_t maxlen)
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buf, maxlen, "%H:%M:%S", tm_info);
}

static void add_history_msg(const char *msg)
{
    pthread_mutex_lock(&g_ui_lock);
    if (g_history_count < MAX_HISTORY) {
        get_current_time_str(g_history[g_history_count].timestamp, sizeof(g_history[g_history_count].timestamp));
        strncpy(g_history[g_history_count].text, msg, sizeof(g_history[g_history_count].text) - 1);
        g_history[g_history_count].text[sizeof(g_history[g_history_count].text) - 1] = '\0';
        g_history_count++;
    } else {
        memmove(&g_history[0], &g_history[1], sizeof(history_entry_t) * (MAX_HISTORY - 1));
        get_current_time_str(g_history[MAX_HISTORY - 1].timestamp, sizeof(g_history[MAX_HISTORY - 1].timestamp));
        strncpy(g_history[MAX_HISTORY - 1].text, msg, sizeof(g_history[MAX_HISTORY - 1].text) - 1);
        g_history[MAX_HISTORY - 1].text[sizeof(g_history[MAX_HISTORY - 1].text) - 1] = '\0';
    }
    pthread_mutex_unlock(&g_ui_lock);
}

static void draw_ui(void)
{
    pthread_mutex_lock(&g_ui_lock);
    if (!g_raw_mode_enabled) {
        if (g_history_count > 0) {
            printf("[%s] %s\n", g_history[g_history_count - 1].timestamp, g_history[g_history_count - 1].text);
            fflush(stdout);
        }
        pthread_mutex_unlock(&g_ui_lock);
        return;
    }

    int rows, cols;
    get_term_size(&rows, &cols);

    /* Clear screen and move cursor to (1,1) */
    printf("\033[H");

    /* 1. Header Bar (Row 1) */
    printf("\033[7m\033[1;36m"); /* Inverted cyan */
    char header_left[128];
    snprintf(header_left, sizeof(header_left), "  IPC CHAT TUI | User: %s (#%d) | Channel: %s",
             g_nickname, g_client_id, g_current_room);
    printf("%-s", header_left);
    int pad = cols - (int)strlen(header_left);
    if (pad > 0) {
        for (int i = 0; i < pad; i++) putchar(' ');
    }
    printf("\033[0m\n");

    /* 2. Main Body: Left Chat History (cols - SIDEBAR_WIDTH - 1), Right Online Users (SIDEBAR_WIDTH) */
    int chat_width = cols - SIDEBAR_WIDTH - 1;
    if (chat_width < 20) chat_width = 20;
    int body_rows = rows - 5;
    if (body_rows < 3) body_rows = 3;

    int history_start = 0;
    if (g_history_count > body_rows) {
        history_start = g_history_count - body_rows;
    }

    for (int r = 0; r < body_rows; r++) {
        /* Line number r in body */
        int hist_idx = history_start + r;
        char chat_line[1100] = {0};
        if (hist_idx < g_history_count) {
            snprintf(chat_line, sizeof(chat_line), "[%s] %s",
                     g_history[hist_idx].timestamp, g_history[hist_idx].text);
        }

        /* Print chat part */
        printf("\033[K");
        int printed = 0;
        for (size_t c = 0; c < strlen(chat_line) && printed < chat_width - 1; c++) {
            putchar(chat_line[c]);
            printed++;
        }
        while (printed < chat_width) {
            putchar(' ');
            printed++;
        }

        /* Divider */
        printf("\033[34m│\033[0m");

        /* Sidebar part */
        if (r == 0) {
            printf("\033[1;33m Online Users (%d) \033[0m", g_online_user_count);
        } else {
            int user_idx = r - 1;
            if (user_idx < g_online_user_count) {
                printf(" %-2d %-10.10s \033[36m%-6.6s\033[0m",
                       g_online_users[user_idx].id,
                       g_online_users[user_idx].nick,
                       g_online_users[user_idx].room);
            }
        }
        printf("\n");
    }

    /* 3. Divider line */
    printf("\033[34m");
    for (int c = 0; c < cols; c++) {
        if (c == chat_width) fputs("┴", stdout);
        else fputs("─", stdout);
    }
    printf("\033[0m\n");

    /* 4. Help / Status bar */
    printf("\033[90m Commands: /rooms | /join <#room> | /msg <user> <txt> | /sendfile <user> <path> | /quit\033[0m\033[K\n");

    /* 5. Input prompt bar */
    printf("\033[1;32m[%s] > \033[0m%s\033[K", g_current_room, g_input_line);

    /* Move cursor to actual input position */
    int prompt_len = (int)strlen(g_current_room) + 5;
    printf("\033[%d;%dH", rows, prompt_len + g_input_pos + 1);

    fflush(stdout);
    pthread_mutex_unlock(&g_ui_lock);
}

static void cleanup(void)
{
    disable_raw_mode();
    if (g_last_send_shmid > 0) {
        shmctl(g_last_send_shmid, IPC_RMID, NULL);
        g_last_send_shmid = -1;
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

static void parse_online_users(const char *content)
{
    pthread_mutex_lock(&g_ui_lock);
    g_online_user_count = 0;
    const char *line = content;
    while (line && *line) {
        const char *next = strchr(line, '\n');
        if (strncmp(line, "  * ID #", 8) == 0) {
            int id = 0;
            char nick[MAX_NICKNAME_LEN] = {0};
            char room[MAX_ROOM_NAME_LEN] = DEFAULT_ROOM;
            if (sscanf(line, "  * ID #%d: %31s", &id, nick) >= 2) {
                const char *rpos = strstr(line, "in room [");
                if (rpos) {
                    sscanf(rpos, "in room [%31[^]]", room);
                }
                if (g_online_user_count < MAX_CLIENTS) {
                    g_online_users[g_online_user_count].id = id;
                    strncpy(g_online_users[g_online_user_count].nick, nick, MAX_NICKNAME_LEN - 1);
                    strncpy(g_online_users[g_online_user_count].room, room, MAX_ROOM_NAME_LEN - 1);
                    g_online_user_count++;
                }
            }
        }
        line = next ? next + 1 : NULL;
    }
    pthread_mutex_unlock(&g_ui_lock);
}

static void *receiver_thread(void *arg)
{
    (void)arg;
    chat_msg_t incoming;

    while (g_running) {
        int res = fifo_recv_msg(g_client_read_fd, &incoming);
        if (res > 0) {
            char buf[1024];
            if (incoming.msg_type == MSG_SERVER_NOTICE) {
                snprintf(buf, sizeof(buf), "*** %s ***", incoming.content);
                add_history_msg(buf);
            } else if (incoming.msg_type == MSG_CHAT_ALL) {
                if (incoming.sender_id != g_client_id) {
                    const char *r = strlen(incoming.room) > 0 ? incoming.room : DEFAULT_ROOM;
                    snprintf(buf, sizeof(buf), "[%s] <%s>: %s", r, incoming.sender_nick, incoming.content);
                    add_history_msg(buf);
                }
            } else if (incoming.msg_type == MSG_CHAT_PRIVATE) {
                snprintf(buf, sizeof(buf), "[PM from %s]: %s", incoming.sender_nick, incoming.content);
                add_history_msg(buf);
            } else if (incoming.msg_type == MSG_LIST_RESP) {
                parse_online_users(incoming.content);
                add_history_msg("Refreshed online users list.");
            } else if (incoming.msg_type == MSG_ROOM_RESP) {
                if (strlen(incoming.room) > 0) {
                    strncpy(g_current_room, incoming.room, sizeof(g_current_room) - 1);
                }
                snprintf(buf, sizeof(buf), "[Channel] %s", incoming.content);
                add_history_msg(buf);
            } else if (incoming.msg_type == MSG_FILE_OFFER) {
                snprintf(buf, sizeof(buf), "[File Offer] %s offered '%s' (%zu bytes)",
                         incoming.sender_nick, incoming.file_name, incoming.file_size);
                add_history_msg(buf);

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
                    snprintf(buf, sizeof(buf), "[File] Saved to %s (%zu bytes)", out_path, incoming.file_size);
                } else {
                    resp.msg_type = MSG_FILE_REJECT;
                    snprintf(buf, sizeof(buf), "[File] Failed downloading %s", incoming.file_name);
                }
                add_history_msg(buf);
                fifo_send_msg(g_server_fd, &resp);

            } else if (incoming.msg_type == MSG_FILE_ACCEPT) {
                snprintf(buf, sizeof(buf), "[File] %s accepted '%s' (%zu bytes)",
                         incoming.sender_nick, incoming.file_name, incoming.file_size);
                add_history_msg(buf);
                if (incoming.file_shmid > 0) {
                    shmctl(incoming.file_shmid, IPC_RMID, NULL);
                    if (g_last_send_shmid == incoming.file_shmid) g_last_send_shmid = -1;
                }
            } else if (incoming.msg_type == MSG_FILE_REJECT) {
                snprintf(buf, sizeof(buf), "[File] %s rejected transfer of '%s'",
                         incoming.sender_nick, incoming.file_name);
                add_history_msg(buf);
                if (incoming.file_shmid > 0) {
                    shmctl(incoming.file_shmid, IPC_RMID, NULL);
                    if (g_last_send_shmid == incoming.file_shmid) g_last_send_shmid = -1;
                }
            } else if (incoming.msg_type == MSG_HISTORY_RESP) {
                char *line = incoming.content;
                while (line && *line) {
                    char *next = strchr(line, '\n');
                    if (next) *next = '\0';
                    if (strlen(line) > 0) add_history_msg(line);
                    line = next ? next + 1 : NULL;
                }
            } else if (incoming.msg_type == MSG_ERROR) {
                snprintf(buf, sizeof(buf), "[ERROR] %s", incoming.content);
                add_history_msg(buf);
                if (g_last_send_shmid > 0) {
                    shmctl(g_last_send_shmid, IPC_RMID, NULL);
                    g_last_send_shmid = -1;
                }
            }
            draw_ui();
        } else if (res < 0 && errno != EINTR) {
            break;
        }
    }
    return NULL;
}

static void *sidebar_poller_thread(void *arg)
{
    (void)arg;
    while (g_running) {
        /* Periodically query users to keep sidebar populated */
        chat_msg_t list_msg;
        memset(&list_msg, 0, sizeof(list_msg));
        list_msg.msg_type = MSG_LIST_USERS;
        list_msg.sender_id = g_client_id;
        list_msg.sender_pid = getpid();
        strncpy(list_msg.sender_nick, g_nickname, sizeof(list_msg.sender_nick) - 1);
        fifo_send_msg(g_server_fd, &list_msg);

        for (int i = 0; i < 30 && g_running; i++) {
            usleep(100000); /* 100ms * 30 = 3 seconds */
        }
    }
    return NULL;
}

static void execute_command(const char *cmd)
{
    if (strcmp(cmd, "/help") == 0) {
        add_history_msg("Commands: /rooms, /join <room>, /leave, /list, /msg <nick> <text>, /sendfile <nick> <path>, /quit");
        return;
    }

    if (strcmp(cmd, "/rooms") == 0) {
        chat_msg_t rmsg;
        memset(&rmsg, 0, sizeof(rmsg));
        rmsg.msg_type = MSG_ROOM_LIST;
        rmsg.sender_id = g_client_id;
        rmsg.sender_pid = getpid();
        strncpy(rmsg.sender_nick, g_nickname, sizeof(rmsg.sender_nick) - 1);
        fifo_send_msg(g_server_fd, &rmsg);
        return;
    }

    if (strncmp(cmd, "/join ", 6) == 0) {
        const char *rname = cmd + 6;
        while (*rname == ' ') rname++;
        char formatted_room[MAX_ROOM_NAME_LEN];
        if (rname[0] != '#') snprintf(formatted_room, sizeof(formatted_room), "#%s", rname);
        else strncpy(formatted_room, rname, sizeof(formatted_room) - 1);

        chat_msg_t jmsg;
        memset(&jmsg, 0, sizeof(jmsg));
        jmsg.msg_type = MSG_ROOM_JOIN;
        jmsg.sender_id = g_client_id;
        jmsg.sender_pid = getpid();
        strncpy(jmsg.sender_nick, g_nickname, sizeof(jmsg.sender_nick) - 1);
        strncpy(jmsg.room, formatted_room, sizeof(jmsg.room) - 1);
        fifo_send_msg(g_server_fd, &jmsg);
        return;
    }

    if (strcmp(cmd, "/leave") == 0) {
        chat_msg_t lmsg;
        memset(&lmsg, 0, sizeof(lmsg));
        lmsg.msg_type = MSG_ROOM_LEAVE;
        lmsg.sender_id = g_client_id;
        lmsg.sender_pid = getpid();
        strncpy(lmsg.sender_nick, g_nickname, sizeof(lmsg.sender_nick) - 1);
        strncpy(lmsg.room, g_current_room, sizeof(lmsg.room) - 1);
        fifo_send_msg(g_server_fd, &lmsg);
        return;
    }

    if (strcmp(cmd, "/list") == 0) {
        chat_msg_t list_msg;
        memset(&list_msg, 0, sizeof(list_msg));
        list_msg.msg_type = MSG_LIST_USERS;
        list_msg.sender_id = g_client_id;
        list_msg.sender_pid = getpid();
        strncpy(list_msg.sender_nick, g_nickname, sizeof(list_msg.sender_nick) - 1);
        fifo_send_msg(g_server_fd, &list_msg);
        return;
    }

    if (strncmp(cmd, "/history", 8) == 0 && (cmd[8] == '\0' || cmd[8] == ' ')) {
        int count = 10;
        if (strlen(cmd) > 8) {
            count = atoi(cmd + 8);
            if (count <= 0) count = 10;
        }
        chat_msg_t hmsg;
        memset(&hmsg, 0, sizeof(hmsg));
        hmsg.msg_type = MSG_HISTORY_REQ;
        hmsg.sender_id = g_client_id;
        hmsg.sender_pid = getpid();
        strncpy(hmsg.sender_nick, g_nickname, sizeof(hmsg.sender_nick) - 1);
        strncpy(hmsg.room, g_current_room, sizeof(hmsg.room) - 1);
        hmsg.target_id = count;
        fifo_send_msg(g_server_fd, &hmsg);
        return;
    }

    if (strncmp(cmd, "/msg ", 5) == 0) {
        char buf[512];
        strncpy(buf, cmd + 5, sizeof(buf) - 1);
        char *target = buf;
        while (*target == ' ') target++;
        char *space = strchr(target, ' ');
        if (!space) {
            add_history_msg("[Usage] /msg <nickname|client_id> <message>");
            return;
        }
        *space = '\0';
        char *pm_text = space + 1;
        while (*pm_text == ' ') pm_text++;

        chat_msg_t pm_msg;
        memset(&pm_msg, 0, sizeof(pm_msg));
        pm_msg.msg_type = MSG_CHAT_PRIVATE;
        pm_msg.sender_id = g_client_id;
        pm_msg.sender_pid = getpid();
        strncpy(pm_msg.sender_nick, g_nickname, sizeof(pm_msg.sender_nick) - 1);

        char *endptr;
        long tid = strtol(target, &endptr, 10);
        if (*endptr == '\0' && tid > 0) pm_msg.target_id = (int)tid;
        else {
            pm_msg.target_id = -1;
            strncpy(pm_msg.target_nick, target, sizeof(pm_msg.target_nick) - 1);
        }
        strncpy(pm_msg.content, pm_text, sizeof(pm_msg.content) - 1);
        pm_msg.content_len = strlen(pm_msg.content);

        fifo_send_msg(g_server_fd, &pm_msg);
        char hist_buf[1024];
        snprintf(hist_buf, sizeof(hist_buf), "[PM to %s]: %s", target, pm_text);
        add_history_msg(hist_buf);
        return;
    }

    if (strncmp(cmd, "/sendfile ", 10) == 0) {
        char buf[512];
        strncpy(buf, cmd + 10, sizeof(buf) - 1);
        char *target = buf;
        while (*target == ' ') target++;
        char *space = strchr(target, ' ');
        if (!space) {
            add_history_msg("[Usage] /sendfile <nickname> <filepath>");
            return;
        }
        *space = '\0';
        char *filepath = space + 1;
        while (*filepath == ' ') filepath++;

        struct stat st;
        if (stat(filepath, &st) < 0 || S_ISDIR(st.st_mode)) {
            char err_buf[256];
            snprintf(err_buf, sizeof(err_buf), "[File] Error: '%s' not found or is a directory.", filepath);
            add_history_msg(err_buf);
            return;
        }

        size_t fsize = (size_t)st.st_size;
        int shmid = shmget(IPC_PRIVATE, fsize > 0 ? fsize : 1, IPC_CREAT | 0666);
        if (shmid < 0) {
            add_history_msg("[File] Error: shmget failed.");
            return;
        }

        void *shm_ptr = shmat(shmid, NULL, 0);
        if (shm_ptr == (void *)-1) {
            shmctl(shmid, IPC_RMID, NULL);
            add_history_msg("[File] Error: shmat failed.");
            return;
        }

        FILE *fp = fopen(filepath, "rb");
        if (!fp || (fsize > 0 && fread(shm_ptr, 1, fsize, fp) != fsize)) {
            if (fp) fclose(fp);
            shmdt(shm_ptr);
            shmctl(shmid, IPC_RMID, NULL);
            add_history_msg("[File] Error reading file.");
            return;
        }
        fclose(fp);
        shmdt(shm_ptr);

        chat_msg_t fmsg;
        memset(&fmsg, 0, sizeof(fmsg));
        fmsg.msg_type = MSG_FILE_OFFER;
        fmsg.sender_id = g_client_id;
        fmsg.sender_pid = getpid();
        strncpy(fmsg.sender_nick, g_nickname, sizeof(fmsg.sender_nick) - 1);
        strncpy(fmsg.target_nick, target, sizeof(fmsg.target_nick) - 1);

        char path_copy[256];
        strncpy(path_copy, filepath, sizeof(path_copy) - 1);
        char *bname = basename(path_copy);
        strncpy(fmsg.file_name, bname, sizeof(fmsg.file_name) - 1);
        fmsg.file_size = fsize;
        fmsg.file_shmid = shmid;
        snprintf(fmsg.content, sizeof(fmsg.content), "Zero-Copy File Offer: %s", bname);

        fifo_send_msg(g_server_fd, &fmsg);
        g_last_send_shmid = shmid;
        char hist_buf[1024];
        snprintf(hist_buf, sizeof(hist_buf), "[File] Offered '%s' (%zu bytes) to %s via SHM (shmid: %d)",
                 bname, fsize, target, shmid);
        add_history_msg(hist_buf);
        return;
    }

    if (strcmp(cmd, "/quit") == 0) {
        chat_msg_t disc_msg;
        memset(&disc_msg, 0, sizeof(disc_msg));
        disc_msg.msg_type = MSG_DISCONNECT;
        disc_msg.sender_id = g_client_id;
        disc_msg.sender_pid = getpid();
        strncpy(disc_msg.sender_nick, g_nickname, sizeof(disc_msg.sender_nick) - 1);
        fifo_send_msg(g_server_fd, &disc_msg);
        g_running = 0;
        return;
    }

    /* Regular chat message */
    chat_msg_t chat_msg;
    memset(&chat_msg, 0, sizeof(chat_msg));
    chat_msg.msg_type = MSG_CHAT_ALL;
    chat_msg.sender_id = g_client_id;
    chat_msg.sender_pid = getpid();
    strncpy(chat_msg.sender_nick, g_nickname, sizeof(chat_msg.sender_nick) - 1);
    strncpy(chat_msg.room, g_current_room, sizeof(chat_msg.room) - 1);
    strncpy(chat_msg.content, cmd, sizeof(chat_msg.content) - 1);
    chat_msg.content_len = strlen(chat_msg.content);
    fifo_send_msg(g_server_fd, &chat_msg);

    char hist_buf[512];
    snprintf(hist_buf, sizeof(hist_buf), "<%s>: %s", g_nickname, cmd);
    add_history_msg(hist_buf);
}

int main(int argc, char *argv[])
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    if (argc >= 2) {
        strncpy(g_nickname, argv[1], sizeof(g_nickname) - 1);
    } else {
        snprintf(g_nickname, sizeof(g_nickname), "User_%d", (int)getpid());
    }

    snprintf(g_client_fifo, sizeof(g_client_fifo), CLIENT_FIFO_TEMPLATE, (int)getpid());
    if (fifo_create(g_client_fifo) < 0) {
        fprintf(stderr, "Failed to create client response FIFO %s\n", g_client_fifo);
        return 1;
    }

    g_client_read_fd = fifo_open_rdwr(g_client_fifo);
    if (g_client_read_fd < 0) {
        fprintf(stderr, "Failed to open client response FIFO\n");
        cleanup();
        return 1;
    }

    g_server_fd = fifo_open_write(SERVER_FIFO_PATH, 1);
    if (g_server_fd < 0) {
        fprintf(stderr, "Cannot connect to server. Is the server running? (%s)\n", SERVER_FIFO_PATH);
        cleanup();
        return 1;
    }

    /* Register */
    chat_msg_t reg_req;
    memset(&reg_req, 0, sizeof(reg_req));
    reg_req.msg_type = MSG_REGISTER;
    reg_req.sender_pid = getpid();
    strncpy(reg_req.sender_nick, g_nickname, sizeof(reg_req.sender_nick) - 1);
    strncpy(reg_req.client_fifo, g_client_fifo, sizeof(reg_req.client_fifo) - 1);
    fifo_send_msg(g_server_fd, &reg_req);

    chat_msg_t reg_resp;
    if (fifo_recv_msg(g_client_read_fd, &reg_resp) <= 0 || reg_resp.status_code != STATUS_SUCCESS) {
        fprintf(stderr, "Registration rejected by server: %s\n", reg_resp.content);
        cleanup();
        return 1;
    }

    g_client_id = reg_resp.sender_id;

    /* Start listener & poller threads */
    pthread_t recv_tid, poller_tid;
    pthread_create(&recv_tid, NULL, receiver_thread, NULL);
    pthread_create(&poller_tid, NULL, sidebar_poller_thread, NULL);

    add_history_msg("Connected to server successfully. Welcome to IPC Chat TUI!");

    enable_raw_mode();
    draw_ui();

    /* Event loop for keyboard input */
    while (g_running) {
        char c;
        int nread = read(STDIN_FILENO, &c, 1);
        if (nread <= 0) {
            if (nread < 0 && errno == EINTR) continue;
            break;
        }

        if (c == 3) { /* Ctrl+C */
            break;
        } else if (c == '\r' || c == '\n') {
            if (strlen(g_input_line) > 0) {
                execute_command(g_input_line);
                g_input_line[0] = '\0';
                g_input_pos = 0;
            }
            draw_ui();
        } else if (c == 127 || c == 8) { /* Backspace */
            if (g_input_pos > 0) {
                g_input_pos--;
                g_input_line[g_input_pos] = '\0';
                draw_ui();
            }
        } else if (c == 27) { /* Escape sequence */
            char seq[3];
            if (read(STDIN_FILENO, &seq[0], 1) > 0 && read(STDIN_FILENO, &seq[1], 1) > 0) {
                /* Arrow keys or other escape sequences ignored or handled */
            }
        } else if (c >= 32 && c <= 126) {
            if (g_input_pos < INPUT_MAX_LEN - 1) {
                g_input_line[g_input_pos++] = c;
                g_input_line[g_input_pos] = '\0';
                draw_ui();
            }
        }
    }

    g_running = 0;
    pthread_cancel(recv_tid);
    pthread_cancel(poller_tid);
    pthread_join(recv_tid, NULL);
    pthread_join(poller_tid, NULL);

    cleanup();
    printf("Goodbye!\n");
    return 0;
}
