#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include "comms.h"
#include "../signals/signal_handler.h"


comms_data_t    g_comms;
pthread_mutex_t g_comms_mutex = PTHREAD_MUTEX_INITIALIZER;
telemetry_bus_t *g_bus  = NULL;
sem_t           *g_sem  = NULL;
mqd_t            g_mq;
int              g_fifo_fd = -1;


void comms_send_fault(const char *severity,
                      float value,
                      float threshold,
                      const char *desc) {
    fault_report_t report;
    memset(&report, 0, sizeof(report));

    strncpy(report.subsystem,   "COMMS",   sizeof(report.subsystem)   - 1);
    strncpy(report.severity,    severity,  sizeof(report.severity)    - 1);
    strncpy(report.description, desc,      sizeof(report.description) - 1);
    report.value     = value;
    report.threshold = threshold;
    get_timestamp(report.timestamp, sizeof(report.timestamp));

    int priority = MQ_PRIORITY_INFO;
    if (strcmp(severity, "CRITICAL") == 0) priority = MQ_PRIORITY_CRITICAL;
    else if (strcmp(severity, "WARNING") == 0) priority = MQ_PRIORITY_WARNING;

    msgq_send(g_mq, &report, priority);
}

/* ─────────────────────────────────────────
   HELPER: HANDLE GROUND STATION COMMAND
   Called by uplink_thread when a command
   arrives via FIFO from Terminal 2.
───────────────────────────────────────── */
void comms_handle_command(const char *cmd) {
    printf(COLOR_BLUE
           "[COMMS] Ground command received: %s\n"
           COLOR_RESET, cmd);

    if (strcmp(cmd, CMD_STATUS) == 0) {
        /* Read latest telemetry from shared memory and print */
        sem_wait(g_sem);
        printf(COLOR_BLUE
               "[OBC]   ── SPACECRAFT STATUS ─────────────────────\n"
               "[OBC]   POWER   : Battery %.1f%% | Solar %.0fW\n"
               "[OBC]   THERMAL : CPU %.1f°C | Body %.1f°C\n"
               "[OBC]   ADCS    : Spin %.2f deg/s | Mode %d\n"
               "[OBC]   COMMS   : Signal %.1f%%\n"
               "[OBC]   Safe Mode: %s\n"
               "[OBC]   ─────────────────────────────────────────\n"
               COLOR_RESET,
               g_bus->battery_pct,
               g_bus->solar_watts,
               g_bus->temp_cpu,
               g_bus->temp_body,
               g_bus->spin_rate,
               g_bus->adcs_mode,
               g_bus->signal_quality,
               g_bus->safe_mode ? "ACTIVE" : "NORMAL");
        sem_post(g_sem);

    } else if (strcmp(cmd, CMD_SAFE_MODE) == 0) {
        /* Trigger safe mode via signal */
        printf(COLOR_YELLOW
               "[COMMS] Broadcasting SAFE MODE command...\n"
               COLOR_RESET);
        /* Send SIGUSR1 to own process group — parent will handle */
        kill(getppid(), SIGUSR1);

    } else if (strcmp(cmd, CMD_SNAPSHOT) == 0) {
        /* Send snapshot info message to MQ for logger to save */
        comms_send_fault("INFO", 0.0f, 0.0f,
                         "Ground station requested snapshot");
        printf(COLOR_BLUE
               "[COMMS] Snapshot sent to FDR\n"
               COLOR_RESET);

    } else if (strcmp(cmd, CMD_RESET_ADCS) == 0) {
        /* Ask parent to restart ADCS via SIGUSR2 */
        printf(COLOR_YELLOW
               "[COMMS] Requesting ADCS reset...\n"
               COLOR_RESET);
        kill(getppid(), SIGUSR2);

    } else {
        printf(COLOR_YELLOW
               "[COMMS] Unknown command: %s\n"
               COLOR_RESET, cmd);
    }

    /* Store last command */
    pthread_mutex_lock(&g_comms_mutex);
    strncpy(g_comms.last_command, cmd,
            sizeof(g_comms.last_command) - 1);
    pthread_mutex_unlock(&g_comms_mutex);
}

/* ─────────────────────────────────────────
   MAIN — COMMS PROCESS ENTRY POINT
───────────────────────────────────────── */
int main(void) {
    printf(COLOR_CYAN
           "[COMMS] Process started (PID: %d)\n"
           COLOR_RESET, getpid());

    /* Step 1: Install child signal handlers */
    setup_child_signals();

    /* Step 2: Attach to shared memory */
    g_bus = shm_attach();
    if (g_bus == NULL) {
        fprintf(stderr, "[COMMS] Failed to attach shared memory\n");
        exit(EXIT_FAILURE);
    }

    /* Step 3: Open semaphore */
    g_sem = sem_open_existing();
    if (g_sem == NULL) {
        fprintf(stderr, "[COMMS] Failed to open semaphore\n");
        exit(EXIT_FAILURE);
    }

    /* Step 4: Open message queue */
    g_mq = msgq_open_existing();
    if (g_mq == (mqd_t)-1) {
        fprintf(stderr, "[COMMS] Failed to open message queue\n");
        exit(EXIT_FAILURE);
    }

    /* Step 5: Open FIFO for reading ground commands */
    g_fifo_fd = fifo_open_read();
    if (g_fifo_fd == -1) {
        fprintf(stderr, "[COMMS] Failed to open FIFO\n");
        exit(EXIT_FAILURE);
    }

    /* Step 6: Initialize comms data */
    pthread_mutex_lock(&g_comms_mutex);
    g_comms.signal_quality  = 90.0f;
    g_comms.uplink_active   = 1;
    g_comms.downlink_active = 1;
    g_comms.fault_active    = 0;
    memset(g_comms.last_command, 0, CMD_MAX_LEN);
    pthread_mutex_unlock(&g_comms_mutex);

    /* Step 7: Start the 2 threads */
    pthread_t tid_uplink, tid_downlink;

    if (pthread_create(&tid_uplink, NULL, uplink_thread, NULL) != 0) {
        perror("[COMMS] pthread_create uplink_thread failed");
        exit(EXIT_FAILURE);
    }

    if (pthread_create(&tid_downlink, NULL, downlink_thread, NULL) != 0) {
        perror("[COMMS] pthread_create downlink_thread failed");
        exit(EXIT_FAILURE);
    }

    /* Step 8: Wait until shutdown signal */
    while (!g_shutdown) {
        sleep(1);
    }

    /* Step 9: Join all threads */
    printf("[COMMS] Shutting down...\n");
    pthread_join(tid_uplink,   NULL);
    pthread_join(tid_downlink, NULL);

    /* Step 10: Release IPC resources */
    shm_detach(g_bus);
    sem_close_handle(g_sem);
    msgq_close(g_mq);
    fifo_close(g_fifo_fd);
    pthread_mutex_destroy(&g_comms_mutex);

    printf("[COMMS] Clean exit.\n");
    return 0;
}
