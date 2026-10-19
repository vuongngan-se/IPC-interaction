#include "worker.h"
#include "logger.h"
#include <unistd.h>

void worker_run(int worker_id, int msqid, int semid, shared_state_t *shm_state)
{
    (void)worker_id;
    (void)msqid;
    (void)semid;
    (void)shm_state;
    LOG_INFO("Worker %d initialized (PID: %d)", worker_id, (int)getpid());
}
