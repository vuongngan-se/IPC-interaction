#include "msg_queue.h"
#include "logger.h"
#include <errno.h>
#include <string.h>

int msg_queue_get_or_create(key_t key)
{
    int msqid = msgget(key, IPC_CREAT | 0666);
    if (msqid < 0) {
        LOG_ERROR("msgget failed: %s", strerror(errno));
        return -1;
    }
    return msqid;
}

int msg_queue_send(int msqid, const worker_msg_t *msg, int nowait)
{
    if (msqid < 0 || msg == NULL) return -1;
    int flags = nowait ? IPC_NOWAIT : 0;
    size_t sz = sizeof(worker_msg_t) - sizeof(long);
    if (msgsnd(msqid, msg, sz, flags) < 0) {
        LOG_ERROR("msgsnd failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

int msg_queue_recv(int msqid, worker_msg_t *msg, long mtype, int nowait)
{
    if (msqid < 0 || msg == NULL) return -1;
    int flags = nowait ? IPC_NOWAIT : 0;
    size_t sz = sizeof(worker_msg_t) - sizeof(long);
    ssize_t res = msgrcv(msqid, msg, sz, mtype, flags);
    if (res < 0) {
        if (errno == ENOMSG || errno == EINTR) return 0;
        LOG_ERROR("msgrcv failed: %s", strerror(errno));
        return -1;
    }
    return 1;
}

int msg_queue_destroy(int msqid)
{
    if (msqid >= 0) {
        return msgctl(msqid, IPC_RMID, NULL);
    }
    return 0;
}
