#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <mqueue.h>
#include "logger.h"
#include "../signals/signal_handler.h"

/* ─────────────────────────────────────────
   GLOBAL VARIABLES
───────────────────────────────────────── */
mqd_t   g_mq      = (mqd_t)-1;
int     g_log_fd  = -1;
int     g_paused  = 0;          /* 1 = logging paused by SAFE MODE */

/* ─────────────────────────────────────────
   MAIN — LOGGER PROCESS ENTRY POINT
   This process:
   1. Opens fdr.log
   2. Opens message queue
   3. Loops — reads fault reports from MQ
   4. Writes each report to fdr.log
   5. Respects g_paused flag (SAFE MODE)
───────────────────────────────────────── */
int main(void) {
    printf(COLOR_MAGENTA
           "[FDR]  Logger process started (PID: %d)\n"
           COLOR_RESET, getpid());

    /* Step 1: Install child signal handlers */
    setup_child_signals();

    /* Step 2: Open fdr.log */
    g_log_fd = fdr_open();
    if (g_log_fd == -1) {
        fprintf(stderr, "[FDR]  Failed to open log file\n");
        exit(EXIT_FAILURE);
    }

    /* Step 3: Open message queue */
    g_mq = msgq_open_existing();
    if (g_mq == (mqd_t)-1) {
        fprintf(stderr, "[FDR]  Failed to open message queue\n");
        fdr_close();
        exit(EXIT_FAILURE);
    }

    printf("[FDR]  Listening for fault reports...\n");

    /* Step 4: Main loop — read from MQ and write to log
       Use timed receive so we can check g_shutdown flag
       every second instead of blocking forever           */
    fault_report_t report;

    while (!g_shutdown) {

        /* Check safe mode — pause logging if active */
        if (g_safe_mode && !g_paused) {
            g_paused = 1;
            printf(COLOR_YELLOW
                   "[FDR]  SAFE MODE — logging paused\n"
                   COLOR_RESET);
        } else if (!g_safe_mode && g_paused) {
            g_paused = 0;
            printf(COLOR_GREEN
                   "[FDR]  Logging resumed\n"
                   COLOR_RESET);
        }

        /* Use timed receive — wait max 1 second
           then loop back to check g_shutdown     */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;

        unsigned int priority = 0;
        ssize_t bytes = mq_timedreceive(g_mq,
                                        (char *)&report,
                                        sizeof(fault_report_t),
                                        &priority,
                                        &ts);
        if (bytes == -1) {
            /* Timeout or signal — check shutdown flag */
            if (g_shutdown) break;
            continue;
        }

        /* Write to fdr.log only if not paused
           But CRITICAL faults always get logged
           even in safe mode                     */
        if (!g_paused ||
            strcmp(report.severity, "CRITICAL") == 0) {
            fdr_write_entry(&report);
        } else {
            printf("[FDR]  Entry skipped (paused): "
                   "[%s] %s\n",
                   report.severity,
                   report.description);
        }
    }

    /* Step 5: Clean shutdown */
    printf("[FDR]  Shutting down...\n");

    /* Read final header to print summary */
    fdr_header_t header;
    if (fdr_read_header(&header) == 0) {
        printf(COLOR_MAGENTA
               "[FDR]  Mission complete → "
               "Total entries logged: %d\n"
               COLOR_RESET,
               header.entry_count);
    }

    /* Step 6: Release resources */
    msgq_close(g_mq);
    fdr_close();

    printf("[FDR]  Clean exit.\n");
    return 0;
}
