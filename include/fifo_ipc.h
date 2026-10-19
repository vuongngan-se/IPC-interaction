#ifndef FIFO_IPC_H
#define FIFO_IPC_H

#include "protocol.h"

/* Create a FIFO file with 0666 permissions */
int  fifo_create(const char *path);

/* Open FIFO for reading (nonblock flag: 1 = O_NONBLOCK, 0 = blocking) */
int  fifo_open_read(const char *path, int nonblock);

/* Open FIFO for writing (nonblock flag: 1 = O_NONBLOCK, 0 = blocking) */
int  fifo_open_write(const char *path, int nonblock);

/*
 * Open FIFO with O_RDWR:
 * Crucial Linux IPC pattern: returns immediately without blocking on a peer,
 * and prevents EOF loops when writers disconnect.
 */
int  fifo_open_rdwr(const char *path);

/* Write a fixed-size chat_msg_t struct to FIFO */
int  fifo_send_msg(int fd, const chat_msg_t *msg);

/* Read a fixed-size chat_msg_t struct from FIFO */
int  fifo_recv_msg(int fd, chat_msg_t *msg);

/* Remove FIFO file from filesystem */
void fifo_destroy(const char *path);

#endif /* FIFO_IPC_H */
