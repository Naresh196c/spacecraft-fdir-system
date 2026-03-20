#ifndef THERMAL_H
#define THERMAL_H

#include <pthread.h>
#include "../common.h"
#include "../ipc/ipc.h"

/* ─────────────────────────────────────────
   THERMAL DATA STRUCT
   Shared between all 3 thermal threads.
   Protected by thermal_mutex.
───────────────────────────────────────── */
typedef struct {
    float   temp_cpu;           /* CPU board temperature °C      */
    float   temp_battery;       /* Battery pack temperature °C   */
    float   temp_antenna;       /* Antenna temperature °C        */
    float   temp_body;          /* Outer body temperature °C     */
    int     heater_on;          /* 1 = heater active             */
    int     fault_active;       /* 1 = fault already reported    */
} thermal_data_t;

/* ─────────────────────────────────────────
   SHARED RESOURCES
   Declared in thermal.c
   Used by thermal_threads.c
───────────────────────────────────────── */
extern thermal_data_t   g_thermal;
extern pthread_mutex_t  g_thermal_mutex;
extern telemetry_bus_t *g_bus;
extern sem_t           *g_sem;
extern mqd_t            g_mq;

/* ─────────────────────────────────────────
   THREAD FUNCTIONS
   Defined in thermal_threads.c
───────────────────────────────────────── */
void *sensor_thread(void *arg);       /* reads 4 zone temperatures */
void *heater_thread(void *arg);       /* turns heater on/off       */
void *bus_writer_thread(void *arg);   /* writes to shared memory   */

/* ─────────────────────────────────────────
   HELPER FUNCTIONS
───────────────────────────────────────── */
void thermal_send_fault(const char *severity,
                        float value,
                        float threshold,
                        const char *desc);

#endif /* THERMAL_H */
