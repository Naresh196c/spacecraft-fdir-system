#ifndef COMMS_H
#define COMMS_H

#include <pthread.h>
#include "../common.h"
#include "../ipc/ipc.h"

/* ─────────────────────────────────────────
   COMMS DATA STRUCT
   Shared between both comms threads.
   Protected by comms_mutex.
───────────────────────────────────────── */
typedef struct {
    float   signal_quality;     /* link quality 0-100%           */
    int     uplink_active;      /* 1 = receiving commands        */
    int     downlink_active;    /* 1 = sending telemetry         */
    int     fault_active;       /* 1 = fault already reported    */
    char    last_command[CMD_MAX_LEN]; /* last cmd received       */
} comms_data_t;

/* ─────────────────────────────────────────
   SHARED RESOURCES
   Declared in comms.c
   Used by comms_threads.c
───────────────────────────────────────── */
extern comms_data_t     g_comms;
extern pthread_mutex_t  g_comms_mutex;
extern telemetry_bus_t *g_bus;
extern sem_t           *g_sem;
extern mqd_t            g_mq;
extern int              g_fifo_fd;    /* FIFO file descriptor      */

/* ─────────────────────────────────────────
   THREAD FUNCTIONS
   Defined in comms_threads.c
───────────────────────────────────────── */
void *uplink_thread(void *arg);     /* reads commands from FIFO  */
void *downlink_thread(void *arg);   /* sends telemetry to MQ     */

/* ─────────────────────────────────────────
   HELPER FUNCTIONS
───────────────────────────────────────── */
void comms_send_fault(const char *severity,
                      float value,
                      float threshold,
                      const char *desc);

void comms_handle_command(const char *cmd);

#endif /* COMMS_H */
