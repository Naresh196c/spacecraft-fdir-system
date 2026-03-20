#ifndef LOGGER_H
#define LOGGER_H

#include <pthread.h>
#include "../common.h"
#include "../ipc/ipc.h"

/* ─────────────────────────────────────────
   SHARED RESOURCES
   Declared in logger.c
   Used by file_io.c
───────────────────────────────────────── */
extern mqd_t    g_mq;           /* message queue to read from    */
extern int      g_log_fd;       /* fdr.log file descriptor       */
extern int      g_paused;       /* 1 = logging paused by ground  */

/* ─────────────────────────────────────────
   FILE I/O FUNCTIONS
   Defined in file_io.c
───────────────────────────────────────── */

/* Create fdr.log and write header at byte 0 */
int  fdr_open(void);

/* Write one fault entry to fdr.log
   Uses lseek to update header entry_count */
int  fdr_write_entry(fault_report_t *report);

/* Read header from fdr.log on startup
   to get existing entry count */
int  fdr_read_header(fdr_header_t *header);

/* Close fdr.log file descriptor */
void fdr_close(void);

#endif /* LOGGER_H */
