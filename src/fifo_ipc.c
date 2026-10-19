#include "fifo_ipc.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>

int fifo_create(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISFIFO(st.st_mode)) {
            /* FIFO already exists, reuse it */
            return 0;
        }
        /* Path exists but is not a FIFO, remove it */
        unlink(path);
    }

    if (mkfifo(path, 0666) < 0) {
        if (errno != EEXIST) {
            LOG_ERROR("mkfifo failed for %s: %s", path, strerror(errno));
            return -1;
        }
    }
    return 0;
}

int fifo_open_read(const char *path, int nonblock)
{
    int flags = O_RDONLY;
    if (nonblock) {
        flags |= O_NONBLOCK;
    }

    int fd = open(path, flags);
    if (fd < 0) {
        if (errno != ENXIO && errno != ENOENT) {
            LOG_ERROR("open for reading failed for %s: %s", path, strerror(errno));
        }
        return -1;
    }
    return fd;
}

int fifo_open_write(const char *path, int nonblock)
{
    int flags = O_WRONLY;
    if (nonblock) {
        flags |= O_NONBLOCK;
    }

    int fd = open(path, flags);
    if (fd < 0) {
        if (errno != ENXIO && errno != ENOENT) {
            LOG_ERROR("open for writing failed for %s: %s", path, strerror(errno));
        }
        return -1;
    }
    return fd;
}

int fifo_open_rdwr(const char *path)
{
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        if (errno != ENXIO && errno != ENOENT) {
            LOG_ERROR("open O_RDWR failed for %s: %s", path, strerror(errno));
        }
        return -1;
    }
    return fd;
}

int fifo_send_msg(int fd, const chat_msg_t *msg)
{
    if (fd < 0 || msg == NULL) {
        return -1;
    }

    size_t total_to_write = sizeof(chat_msg_t);
    const char *ptr = (const char *)msg;
    size_t written = 0;

    /*
     * Because sizeof(chat_msg_t) <= PIPE_BUF, a single write call
     * is guaranteed to be atomic by POSIX standards.
     */
    while (written < total_to_write) {
        ssize_t res = write(fd, ptr + written, total_to_write - written);
        if (res < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EPIPE) {
                LOG_WARN("Broken pipe writing to FIFO (fd=%d)", fd);
                return -2;
            }
            LOG_ERROR("write to FIFO failed (fd=%d): %s", fd, strerror(errno));
            return -1;
        }
        written += (size_t)res;
    }

    return (int)written;
}

int fifo_recv_msg(int fd, chat_msg_t *msg)
{
    if (fd < 0 || msg == NULL) {
        return -1;
    }

    size_t total_to_read = sizeof(chat_msg_t);
    char *ptr = (char *)msg;
    size_t read_bytes = 0;

    while (read_bytes < total_to_read) {
        ssize_t res = read(fd, ptr + read_bytes, total_to_read - read_bytes);
        if (res < 0) {
            if (errno == EINTR) {
                return -1;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0; /* Non-blocking: no data yet */
            }
            LOG_ERROR("read from FIFO failed (fd=%d): %s", fd, strerror(errno));
            return -1;
        }
        if (res == 0) {
            /* EOF reached */
            if (read_bytes == 0) {
                return 0;
            }
            /* Partial read before EOF: corrupted packet */
            LOG_WARN("Partial packet received on FIFO (fd=%d, read=%zu/%zu)",
                     fd, read_bytes, total_to_read);
            return -1;
        }
        read_bytes += (size_t)res;
    }

    return 1;
}

void fifo_destroy(const char *path)
{
    if (path != NULL && strlen(path) > 0) {
        unlink(path);
    }
}
