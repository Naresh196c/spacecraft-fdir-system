#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include "ipc.h"

/* ─────────────────────────────────────────
   FIFO FUNCTIONS
───────────────────────────────────────── */

/*
 * fifo_create()
 * Called ONCE by main.c on startup.
 * Creates the FIFO file at FIFO_PATH.
 * This is what Terminal 2 writes into.
 */
int fifo_create(void) {
    /* Remove old FIFO if it already exists */
    unlink(FIFO_PATH);

    /* Create new FIFO with read/write permissions */
    if (mkfifo(FIFO_PATH, 0666) == -1) {
        perror("[FIFO] mkfifo failed");
        return -1;
    }

    printf("[FIFO] FIFO created → %s\n", FIFO_PATH);
    return 0;
}

/*
 * fifo_open_read()
 * Called by COMMS process (comms_threads.c).
 * Opens FIFO for reading ground station commands.
 *
 * NOTE: O_RDONLY will BLOCK here until someone
 * opens the other end for writing.
 * We use O_RDWR to avoid that blocking.
 */
int fifo_open_read(void) {
    int fd = open(FIFO_PATH, O_RDWR | O_NONBLOCK);
    if (fd == -1) {
        perror("[FIFO] open read failed");
        return -1;
    }

    printf("[FIFO] FIFO opened for reading\n");
    return fd;
}

/*
 * fifo_open_write()
 * Opens FIFO for writing.
 * Used internally if needed to send commands
 * programmatically (e.g. during SIGUSR1 → SNAPSHOT).
 */
int fifo_open_write(void) {
    int fd = open(FIFO_PATH, O_WRONLY | O_NONBLOCK);
    if (fd == -1) {
        perror("[FIFO] open write failed");
        return -1;
    }
    return fd;
}

/*
 * fifo_read_command()
 * Called by uplink_thread in comms_threads.c.
 * Reads one command from the FIFO.
 *
 * Returns:
 *   > 0  → number of bytes read (command received)
 *     0  → no command yet (non-blocking, try again)
 *    -1  → error
 */
int fifo_read_command(int fd, char *buf, int maxlen) {
    memset(buf, 0, maxlen);

    ssize_t bytes = read(fd, buf, maxlen - 1);

    if (bytes == -1) {
        /* EAGAIN means no data yet — not an error in non-blocking mode */
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        perror("[FIFO] read failed");
        return -1;
    }

    if (bytes == 0) {
        return 0;
    }

    /* Strip trailing newline if present */
    buf[bytes] = '\0';
    if (buf[bytes - 1] == '\n') {
        buf[bytes - 1] = '\0';
        bytes--;
    }

    return (int)bytes;
}

/*
 * fifo_close()
 * Called by COMMS process on exit.
 * Closes the file descriptor.
 */
void fifo_close(int fd) {
    if (fd >= 0) {
        if (close(fd) == -1) {
            perror("[FIFO] close failed");
        }
    }
}

/*
 * fifo_destroy()
 * Called ONCE by main.c on shutdown.
 * Deletes the FIFO file from the filesystem.
 */
void fifo_destroy(void) {
    if (unlink(FIFO_PATH) == -1) {
        perror("[FIFO] unlink failed");
    } else {
        printf("[FIFO] FIFO destroyed → %s\n", FIFO_PATH);
    }
}
