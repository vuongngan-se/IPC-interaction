#include "shared_memory.h"
#include "logger.h"
#include <errno.h>
#include <string.h>

shared_state_t *shm_create_and_attach(key_t key, int *shmid_out)
{
    int shmid = shmget(key, sizeof(shared_state_t), IPC_CREAT | IPC_EXCL | 0666);
    if (shmid < 0) {
        if (errno == EEXIST) {
            /* If already exists, open existing and reinitialize or reuse */
            shmid = shmget(key, sizeof(shared_state_t), 0666);
        }
        if (shmid < 0) {
            LOG_ERROR("shmget failed: %s", strerror(errno));
            return NULL;
        }
    }

    shared_state_t *ptr = (shared_state_t *)shmat(shmid, NULL, 0);
    if (ptr == (void *)-1) {
        LOG_ERROR("shmat failed: %s", strerror(errno));
        return NULL;
    }

    if (shmid_out != NULL) {
        *shmid_out = shmid;
    }

    return ptr;
}

shared_state_t *shm_attach_existing(key_t key, int *shmid_out)
{
    int shmid = shmget(key, sizeof(shared_state_t), 0666);
    if (shmid < 0) {
        LOG_ERROR("shmget existing failed: %s", strerror(errno));
        return NULL;
    }

    shared_state_t *ptr = (shared_state_t *)shmat(shmid, NULL, 0);
    if (ptr == (void *)-1) {
        LOG_ERROR("shmat failed: %s", strerror(errno));
        return NULL;
    }

    if (shmid_out != NULL) {
        *shmid_out = shmid;
    }

    return ptr;
}

int shm_detach(shared_state_t *shm_ptr)
{
    if (shm_ptr != NULL) {
        return shmdt(shm_ptr);
    }
    return 0;
}

int shm_destroy(int shmid)
{
    if (shmid >= 0) {
        return shmctl(shmid, IPC_RMID, NULL);
    }
    return 0;
}
