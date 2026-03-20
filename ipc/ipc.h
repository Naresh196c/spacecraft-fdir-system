#ifndef IPC_H
#define IPC_H

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <mqueue.h>
#include "../common.h"

/* ─────────────────────────────────────────
   MESSAGE QUEUE SETTINGS
───────────────────────────────────────── */
#define MQ_MAX_MESSAGES     10          /* max messages in queue at once  */
#define MQ_MAX_MSG_SIZE     sizeof(fault_report_t)

/* ─────────────────────────────────────────
   MESSAGE PRIORITY LEVELS
   Higher number = delivered first
───────────────────────────────────────── */
#define MQ_PRIORITY_CRITICAL    9
#define MQ_PRIORITY_WARNING     5
#define MQ_PRIORITY_INFO        1

/* ─────────────────────────────────────────
   GROUND STATION COMMANDS
   Received via FIFO from Terminal 2
───────────────────────────────────────── */
#define CMD_STATUS          "STATUS"
#define CMD_SAFE_MODE       "SAFE_MODE"
#define CMD_SNAPSHOT        "SNAPSHOT"
#define CMD_RESET_ADCS      "RESET_ADCS"
#define CMD_MAX_LEN         32

/* ─────────────────────────────────────────
   SHARED MEMORY FUNCTIONS
   shm_manager.c
───────────────────────────────────────── */

/* Create shared memory segment — called by main.c on startup */
telemetry_bus_t *shm_create(void);

/* Attach to existing shared memory — called by each subsystem */
telemetry_bus_t *shm_attach(void);

/* Detach from shared memory — called by each subsystem on exit */
void shm_detach(telemetry_bus_t *bus);

/* Destroy shared memory — called by main.c on shutdown */
void shm_destroy(telemetry_bus_t *bus);

/* ─────────────────────────────────────────
   SEMAPHORE FUNCTIONS
   shm_manager.c
───────────────────────────────────────── */

/* Create semaphore — called by main.c on startup */
sem_t *sem_create(void);

/* Open existing semaphore — called by subsystems */
sem_t *sem_open_existing(void);

/* Close semaphore — called by subsystems on exit */
void sem_close_handle(sem_t *sem);

/* Destroy semaphore — called by main.c on shutdown */
void sem_destroy_handle(sem_t *sem);

/* ─────────────────────────────────────────
   MESSAGE QUEUE FUNCTIONS
   msgq_manager.c
───────────────────────────────────────── */

/* Create message queue — called by main.c on startup */
mqd_t msgq_create(void);

/* Open existing message queue — called by subsystems */
mqd_t msgq_open_existing(void);

/* Send a fault report to the queue */
int msgq_send(mqd_t mq, fault_report_t *report, int priority);

/* Receive a fault report from the queue */
int msgq_receive(mqd_t mq, fault_report_t *report);

/* Close message queue handle */
void msgq_close(mqd_t mq);

/* Destroy message queue — called by main.c on shutdown */
void msgq_destroy(void);

/* ─────────────────────────────────────────
   FIFO FUNCTIONS
   fifo_manager.c
───────────────────────────────────────── */

/* Create FIFO file — called by main.c on startup */
int fifo_create(void);

/* Open FIFO for reading — called by COMMS process */
int fifo_open_read(void);

/* Open FIFO for writing — used by ground station commands */
int fifo_open_write(void);

/* Read a command from FIFO */
int fifo_read_command(int fd, char *buf, int maxlen);

/* Close FIFO file descriptor */
void fifo_close(int fd);

/* Delete FIFO file — called by main.c on shutdown */
void fifo_destroy(void);

#endif /* IPC_H */
