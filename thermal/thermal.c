#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include "thermal.h"
#include "../signals/signal_handler.h"

/* ─────────────────────────────────────────
   GLOBAL VARIABLES
   Shared between thermal.c and thermal_threads.c
───────────────────────────────────────── */
thermal_data_t  g_thermal;
pthread_mutex_t g_thermal_mutex = PTHREAD_MUTEX_INITIALIZER;
telemetry_bus_t *g_bus = NULL;
sem_t           *g_sem = NULL;
mqd_t            g_mq;

/* ─────────────────────────────────────────
   HELPER: SEND FAULT TO MESSAGE QUEUE
───────────────────────────────────────── */
void thermal_send_fault(const char *severity,
                        float value,
                        float threshold,
                        const char *desc) {
    fault_report_t report;
    memset(&report, 0, sizeof(report));

    strncpy(report.subsystem,   "THERMAL",  sizeof(report.subsystem)   - 1);
    strncpy(report.severity,    severity,   sizeof(report.severity)    - 1);
    strncpy(report.description, desc,       sizeof(report.description) - 1);
    report.value     = value;
    report.threshold = threshold;
    get_timestamp(report.timestamp, sizeof(report.timestamp));

    int priority = MQ_PRIORITY_INFO;
    if (strcmp(severity, "CRITICAL") == 0) priority = MQ_PRIORITY_CRITICAL;
    else if (strcmp(severity, "WARNING") == 0) priority = MQ_PRIORITY_WARNING;

    msgq_send(g_mq, &report, priority);
}

/* ─────────────────────────────────────────
   MAIN — THERMAL PROCESS ENTRY POINT
───────────────────────────────────────── */
int main(void) {
    printf(COLOR_CYAN
           "[THERMAL] Process started (PID: %d)\n"
           COLOR_RESET, getpid());

    /* Step 1: Install child signal handlers */
    setup_child_signals();

    /* Step 2: Attach to shared memory */
    g_bus = shm_attach();
    if (g_bus == NULL) {
        fprintf(stderr, "[THERMAL] Failed to attach shared memory\n");
        exit(EXIT_FAILURE);
    }

    /* Step 3: Open semaphore */
    g_sem = sem_open_existing();
    if (g_sem == NULL) {
        fprintf(stderr, "[THERMAL] Failed to open semaphore\n");
        exit(EXIT_FAILURE);
    }

    /* Step 4: Open message queue */
    g_mq = msgq_open_existing();
    if (g_mq == (mqd_t)-1) {
        fprintf(stderr, "[THERMAL] Failed to open message queue\n");
        exit(EXIT_FAILURE);
    }

    /* Step 5: Initialize thermal data */
    pthread_mutex_lock(&g_thermal_mutex);
    g_thermal.temp_cpu      = 30.0f;
    g_thermal.temp_battery  = 20.0f;
    g_thermal.temp_antenna  = 25.0f;
    g_thermal.temp_body     = 22.0f;
    g_thermal.heater_on     = 0;
    g_thermal.fault_active  = 0;
    pthread_mutex_unlock(&g_thermal_mutex);

    /* Step 6: Start the 3 threads */
    pthread_t tid_sensor, tid_heater, tid_bus;

    if (pthread_create(&tid_sensor, NULL, sensor_thread, NULL) != 0) {
        perror("[THERMAL] pthread_create sensor_thread failed");
        exit(EXIT_FAILURE);
    }

    if (pthread_create(&tid_heater, NULL, heater_thread, NULL) != 0) {
        perror("[THERMAL] pthread_create heater_thread failed");
        exit(EXIT_FAILURE);
    }

    if (pthread_create(&tid_bus, NULL, bus_writer_thread, NULL) != 0) {
        perror("[THERMAL] pthread_create bus_writer_thread failed");
        exit(EXIT_FAILURE);
    }

    /* Step 7: Wait until shutdown signal */
    while (!g_shutdown) {
        sleep(1);
    }

    /* Step 8: Join all threads */
    printf("[THERMAL] Shutting down...\n");
    pthread_join(tid_sensor, NULL);
    pthread_join(tid_heater, NULL);
    pthread_join(tid_bus,    NULL);

    /* Step 9: Release IPC resources */
    shm_detach(g_bus);
    sem_close_handle(g_sem);
    msgq_close(g_mq);
    pthread_mutex_destroy(&g_thermal_mutex);

    printf("[THERMAL] Clean exit.\n");
    return 0;
}
