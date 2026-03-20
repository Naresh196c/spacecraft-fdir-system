#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include "power.h"
#include "../signals/signal_handler.h"

/* ─────────────────────────────────────────
   GLOBAL VARIABLES
   Shared between power.c and power_threads.c
───────────────────────────────────────── */
power_data_t    g_power;
pthread_mutex_t g_power_mutex = PTHREAD_MUTEX_INITIALIZER;
telemetry_bus_t *g_bus = NULL;
sem_t           *g_sem = NULL;
mqd_t            g_mq;

/* ─────────────────────────────────────────
   HELPER: SEND FAULT TO MESSAGE QUEUE
   Called by power threads when a threshold
   is crossed.
───────────────────────────────────────── */
void power_send_fault(const char *severity,
                      float value,
                      float threshold,
                      const char *desc) {
    fault_report_t report;
    memset(&report, 0, sizeof(report));

    strncpy(report.subsystem,  "POWER",    sizeof(report.subsystem) - 1);
    strncpy(report.severity,   severity,   sizeof(report.severity)  - 1);
    strncpy(report.description, desc,      sizeof(report.description) - 1);
    report.value     = value;
    report.threshold = threshold;
    get_timestamp(report.timestamp, sizeof(report.timestamp));

    /* Choose priority based on severity */
    int priority = MQ_PRIORITY_INFO;
    if (strcmp(severity, "CRITICAL") == 0) priority = MQ_PRIORITY_CRITICAL;
    else if (strcmp(severity, "WARNING") == 0) priority = MQ_PRIORITY_WARNING;

    msgq_send(g_mq, &report, priority);
}

/* ─────────────────────────────────────────
   MAIN — POWER PROCESS ENTRY POINT
───────────────────────────────────────── */
int main(void) {
    printf(COLOR_CYAN
           "[POWER] Process started (PID: %d)\n"
           COLOR_RESET, getpid());

    /* Step 1: Install child signal handlers */
    setup_child_signals();

    /* Step 2: Attach to shared memory */
    g_bus = shm_attach();
    if (g_bus == NULL) {
        fprintf(stderr, "[POWER] Failed to attach shared memory\n");
        exit(EXIT_FAILURE);
    }

    /* Step 3: Open semaphore */
    g_sem = sem_open_existing();
    if (g_sem == NULL) {
        fprintf(stderr, "[POWER] Failed to open semaphore\n");
        exit(EXIT_FAILURE);
    }

    /* Step 4: Open message queue */
    g_mq = msgq_open_existing();
    if (g_mq == (mqd_t)-1) {
        fprintf(stderr, "[POWER] Failed to open message queue\n");
        exit(EXIT_FAILURE);
    }

    /* Step 5: Initialize power data */
    pthread_mutex_lock(&g_power_mutex);
    g_power.solar_watts  = 110.0f;
    g_power.battery_pct  = 100.0f;
    g_power.in_eclipse   = 0;
    g_power.fault_active = 0;
    pthread_mutex_unlock(&g_power_mutex);

    /* Step 6: Start the 3 threads */
    pthread_t tid_solar, tid_battery, tid_bus;

    if (pthread_create(&tid_solar, NULL, solar_thread, NULL) != 0) {
        perror("[POWER] pthread_create solar_thread failed");
        exit(EXIT_FAILURE);
    }

    if (pthread_create(&tid_battery, NULL, battery_thread, NULL) != 0) {
        perror("[POWER] pthread_create battery_thread failed");
        exit(EXIT_FAILURE);
    }

    if (pthread_create(&tid_bus, NULL, bus_writer_thread, NULL) != 0) {
        perror("[POWER] pthread_create bus_writer_thread failed");
        exit(EXIT_FAILURE);
    }

    /* Step 7: Wait until shutdown signal received */
    while (!g_shutdown) {
        sleep(1);
    }

    /* Step 8: Clean shutdown — join all threads */
    printf("[POWER] Shutting down...\n");
    pthread_join(tid_solar,   NULL);
    pthread_join(tid_battery, NULL);
    pthread_join(tid_bus,     NULL);

    /* Step 9: Release IPC resources */
    shm_detach(g_bus);
    sem_close_handle(g_sem);
    msgq_close(g_mq);
    pthread_mutex_destroy(&g_power_mutex);

    printf("[POWER] Clean exit.\n");
    return 0;
}
