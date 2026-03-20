#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <unistd.h>
#include "ipc.h"

/* ─────────────────────────────────────────
   SHARED MEMORY FUNCTIONS
───────────────────────────────────────── */

/*
 * shm_create()
 * Called ONCE by main.c on startup.
 * Creates the shared memory segment and
 * initializes all values to zero/defaults.
 */
telemetry_bus_t *shm_create(void) {
    /* Step 1: Create shared memory object */
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd == -1) {
        perror("[SHM] shm_open failed");
        return NULL;
    }

    /* Step 2: Set the size of shared memory */
    if (ftruncate(fd, sizeof(telemetry_bus_t)) == -1) {
        perror("[SHM] ftruncate failed");
        close(fd);
        return NULL;
    }

    /* Step 3: Map shared memory into this process */
    telemetry_bus_t *bus = mmap(NULL,
                                sizeof(telemetry_bus_t),
                                PROT_READ | PROT_WRITE,
                                MAP_SHARED,
                                fd,
                                0);
    if (bus == MAP_FAILED) {
        perror("[SHM] mmap failed");
        close(fd);
        return NULL;
    }

    close(fd);

    /* Step 4: Initialize all fields to safe defaults */
    memset(bus, 0, sizeof(telemetry_bus_t));
    bus->battery_pct    = 100.0f;
    bus->solar_watts    = 110.0f;
    bus->temp_cpu       = 30.0f;
    bus->temp_battery   = 20.0f;
    bus->temp_antenna   = 25.0f;
    bus->temp_body      = 22.0f;
    bus->spin_rate      = 0.0f;
    bus->signal_quality = 90.0f;
    bus->fault_flags    = FAULT_NONE;
    bus->safe_mode      = MODE_NORMAL;
    get_timestamp(bus->timestamp, sizeof(bus->timestamp));

    printf("[SHM]  Shared memory created → %s\n", SHM_NAME);
    return bus;
}

/*
 * shm_attach()
 * Called by each child process (power, thermal, adcs, comms).
 * Attaches to the already created shared memory.
 */
telemetry_bus_t *shm_attach(void) {
    /* Open existing shared memory (already created by main.c) */
    int fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (fd == -1) {
        perror("[SHM] shm_open attach failed");
        return NULL;
    }

    /* Map into this process */
    telemetry_bus_t *bus = mmap(NULL,
                                sizeof(telemetry_bus_t),
                                PROT_READ | PROT_WRITE,
                                MAP_SHARED,
                                fd,
                                0);
    if (bus == MAP_FAILED) {
        perror("[SHM] mmap attach failed");
        close(fd);
        return NULL;
    }

    close(fd);
    return bus;
}

/*
 * shm_detach()
 * Called by each child process on exit.
 * Unmaps shared memory from that process.
 */
void shm_detach(telemetry_bus_t *bus) {
    if (bus != NULL) {
        if (munmap(bus, sizeof(telemetry_bus_t)) == -1) {
            perror("[SHM] munmap failed");
        }
    }
}

/*
 * shm_destroy()
 * Called ONCE by main.c on shutdown.
 * Unmaps and deletes the shared memory object.
 */
void shm_destroy(telemetry_bus_t *bus) {
    shm_detach(bus);

    if (shm_unlink(SHM_NAME) == -1) {
        perror("[SHM] shm_unlink failed");
    } else {
        printf("[SHM]  Shared memory destroyed → %s\n", SHM_NAME);
    }
}

/* ─────────────────────────────────────────
   SEMAPHORE FUNCTIONS
───────────────────────────────────────── */

/*
 * sem_create()
 * Called ONCE by main.c on startup.
 * Creates a named semaphore with value 1
 * (binary semaphore = mutex for shared memory).
 */
sem_t *sem_create(void) {
    /* O_CREAT creates it, initial value = 1 (unlocked) */
    sem_t *sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    if (sem == SEM_FAILED) {
        perror("[SEM]  sem_open create failed");
        return NULL;
    }

    printf("[SEM]  Semaphore created → %s\n", SEM_NAME);
    return sem;
}

/*
 * sem_open_existing()
 * Called by each child process.
 * Opens the already created semaphore.
 */
sem_t *sem_open_existing(void) {
    sem_t *sem = sem_open(SEM_NAME, 0);
    if (sem == SEM_FAILED) {
        perror("[SEM]  sem_open existing failed");
        return NULL;
    }
    return sem;
}

/*
 * sem_close_handle()
 * Called by each child process on exit.
 * Closes the semaphore handle (does NOT destroy it).
 */
void sem_close_handle(sem_t *sem) {
    if (sem != NULL) {
        if (sem_close(sem) == -1) {
            perror("[SEM]  sem_close failed");
        }
    }
}

/*
 * sem_destroy_handle()
 * Called ONCE by main.c on shutdown.
 * Closes handle AND deletes the semaphore.
 */
void sem_destroy_handle(sem_t *sem) {
    sem_close_handle(sem);

    if (sem_unlink(SEM_NAME) == -1) {
        perror("[SEM]  sem_unlink failed");
    } else {
        printf("[SEM]  Semaphore destroyed → %s\n", SEM_NAME);
    }
}
