// file_io.c (Simplified Version)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "logger.h"
#include <stddef.h>
/* ---------- OPEN FILE ---------- */
int fdr_open(void) {

    int fd = open(FDR_LOG_PATH, O_CREAT | O_RDWR, 0644);

    if (fd < 0) {
        perror("open");
        return -1;
    }

    /* Check if file empty */
    off_t size = lseek(fd, 0, SEEK_END);

    if (size == 0) {

        fdr_header_t header = {0};

        strcpy(header.magic, "FDRLOG");
        strcpy(header.mission, MISSION_NAME);
        header.version = 1;
        header.entry_count = 0;

        lseek(fd, 0, SEEK_SET);
        write(fd, &header, sizeof(header));

        printf("[FDR] New log created\n");

    } else {
        printf("[FDR] Existing log opened\n");
    }

    return fd;
}

/* ---------- READ HEADER ---------- */
int fdr_read_header(fdr_header_t *header) {

    lseek(g_log_fd, 0, SEEK_SET);

    if (read(g_log_fd, header, sizeof(*header)) <= 0) {
        perror("read header");
        return -1;
    }

    return 0;
}

/* ---------- WRITE ENTRY ---------- */
int fdr_write_entry(fault_report_t *report) {

    fdr_header_t header;

    if (fdr_read_header(&header) == -1)
        return -1;

    fdr_entry_t entry = {0};

    entry.entry_id = header.entry_count + 1;
    entry.value = report->value;
    entry.threshold = report->threshold;

    strcpy(entry.subsystem, report->subsystem);
    strcpy(entry.severity, report->severity);
    strcpy(entry.description, report->description);
    strcpy(entry.timestamp, report->timestamp);

    /* Write entry at end */
    lseek(g_log_fd, 0, SEEK_END);
    write(g_log_fd, &entry, sizeof(entry));

    /* Update count */
    header.entry_count++;

    lseek(g_log_fd, offsetof(fdr_header_t, entry_count), SEEK_SET);
    write(g_log_fd, &header.entry_count, sizeof(header.entry_count));

    printf("[FDR] Entry %d logged\n", entry.entry_id);

    return 0;
}

/* ---------- CLOSE FILE ---------- */
void fdr_close(void) {

    if (g_log_fd >= 0) {
        close(g_log_fd);
        printf("[FDR] Log closed\n");
        g_log_fd = -1;
    }
}
