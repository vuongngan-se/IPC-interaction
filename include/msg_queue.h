#ifndef MSG_QUEUE_H
#define MSG_QUEUE_H

#include "protocol.h"
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

/* Create or open a System V Message Queue */
int msg_queue_get_or_create(key_t key);

/* Send a worker_msg_t to the message queue */
int msg_queue_send(int msqid, const worker_msg_t *msg, int nowait);

/* Receive a worker_msg_t matching mtype from the message queue */
int msg_queue_recv(int msqid, worker_msg_t *msg, long mtype, int nowait);

/* Remove the message queue from the system */
int msg_queue_destroy(int msqid);

#endif /* MSG_QUEUE_H */
