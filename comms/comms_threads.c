#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "comms.h"
#include "../signals/signal_handler.h"

/* ─────────────────────────────────────────
   UPLINK THREAD
   Continuously reads commands from FIFO.
   This is what receives commands typed in
   Terminal 2 like:
     echo "STATUS" > /tmp/groundstation
   Non-blocking read — checks every 1 second.
───────────────────────────────────────── */
void *uplink_thread(void *arg) {
    (void)arg;
    char cmd_buf[CMD_MAX_LEN];

    printf("[COMMS] Uplink thread ready — "
           "waiting for ground commands...\n");

    while (!g_shutdown) {
        /* Try to read a command from FIFO (non-blocking) */
        int bytes = fifo_read_command(g_fifo_fd,
                                      cmd_buf,
                                      CMD_MAX_LEN);
        if (bytes > 0) {
            /* Command received — handle it */
            pthread_mutex_lock(&g_comms_mutex);
            g_comms.uplink_active = 1;
            pthread_mutex_unlock(&g_comms_mutex);

            comms_handle_command(cmd_buf);

        } else {
            /* No command yet — mark uplink idle */
            pthread_mutex_lock(&g_comms_mutex);
            g_comms.uplink_active = 0;
            pthread_mutex_unlock(&g_comms_mutex);
        }

        sleep(1);
    }

    return NULL;
}

/* ─────────────────────────────────────────
   DOWNLINK THREAD
   Simulates signal quality every second.
   Every 30 seconds sends a telemetry
   snapshot to the message queue so the
   logger saves it to fdr.log.
   Sends WARNING if signal drops < 30%.
   Sends CRITICAL if signal drops < 10%.
───────────────────────────────────────── */
void *downlink_thread(void *arg) {
    (void)arg;
    srand(time(NULL) ^ getpid());

    int elapsed = 0;

    while (!g_shutdown) {
        sleep(SENSOR_INTERVAL);
        elapsed++;

        pthread_mutex_lock(&g_comms_mutex);

        /* Simulate signal quality fluctuation */
        if (g_safe_mode) {
            /* In safe mode signal is suspended */
            g_comms.downlink_active = 0;
            g_comms.signal_quality  = 0.0f;
            printf(COLOR_YELLOW
                   "[COMMS] SAFE MODE | Downlink suspended\n"
                   COLOR_RESET);
            pthread_mutex_unlock(&g_comms_mutex);
            continue;
        }

        /* Normal signal: random between 60% and 100% */
        /* Occasional dropout: drops to 5-25% */
        int dropout = (rand() % 20 == 0);  /* 5% chance per second */
        if (dropout) {
            g_comms.signal_quality = (float)(rand() % 20 + 5);
        } else {
            g_comms.signal_quality = (float)(rand() % 40 + 60);
        }

        g_comms.downlink_active = 1;
        float sig = g_comms.signal_quality;

        printf(COLOR_CYAN
               "[COMMS] Signal: %.1f%% | Uplink: %s | Downlink: ACTIVE\n"
               COLOR_RESET,
               sig,
               g_comms.uplink_active ? "RECEIVING" : "IDLE");

        /* Check signal thresholds */
        if (sig <= SIGNAL_CRITICAL && !g_comms.fault_active) {
            g_comms.fault_active = 1;
            pthread_mutex_unlock(&g_comms_mutex);

            printf(COLOR_RED
                   "[COMMS] ⚠⚠ CRITICAL: Signal lost = %.1f%%\n"
                   COLOR_RESET, sig);

            comms_send_fault("CRITICAL",
                             sig,
                             SIGNAL_CRITICAL,
                             "Ground link lost — signal below threshold");
            continue;

        } else if (sig <= SIGNAL_WARNING && !g_comms.fault_active) {
            pthread_mutex_unlock(&g_comms_mutex);

            printf(COLOR_YELLOW
                   "[COMMS] ⚠ WARNING: Weak signal = %.1f%%\n"
                   COLOR_RESET, sig);

            comms_send_fault("WARNING",
                             sig,
                             SIGNAL_WARNING,
                             "Signal quality degraded");
            continue;

        } else if (sig > SIGNAL_WARNING && g_comms.fault_active) {
            g_comms.fault_active = 0;
            pthread_mutex_unlock(&g_comms_mutex);

            printf(COLOR_GREEN
                   "[COMMS] Signal restored: %.1f%%\n"
                   COLOR_RESET, sig);

            comms_send_fault("INFO",
                             sig,
                             SIGNAL_WARNING,
                             "Ground link restored");
            continue;
        }

        pthread_mutex_unlock(&g_comms_mutex);

        /* Every 30 seconds send a telemetry snapshot to FDR */
        if (elapsed % 30 == 0) {
            comms_send_fault("INFO",
                             sig,
                             0.0f,
                             "Periodic telemetry snapshot — all systems");
            printf(COLOR_BLUE
                   "[COMMS] Periodic telemetry snapshot sent to FDR\n"
                   COLOR_RESET);
        }

        /* Write signal quality to shared memory bus */
        sem_wait(g_sem);
        g_bus->signal_quality = sig;
        g_bus->uplink_active  = g_comms.uplink_active;
        sem_post(g_sem);
    }

    return NULL;
}
