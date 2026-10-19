#ifndef WORKER_H
#define WORKER_H

#include "protocol.h"

/* Worker process entry point: consumes tasks from Message Queue */
void worker_run(int worker_id, int msqid, int semid, shared_state_t *shm_state);

#endif /* WORKER_H */
