#include "semaphore.h"
#include "logger.h"
#include <errno.h>
#include <string.h>

int sem_create_and_init(key_t key)
{
    int semid = semget(key, 1, IPC_CREAT | IPC_EXCL | 0666);
    if (semid < 0) {
        if (errno == EEXIST) {
            semid = semget(key, 1, 0666);
            if (semid < 0) {
                LOG_ERROR("semget existing failed: %s", strerror(errno));
                return -1;
            }
            return semid;
        }
        LOG_ERROR("semget failed: %s", strerror(errno));
        return -1;
    }

    /* Initialize binary semaphore to 1 */
    union semun arg;
    arg.val = 1;
    if (semctl(semid, 0, SETVAL, arg) < 0) {
        LOG_ERROR("semctl SETVAL failed: %s", strerror(errno));
        return -1;
    }

    return semid;
}

int sem_get_existing(key_t key)
{
    int semid = semget(key, 1, 0666);
    if (semid < 0) {
        LOG_ERROR("semget failed: %s", strerror(errno));
        return -1;
    }
    return semid;
}

int sem_lock(int semid)
{
    struct sembuf sb;
    sb.sem_num = 0;
    sb.sem_op  = -1;       /* P operation: decrement */
    sb.sem_flg = SEM_UNDO; /* SEM_UNDO prevents deadlocks if process crashes */

    while (semop(semid, &sb, 1) < 0) {
        if (errno == EINTR) {
            continue;      /* Interrupted by signal, retry */
        }
        LOG_ERROR("sem_lock (P) failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

int sem_unlock(int semid)
{
    struct sembuf sb;
    sb.sem_num = 0;
    sb.sem_op  = 1;        /* V operation: increment */
    sb.sem_flg = SEM_UNDO;

    while (semop(semid, &sb, 1) < 0) {
        if (errno == EINTR) {
            continue;
        }
        LOG_ERROR("sem_unlock (V) failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

int sem_destroy(int semid)
{
    if (semid >= 0) {
        return semctl(semid, 0, IPC_RMID);
    }
    return 0;
}
