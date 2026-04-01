
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include "common.h"
#include "ipc/ipc.h"
#include "signals/signal_handler.h"

/* IPC */
static telemetry_bus_t *g_bus;
static sem_t *g_sem;
static mqd_t g_mq;

/* ---------- START PROCESS ---------- */
pid_t start_process(const char *bin, const char *name) {

    pid_t pid = fork();

    if (pid == 0) {
        execlp(bin, bin, NULL);
        exit(1);
    }

    printf("[OBC] %s started (PID: %d)\n\n", name, pid);
    return pid;
}

/* ---------- INIT IPC ---------- */
void init_ipc() {

    g_bus = shm_create();
    g_sem = sem_create();
    g_mq  = msgq_create();
    fifo_create();

    printf("[OBC] IPC initialized\n\n");
}

/* ---------- CLEAN IPC ---------- */
void clean_ipc() {

    printf("[OBC] Cleaning IPC...\n\n");

    shm_destroy(g_bus);
    sem_destroy_handle(g_sem);
    msgq_close(g_mq);
    msgq_destroy();
    fifo_destroy();
}

/* ---------- WAIT CHILDREN ---------- */
void wait_children() {

    wait(NULL);
    wait(NULL);
    wait(NULL);
    wait(NULL);

    printf("[OBC] All processes stopped\n\n");
}

/* ---------- WATCHDOG ---------- */
void watchdog() {

    sem_wait(g_sem);

    float batt = g_bus->battery_pct;
    float temp = g_bus->temp_cpu;
    float solar = g_bus->solar_watts;
    int safe = g_bus->safe_mode;

    sem_post(g_sem);

    /* ENTER SAFE MODE */
    if (!safe && (batt <= BATTERY_CRITICAL || temp >= TEMP_CRITICAL)) {

        printf("[OBC] SAFE MODE ON\n\n");

        broadcast_safe_mode();

        sem_wait(g_sem);
        g_bus->safe_mode = MODE_SAFE;
        sem_post(g_sem);
    }

    /* EXIT SAFE MODE */
    if (safe && batt > BATTERY_WARNING && temp < TEMP_WARNING && solar > 0) {

        printf("[OBC] SAFE MODE OFF\n\n");

        broadcast_safe_mode_exit();

        sem_wait(g_sem);
        g_bus->safe_mode = MODE_NORMAL;
        sem_post(g_sem);
    }
}

/* ---------- MAIN ---------- */
int main() {

    setup_parent_signals();

    mkdir("bin", 0755);

    init_ipc();

    printf("[OBC] Starting subsystems...\n\n");

    g_pid_logger  = start_process(LOGGER_BIN, "LOGGER");
    sleep(1);

    g_pid_power   = start_process(POWER_BIN, "POWER");
    g_pid_thermal = start_process(THERMAL_BIN, "THERMAL");
    g_pid_comms   = start_process(COMMS_BIN, "COMMS");

    printf("[OBC] Mission started\n\n");

    int t = 0;

    while (!g_shutdown) {

        sleep(1);
        t++;

        if (t % 2 == 0) {
            printf("------------------------------------------------\n");
            watchdog();
        }
    }

    /* SHUTDOWN */
    printf("\n[OBC] Shutdown...\n\n");

    broadcast_shutdown();
    sleep(2);

    wait_children();
    clean_ipc();

    printf("[OBC] Done\n\n");

    return 0;
}
