#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "common.h"
#include "ipc/ipc.h"
#include "signals/signal_handler.h"

/* IPC handles — created in main, cleaned up in main */
static telemetry_bus_t *g_bus = NULL;
static sem_t           *g_sem = NULL;
static mqd_t            g_mq;

/* ─────────────────────────────────────────
   Print the spacecraft banner on startup
───────────────────────────────────────── */
static void print_banner(void) {
    printf("%s",COLOR_CYAN);
    printf("╔══════════════════════════════════════════════════════╗\n");
    printf("║     SPACECRAFT ONBOARD FAULT DETECTION SYSTEM       ║\n");
    printf("║          FDIR v%s  |  Mission: %-16s     ║\n",
           FDIR_VERSION, MISSION_NAME);
    printf("╚══════════════════════════════════════════════════════╝\n");
    printf("%s",COLOR_RESET);
}

/* ─────────────────────────────────────────
   Fork a child process and replace it with
   the given binary using execvp()
───────────────────────────────────────── */
static pid_t launch_process(const char *binary,
                             const char *name) {
    pid_t pid = fork();

    if (pid == -1) {
        perror("[OBC]  fork() failed");
        return -1;
    }

    if (pid == 0) {
        /* Child: replace itself with the subsystem binary */
        char *args[] = { (char *)binary, NULL };
        execvp(binary, args);

        /* execvp only returns if it failed */
        fprintf(stderr, "[OBC]  execvp failed for %s: %s\n",
                binary, strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* Parent: print which process started */
    printf("[OBC]  %-10s started (PID: %d)\n", name, pid);
    return pid;
}

/* ─────────────────────────────────────────
   Create all IPC resources before forking
   Children attach to these after exec()
───────────────────────────────────────── */
static int init_ipc(void) {
    g_bus = shm_create();
    if (g_bus == NULL) return -1;

    g_sem = sem_create();
    if (g_sem == NULL) return -1;

    g_mq = msgq_create();
    if (g_mq == (mqd_t)-1) return -1;

    if (fifo_create() == -1) return -1;

    printf("[OBC]  All IPC resources initialized\n");
    return 0;
}

/* ─────────────────────────────────────────
   Destroy all IPC resources on shutdown
───────────────────────────────────────── */
static void cleanup_ipc(void) {
    printf("[OBC]  Cleaning up IPC resources...\n");
    shm_destroy(g_bus);
    sem_destroy_handle(g_sem);
    msgq_close(g_mq);
    msgq_destroy();
    fifo_destroy();
    printf("[OBC]  IPC cleanup complete\n");
}

/* ─────────────────────────────────────────
   Wait for all 4 child processes to exit
   Uses blocking waitpid() — zero zombies
───────────────────────────────────────── */
static void wait_for_children(void) {
    printf("[OBC]  Waiting for all subsystems to exit...\n");

    pid_t pids[4] = {
        g_pid_power,
        g_pid_thermal,
        g_pid_comms,
        g_pid_logger
    };

    const char *names[4] = {
        "POWER", "THERMAL", "COMMS", "LOGGER"
    };

    for (int i = 0; i < 4; i++) {
        if (pids[i] > 0) {
            int status;
            if (waitpid(pids[i], &status, 0) == -1) {
                if (errno != ECHILD)
                    perror("[OBC]  waitpid failed");
            } else {
                printf("[OBC]  %-8s (PID %d) → reaped ✓\n",
                       names[i], pids[i]);
            }
        }
    }

    printf("[OBC]  All subsystems exited. Zero zombies.\n");
}

/* ─────────────────────────────────────────
   Watchdog — runs every WATCHDOG_INTERVAL seconds
   Reads shared memory and checks for faults
   Triggers or exits Safe Mode automatically
───────────────────────────────────────── */
static void run_watchdog(void) {
    static int last_fault = FAULT_NONE;

    /* Read latest values from shared memory */
    sem_wait(g_sem);
    int   fault    = g_bus->fault_flags;
    int   safe     = g_bus->safe_mode;
    float batt     = g_bus->battery_pct;
    float cpu_temp = g_bus->temp_cpu;
    float solar    = g_bus->solar_watts;
    sem_post(g_sem);


  
    
    /* Battery critically low → trigger Safe Mode */
    if (batt <= BATTERY_CRITICAL && !safe) {
        printf(COLOR_RED
               "[OBC]  WATCHDOG: Critical battery %.1f%%"
               " → triggering SAFE MODE\n"
               COLOR_RESET, batt);
        broadcast_safe_mode();
        sem_wait(g_sem);
        g_bus->safe_mode = MODE_SAFE;
        sem_post(g_sem);
    }

    /* CPU overheating → trigger Safe Mode */
    if (cpu_temp >= TEMP_CRITICAL && !safe) {
        printf(COLOR_RED
               "[OBC]  WATCHDOG: Critical temp %.1f°C"
               " → triggering SAFE MODE\n"
               COLOR_RESET, cpu_temp);
        broadcast_safe_mode();
        sem_wait(g_sem);
        g_bus->safe_mode = MODE_SAFE;
        sem_post(g_sem);
    }

    /* All systems recovered → exit Safe Mode
       Solar must be back online before exiting */
    if (safe &&
        batt     > BATTERY_WARNING &&
        cpu_temp < TEMP_WARNING    &&
        solar    > 0) {
        printf(COLOR_GREEN
               "[OBC]  WATCHDOG: All systems nominal"
               " → exiting SAFE MODE\n"
               COLOR_RESET);
        g_safe_mode = 0;
        g_restart   = 0;
        sem_wait(g_sem);
        g_bus->safe_mode = MODE_NORMAL;
        sem_post(g_sem);
        broadcast_safe_mode_exit();
    }

    (void)fault;
    (void)last_fault;
}

/* ─────────────────────────────────────────
   MAIN — OBC Supervisor entry point
───────────────────────────────────────── */
int main(void) {
    print_banner();

    /* Step 1: Install signal handlers */
    setup_parent_signals();

    /* Step 2: Create bin/ folder for binaries */
    mkdir("bin", 0755);

    /* Step 3: Create all IPC resources */
    if (init_ipc() == -1) {
        fprintf(stderr, "[OBC]  IPC init failed. Aborting.\n");
        exit(EXIT_FAILURE);
    }

    /* Step 4: Fork 4 child processes */
    printf("[OBC]  Launching subsystems...\n");
    printf("──────────────────────────────────────────────\n");

    g_pid_logger  = launch_process(LOGGER_BIN,  "LOGGER");
    sleep(1); /* Logger needs time to open MQ first */

    g_pid_power   = launch_process(POWER_BIN,   "POWER");
    g_pid_thermal = launch_process(THERMAL_BIN, "THERMAL");
    g_pid_comms   = launch_process(COMMS_BIN,   "COMMS");

    /* Check all forks succeeded */
    if (g_pid_power == -1 || g_pid_thermal == -1 ||
        g_pid_comms == -1 || g_pid_logger  == -1) {
        fprintf(stderr, "[OBC]  Fork failed!\n");
        broadcast_shutdown();
        wait_for_children();
        cleanup_ipc();
        exit(EXIT_FAILURE);
    }

    printf("──────────────────────────────────────────────\n");
    printf(COLOR_GREEN
           "[OBC]  All systems GO. Mission started.\n"
           "[OBC]  Press Ctrl+C to shutdown.\n"
           "[OBC]  Send commands via: echo \"CMD\" > %s\n"
           COLOR_RESET, FIFO_PATH);
    printf("──────────────────────────────────────────────\n");

    /* Step 5: Main loop — print separator + run watchdog */
    int watchdog_tick = 0;
    int shutdown_sent = 0;

    while (!g_shutdown) {
        sleep(1);
        watchdog_tick++;

        /* Separator line every 2 seconds */
        if (watchdog_tick % 2 == 0) {
            printf(COLOR_BLUE
                   "┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄\n"
                   COLOR_RESET);
        }

        /* Run watchdog every WATCHDOG_INTERVAL seconds */
        if (watchdog_tick % WATCHDOG_INTERVAL == 0) {
            run_watchdog();
        }
    }

    /* Step 6: Clean shutdown */
    /* Step 6: Clean shutdown */
    if (!shutdown_sent) {
        shutdown_sent = 1;
        broadcast_shutdown();
    }
    sleep(2);
    wait_for_children();
    cleanup_ipc();
    return 0;
}
