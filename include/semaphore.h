#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

/* Union semun required by semctl on Linux */
#if defined(__GNU_LIBRARY__) && !defined(_SEM_SEMUN_UNDEFINED)
/* union semun is defined by including <sys/sem.h> */
#else
union semun {
    int val;                    /* value for SETVAL */
    struct semid_ds *buf;       /* buffer for IPC_STAT, IPC_SET */
    unsigned short  *array;     /* array for GETALL, SETALL */
    struct seminfo  *__buf;     /* buffer for IPC_INFO */
};
#endif

/* Create and initialize a binary semaphore (mutex) to 1 */
int sem_create_and_init(key_t key);

/* Get identifier of an existing semaphore */
int sem_get_existing(key_t key);

/* Lock (P operation / wait) */
int sem_lock(int semid);

/* Unlock (V operation / post) */
int sem_unlock(int semid);

/* Destroy the semaphore set */
int sem_destroy(int semid);

#endif /* SEMAPHORE_H */
