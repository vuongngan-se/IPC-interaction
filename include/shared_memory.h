#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include "protocol.h"
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

/* Create and attach a new System V Shared Memory segment for shared_state_t */
shared_state_t *shm_create_and_attach(key_t key, int *shmid_out);

/* Attach to an existing Shared Memory segment */
shared_state_t *shm_attach_existing(key_t key, int *shmid_out);

/* Detach the Shared Memory segment pointer */
int shm_detach(shared_state_t *shm_ptr);

/* Mark the Shared Memory segment for deletion */
int shm_destroy(int shmid);

#endif /* SHARED_MEMORY_H */
