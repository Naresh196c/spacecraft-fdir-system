#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <errno.h>
#include "ipc.h"

/* ─────────────────────────────────────────
   MESSAGE QUEUE FUNCTIONS
───────────────────────────────────────── */

/*
 * msgq_create()
 * Called ONCE by main.c on startup.
 * Creates the message queue with priority support.
 * Higher priority messages are delivered first.
 */
mqd_t msgq_create(void) {
    struct mq_attr attr;

    /* Set message queue attributes */
    attr.mq_flags   = 0;                        /* blocking mode            */
    attr.mq_maxmsg  = MQ_MAX_MESSAGES;          /* max 10 messages          */
    attr.mq_msgsize = sizeof(fault_report_t);   /* each message = fault_report_t */
    attr.mq_curmsgs = 0;                        /* currently 0 messages     */

    /* Create the message queue */
    mqd_t mq = mq_open(MQ_NAME,
                        O_CREAT | O_RDWR,
                        0666,
                        &attr);
    if (mq == (mqd_t)-1) {
        perror("[MQ]   mq_open create failed");
        return (mqd_t)-1;
    }

    printf("[MQ]   Message queue created → %s\n", MQ_NAME);
    return mq;
}

/*
 * msgq_open_existing()
 * Called by child processes (monitor, logger).
 * Opens the already created message queue.
 */
mqd_t msgq_open_existing(void) {
    mqd_t mq = mq_open(MQ_NAME, O_RDWR);
    if (mq == (mqd_t)-1) {
        perror("[MQ]   mq_open existing failed");
        return (mqd_t)-1;
    }
    return mq;
}

/*
 * msgq_send()
 * Called by OBC (main.c) when a fault is detected.
 * Sends a fault_report_t to the queue with a priority.
 *
 * Priority values:
 *   MQ_PRIORITY_CRITICAL = 9  → delivered first
 *   MQ_PRIORITY_WARNING  = 5
 *   MQ_PRIORITY_INFO     = 1  → delivered last
 */
int msgq_send(mqd_t mq, fault_report_t *report, int priority) {
    if (mq_send(mq,
                (const char *)report,
                sizeof(fault_report_t),
                priority) == -1) {
        perror("[MQ]   mq_send failed");
        return -1;
    }

    printf(COLOR_MAGENTA
           "[MQ]   Alert sent → [%s] %s: %.1f\n"
           COLOR_RESET,
           report->severity,
           report->subsystem,
           report->value);

    return 0;
}

/*
 * msgq_receive()
 * Called by logger.c in a loop.
 * Blocks until a message arrives.
 * Highest priority message is always received first.
 */
int msgq_receive(mqd_t mq, fault_report_t *report) {
    unsigned int priority;

    ssize_t bytes = mq_receive(mq,
                               (char *)report,
                               sizeof(fault_report_t),
                               &priority);
    if (bytes == -1) {
        if (errno != EINTR) {
            perror("[MQ]   mq_receive failed");
        }
        return -1;
    }

    return (int)priority;
}

/*
 * msgq_close()
 * Called by each process when done using the queue.
 * Closes the handle but does NOT delete the queue.
 */
void msgq_close(mqd_t mq) {
    if (mq != (mqd_t)-1) {
        if (mq_close(mq) == -1) {
            perror("[MQ]   mq_close failed");
        }
    }
}

/*
 * msgq_destroy()
 * Called ONCE by main.c on shutdown.
 * Permanently deletes the message queue.
 */
void msgq_destroy(void) {
    if (mq_unlink(MQ_NAME) == -1) {
        perror("[MQ]   mq_unlink failed");
    } else {
        printf("[MQ]   Message queue destroyed → %s\n", MQ_NAME);
    }
}
