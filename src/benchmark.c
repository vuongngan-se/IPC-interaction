#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <errno.h>

#if defined(__GNU_LIBRARY__) && !defined(_SEM_SEMUN_UNDEFINED)
/* union semun is defined by including <sys/sem.h> */
#else
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
    struct seminfo *__buf;
};
#endif

typedef struct {
    const char *name;
    double total_sec;
    double avg_latency_us;
    double throughput_msg_sec;
    double bandwidth_mb_sec;
} bench_result_t;

static double get_time_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* ========================================================
 * 1. Shared Memory + Semaphore Benchmark
 * ======================================================== */
static bench_result_t bench_shm(int rounds, int payload_size)
{
    bench_result_t res = {"SysV Shared Memory + Sem", 0, 0, 0, 0};

    int shmid = shmget(IPC_PRIVATE, payload_size > 0 ? payload_size : 64, IPC_CREAT | 0666);
    int semid = semget(IPC_PRIVATE, 2, IPC_CREAT | 0666);

    union semun arg;
    arg.val = 0;
    semctl(semid, 0, SETVAL, arg);
    semctl(semid, 1, SETVAL, arg);

    struct sembuf p0 = {0, -1, 0}, v0 = {0, 1, 0};
    struct sembuf p1 = {1, -1, 0}, v1 = {1, 1, 0};

    pid_t pid = fork();
    if (pid == 0) {
        char *ptr = (char *)shmat(shmid, NULL, 0);
        for (int i = 0; i < rounds; i++) {
            semop(semid, &p0, 1);
            ptr[0] = (char)(ptr[0] + 1);
            semop(semid, &v1, 1);
        }
        shmdt(ptr);
        exit(0);
    }

    char *ptr = (char *)shmat(shmid, NULL, 0);
    memset(ptr, 'A', payload_size);

    double t0 = get_time_sec();
    for (int i = 0; i < rounds; i++) {
        semop(semid, &v0, 1);
        semop(semid, &p1, 1);
    }
    double t1 = get_time_sec();

    waitpid(pid, NULL, 0);
    shmdt(ptr);
    shmctl(shmid, IPC_RMID, NULL);
    semctl(semid, 0, IPC_RMID);

    res.total_sec = t1 - t0;
    res.avg_latency_us = (res.total_sec / rounds) * 1e6;
    res.throughput_msg_sec = rounds / res.total_sec;
    res.bandwidth_mb_sec = (res.throughput_msg_sec * payload_size * 2) / (1024.0 * 1024.0);
    return res;
}

/* ========================================================
 * 2. System V Message Queue Benchmark
 * ======================================================== */
typedef struct {
    long mtype;
    char mtext[4096];
} bench_msg_t;

static bench_result_t bench_msgq(int rounds, int payload_size)
{
    bench_result_t res = {"SysV Message Queue", 0, 0, 0, 0};
    if (payload_size > 4000) payload_size = 4000;

    int msqid = msgget(IPC_PRIVATE, IPC_CREAT | 0666);

    pid_t pid = fork();
    if (pid == 0) {
        bench_msg_t msg;
        for (int i = 0; i < rounds; i++) {
            msgrcv(msqid, &msg, payload_size, 1, 0);
            msg.mtype = 2;
            msgsnd(msqid, &msg, payload_size, 0);
        }
        exit(0);
    }

    bench_msg_t msg;
    msg.mtype = 1;
    memset(msg.mtext, 'B', payload_size);

    double t0 = get_time_sec();
    for (int i = 0; i < rounds; i++) {
        msg.mtype = 1;
        msgsnd(msqid, &msg, payload_size, 0);
        msgrcv(msqid, &msg, payload_size, 2, 0);
    }
    double t1 = get_time_sec();

    waitpid(pid, NULL, 0);
    msgctl(msqid, IPC_RMID, NULL);

    res.total_sec = t1 - t0;
    res.avg_latency_us = (res.total_sec / rounds) * 1e6;
    res.throughput_msg_sec = rounds / res.total_sec;
    res.bandwidth_mb_sec = (res.throughput_msg_sec * payload_size * 2) / (1024.0 * 1024.0);
    return res;
}

/* ========================================================
 * 3. POSIX FIFO (Named Pipe) Benchmark
 * ======================================================== */
static bench_result_t bench_fifo(int rounds, int payload_size)
{
    bench_result_t res = {"POSIX FIFO (Named Pipe)", 0, 0, 0, 0};
    const char *f1 = "/tmp/bench_f1.fifo";
    const char *f2 = "/tmp/bench_f2.fifo";

    unlink(f1);
    unlink(f2);
    mkfifo(f1, 0666);
    mkfifo(f2, 0666);

    pid_t pid = fork();
    if (pid == 0) {
        int rfd = open(f1, O_RDONLY);
        int wfd = open(f2, O_WRONLY);
        char *buf = malloc(payload_size);
        for (int i = 0; i < rounds; i++) {
            int n = 0;
            while (n < payload_size) {
                int r = read(rfd, buf + n, payload_size - n);
                if (r <= 0) break;
                n += r;
            }
            int w = 0;
            while (w < payload_size) {
                int s = write(wfd, buf + w, payload_size - w);
                if (s <= 0) break;
                w += s;
            }
        }
        free(buf);
        close(rfd);
        close(wfd);
        exit(0);
    }

    int wfd = open(f1, O_WRONLY);
    int rfd = open(f2, O_RDONLY);
    char *buf = malloc(payload_size);
    memset(buf, 'C', payload_size);

    double t0 = get_time_sec();
    for (int i = 0; i < rounds; i++) {
        int w = 0;
        while (w < payload_size) {
            int s = write(wfd, buf + w, payload_size - w);
            if (s <= 0) break;
            w += s;
        }
        int n = 0;
        while (n < payload_size) {
            int r = read(rfd, buf + n, payload_size - n);
            if (r <= 0) break;
            n += r;
        }
    }
    double t1 = get_time_sec();

    waitpid(pid, NULL, 0);
    free(buf);
    close(wfd);
    close(rfd);
    unlink(f1);
    unlink(f2);

    res.total_sec = t1 - t0;
    res.avg_latency_us = (res.total_sec / rounds) * 1e6;
    res.throughput_msg_sec = rounds / res.total_sec;
    res.bandwidth_mb_sec = (res.throughput_msg_sec * payload_size * 2) / (1024.0 * 1024.0);
    return res;
}

/* ========================================================
 * 4. Unix Domain Sockets Benchmark (AF_UNIX)
 * ======================================================== */
static bench_result_t bench_unix_socket(int rounds, int payload_size)
{
    bench_result_t res = {"Unix Domain Socket (AF_UNIX)", 0, 0, 0, 0};
    int sv[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);

    pid_t pid = fork();
    if (pid == 0) {
        close(sv[0]);
        char *buf = malloc(payload_size);
        for (int i = 0; i < rounds; i++) {
            int n = 0;
            while (n < payload_size) {
                int r = read(sv[1], buf + n, payload_size - n);
                if (r <= 0) break;
                n += r;
            }
            int w = 0;
            while (w < payload_size) {
                int s = write(sv[1], buf + w, payload_size - w);
                if (s <= 0) break;
                w += s;
            }
        }
        free(buf);
        close(sv[1]);
        exit(0);
    }

    close(sv[1]);
    char *buf = malloc(payload_size);
    memset(buf, 'D', payload_size);

    double t0 = get_time_sec();
    for (int i = 0; i < rounds; i++) {
        int w = 0;
        while (w < payload_size) {
            int s = write(sv[0], buf + w, payload_size - w);
            if (s <= 0) break;
            w += s;
        }
        int n = 0;
        while (n < payload_size) {
            int r = read(sv[0], buf + n, payload_size - n);
            if (r <= 0) break;
            n += r;
        }
    }
    double t1 = get_time_sec();

    waitpid(pid, NULL, 0);
    free(buf);
    close(sv[0]);

    res.total_sec = t1 - t0;
    res.avg_latency_us = (res.total_sec / rounds) * 1e6;
    res.throughput_msg_sec = rounds / res.total_sec;
    res.bandwidth_mb_sec = (res.throughput_msg_sec * payload_size * 2) / (1024.0 * 1024.0);
    return res;
}

/* ========================================================
 * 5. TCP Socket Benchmark (AF_INET, Loopback 127.0.0.1)
 * ======================================================== */
static bench_result_t bench_tcp_socket(int rounds, int payload_size)
{
    bench_result_t res = {"TCP Loopback (127.0.0.1)", 0, 0, 0, 0};

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    serv_addr.sin_port = 0; /* Let OS assign port */

    bind(listen_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    listen(listen_fd, 1);

    socklen_t len = sizeof(serv_addr);
    getsockname(listen_fd, (struct sockaddr *)&serv_addr, &len);
    int port = ntohs(serv_addr.sin_port);

    pid_t pid = fork();
    if (pid == 0) {
        close(listen_fd);
        int client_fd = socket(AF_INET, SOCK_STREAM, 0);
        int nodelay = 1;
        setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

        struct sockaddr_in saddr;
        memset(&saddr, 0, sizeof(saddr));
        saddr.sin_family = AF_INET;
        saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        saddr.sin_port = htons(port);

        connect(client_fd, (struct sockaddr *)&saddr, sizeof(saddr));

        char *buf = malloc(payload_size);
        for (int i = 0; i < rounds; i++) {
            int n = 0;
            while (n < payload_size) {
                int r = read(client_fd, buf + n, payload_size - n);
                if (r <= 0) break;
                n += r;
            }
            int w = 0;
            while (w < payload_size) {
                int s = write(client_fd, buf + w, payload_size - w);
                if (s <= 0) break;
                w += s;
            }
        }
        free(buf);
        close(client_fd);
        exit(0);
    }

    int conn_fd = accept(listen_fd, NULL, NULL);
    int nodelay = 1;
    setsockopt(conn_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    char *buf = malloc(payload_size);
    memset(buf, 'E', payload_size);

    double t0 = get_time_sec();
    for (int i = 0; i < rounds; i++) {
        int w = 0;
        while (w < payload_size) {
            int s = write(conn_fd, buf + w, payload_size - w);
            if (s <= 0) break;
            w += s;
        }
        int n = 0;
        while (n < payload_size) {
            int r = read(conn_fd, buf + n, payload_size - n);
            if (r <= 0) break;
            n += r;
        }
    }
    double t1 = get_time_sec();

    waitpid(pid, NULL, 0);
    free(buf);
    close(conn_fd);
    close(listen_fd);

    res.total_sec = t1 - t0;
    res.avg_latency_us = (res.total_sec / rounds) * 1e6;
    res.throughput_msg_sec = rounds / res.total_sec;
    res.bandwidth_mb_sec = (res.throughput_msg_sec * payload_size * 2) / (1024.0 * 1024.0);
    return res;
}

int main(int argc, char *argv[])
{
    int rounds = 5000;
    int payload_size = 512;
    int markdown_mode = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--rounds") == 0 && i + 1 < argc) {
            rounds = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            payload_size = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--markdown") == 0) {
            markdown_mode = 1;
        }
    }

    if (rounds <= 0) rounds = 5000;
    if (payload_size <= 0) payload_size = 512;

    bench_result_t results[5];
    results[0] = bench_shm(rounds, payload_size);
    results[1] = bench_msgq(rounds, payload_size);
    results[2] = bench_fifo(rounds, payload_size);
    results[3] = bench_unix_socket(rounds, payload_size);
    results[4] = bench_tcp_socket(rounds, payload_size);

    if (markdown_mode) {
        printf("| IPC / Transport Mechanism | Avg Latency (µs) | Throughput (msgs/sec) | Bandwidth (MB/s) |\n");
        printf("| :--- | :---: | :---: | :---: |\n");
        for (int i = 0; i < 5; i++) {
            printf("| %-27s | %10.2f µs | %15.0f | %12.2f MB/s |\n",
                   results[i].name, results[i].avg_latency_us,
                   results[i].throughput_msg_sec, results[i].bandwidth_mb_sec);
        }
    } else {
        printf("\n========================================================================================\n");
        printf("           LINUX IPC & SOCKET EMPIRICAL PERFORMANCE BENCHMARK SUITE                     \n");
        printf("========================================================================================\n");
        printf(" Configuration: %d Round-Trip Cycles | Message Payload: %d Bytes\n", rounds, payload_size);
        printf("----------------------------------------------------------------------------------------\n");
        printf(" %-28s | %-12s | %-16s | %-12s\n",
               "Mechanism", "Avg Latency", "Throughput", "Bandwidth");
        printf("----------------------------------------------------------------------------------------\n");
        for (int i = 0; i < 5; i++) {
            printf(" %-28s | %8.2f µs   | %10.0f msg/s | %8.2f MB/s\n",
                   results[i].name, results[i].avg_latency_us,
                   results[i].throughput_msg_sec, results[i].bandwidth_mb_sec);
        }
        printf("========================================================================================\n\n");
    }

    return 0;
}
