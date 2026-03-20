#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "logger.h"

/* ─────────────────────────────────────────
   fdr_open()
   Called by logger.c on startup.
   Creates fdr.log if it doesn't exist.
   Writes a fresh header at byte 0.
   Uses open() — no stdio.
───────────────────────────────────────── */
int fdr_open(void) {
    /* Open or create fdr.log */
    int fd = open(FDR_LOG_PATH,
                  O_CREAT | O_RDWR,
                  0644);
    if (fd == -1) {
        perror("[FDR]  open fdr.log failed");
        return -1;
    }

    /* Check if file is brand new (size = 0) */
    off_t size = lseek(fd, 0, SEEK_END);
    if (size == -1) {
        perror("[FDR]  lseek SEEK_END failed");
        close(fd);
        return -1;
    }

    if (size == 0) {
        /* Brand new file — write fresh header */
        fdr_header_t header;
        memset(&header, 0, sizeof(header));

        strncpy(header.magic,   "FDRLOG",      sizeof(header.magic)   - 1);
        strncpy(header.mission, MISSION_NAME,  sizeof(header.mission) - 1);
        header.version     = 1;
        header.entry_count = 0;
        get_timestamp(header.created_at, sizeof(header.created_at));

        /* Seek to byte 0 and write header */
        lseek(fd, 0, SEEK_SET);
        if (write(fd, &header, sizeof(fdr_header_t)) == -1) {
            perror("[FDR]  write header failed");
            close(fd);
            return -1;
        }

        printf("[FDR]  New log created → %s\n", FDR_LOG_PATH);

    } else {
        /* Existing file — read current entry count */
        fdr_header_t header;
        if (fdr_read_header(&header) == 0) {
            printf("[FDR]  Existing log found → %s "
                   "(entries: %d)\n",
                   FDR_LOG_PATH,
                   header.entry_count);
        }
    }

    return fd;
}

/* ─────────────────────────────────────────
   fdr_read_header()
   Reads the 128-byte header from byte 0
   of fdr.log using read().
   Called on startup to get entry_count.
───────────────────────────────────────── */
int fdr_read_header(fdr_header_t *header) {
    /* Seek to beginning of file */
    if (lseek(g_log_fd, 0, SEEK_SET) == -1) {
        perror("[FDR]  lseek read header failed");
        return -1;
    }

    /* Read the header */
    ssize_t bytes = read(g_log_fd,
                         header,
                         sizeof(fdr_header_t));
    if (bytes == -1) {
        perror("[FDR]  read header failed");
        return -1;
    }

    if (bytes < (ssize_t)sizeof(fdr_header_t)) {
        fprintf(stderr, "[FDR]  Incomplete header read\n");
        return -1;
    }

    return 0;
}

/* ─────────────────────────────────────────
   fdr_write_entry()
   Called every time a fault report arrives
   from the message queue.

   Steps:
   1. Read current entry_count from header
   2. Seek to end of file
   3. Write the new entry (128 bytes)
   4. lseek back to byte 8 (entry_count field)
   5. Write updated entry_count
───────────────────────────────────────── */
int fdr_write_entry(fault_report_t *report) {
    /* Step 1: Read current header to get entry_count */
    fdr_header_t header;
    if (fdr_read_header(&header) == -1) {
        return -1;
    }

    /* Step 2: Build the log entry */
    fdr_entry_t entry;
    memset(&entry, 0, sizeof(entry));

    entry.entry_id  = header.entry_count + 1;
    entry.value     = report->value;
    entry.threshold = report->threshold;
    strncpy(entry.subsystem,   report->subsystem,
            sizeof(entry.subsystem)   - 1);
    strncpy(entry.severity,    report->severity,
            sizeof(entry.severity)    - 1);
    strncpy(entry.description, report->description,
            sizeof(entry.description) - 1);
    strncpy(entry.timestamp,   report->timestamp,
            sizeof(entry.timestamp)   - 1);

    /* Step 3: Seek to end of file and write entry */
    if (lseek(g_log_fd, 0, SEEK_END) == -1) {
        perror("[FDR]  lseek SEEK_END write failed");
        return -1;
    }

    if (write(g_log_fd, &entry, sizeof(fdr_entry_t)) == -1) {
        perror("[FDR]  write entry failed");
        return -1;
    }

    /* Step 4: Update entry_count in header
       entry_count is at offset:
       magic(8) + mission(16) + version(4) = byte 28 */
    header.entry_count++;
    off_t count_offset = (off_t)(
        sizeof(header.magic)   +
        sizeof(header.mission) +
        sizeof(header.version)
    );

    if (lseek(g_log_fd, count_offset, SEEK_SET) == -1) {
        perror("[FDR]  lseek to entry_count failed");
        return -1;
    }

    /* Step 5: Write updated count */
    if (write(g_log_fd,
              &header.entry_count,
              sizeof(header.entry_count)) == -1) {
        perror("[FDR]  write entry_count failed");
        return -1;
    }

    printf(COLOR_MAGENTA
           "[FDR]  Entry #%d written → [%s] %s: %s\n"
           COLOR_RESET,
           entry.entry_id,
           entry.severity,
           entry.subsystem,
           entry.description);

    return 0;
}

/* ─────────────────────────────────────────
   fdr_close()
   Called by logger.c on shutdown.
   Closes the fdr.log file descriptor.
───────────────────────────────────────── */
void fdr_close(void) {
    if (g_log_fd >= 0) {
        if (close(g_log_fd) == -1) {
            perror("[FDR]  close failed");
        } else {
            printf("[FDR]  Log file closed → %s\n",
                   FDR_LOG_PATH);
        }
        g_log_fd = -1;
    }
}
