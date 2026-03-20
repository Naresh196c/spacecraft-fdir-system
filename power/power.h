#ifndef POWER_H
#define POWER_H

#include <pthread.h>
#include "../common.h"
#include "../ipc/ipc.h"

/* ─────────────────────────────────────────
   POWER DATA STRUCT
   Shared between all 3 power threads.
   Protected by power_mutex.
───────────────────────────────────────── */
typedef struct {
    float   solar_watts;        /* current solar panel output W  */
    float   battery_pct;        /* battery percentage 0-100      */
    int     in_eclipse;         /* 1 = no sunlight               */
    int     fault_active;       /* 1 = fault has been reported   */
} power_data_t;

/* ─────────────────────────────────────────
   SHARED RESOURCES
   Declared in power.c
   Used by power_threads.c
───────────────────────────────────────── */
extern power_data_t     g_power;        /* shared power data         */
extern pthread_mutex_t  g_power_mutex;  /* protects g_power          */
extern telemetry_bus_t *g_bus;          /* shared memory bus         */
extern sem_t           *g_sem;          /* semaphore for shared mem  */
extern mqd_t            g_mq;          /* message queue             */

/* ─────────────────────────────────────────
   THREAD FUNCTIONS
   Defined in power_threads.c
───────────────────────────────────────── */
void *solar_thread(void *arg);      /* generates solar watts     */
void *battery_thread(void *arg);    /* drains or charges battery */
void *bus_writer_thread(void *arg); /* writes data to shared mem */

/* ─────────────────────────────────────────
   HELPER FUNCTIONS
───────────────────────────────────────── */
void power_send_fault(const char *severity,
                      float value,
                      float threshold,
                      const char *desc);

#endif /* POWER_H */
