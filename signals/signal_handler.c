#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include "signal_handler.h"

volatile sig_atomic_t g_shutdown   = 0;
volatile sig_atomic_t g_safe_mode  = 0;
volatile sig_atomic_t g_restart    = 0;

pid_t g_pid_power   = -1;
pid_t g_pid_thermal = -1;
pid_t g_pid_comms   = -1;
pid_t g_pid_logger  = -1;

void handle_sigint(int sig) {
    (void)sig;
    write(STDOUT_FILENO,
          "\n[OBC]  SIGINT received → Initiating shutdown...\n", 50);
    g_shutdown = 1;
}

void handle_sigusr1(int sig) {
    (void)sig;
    write(STDOUT_FILENO,
          "[OBC]  SIGUSR1 received → Entering SAFE MODE\n", 46);
    g_safe_mode = 1;
}

/* Children use this → exit safe mode */
void handle_sigusr2(int sig) {
    (void)sig;
    g_safe_mode = 0;
    write(STDOUT_FILENO,
          "[OBC]  SIGUSR2 received → Exiting SAFE MODE\n", 45);
}

/* Parent uses this → restart ADCS */
void handle_sigusr2_parent(int sig) {
    (void)sig;
    write(STDOUT_FILENO,
          "[OBC]  SIGUSR2 received → Restarting crashed subsystem\n", 56);
    g_restart = 1;
}

void handle_sigchld(int sig) {
    (void)sig;
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (pid == g_pid_power) {
            write(STDOUT_FILENO, "[OBC]  POWER process exited\n", 28);
            g_pid_power = -1;
        } else if (pid == g_pid_thermal) {
            write(STDOUT_FILENO, "[OBC]  THERMAL process exited\n", 30);
            g_pid_thermal = -1;
        } else if (pid == g_pid_comms) {
            write(STDOUT_FILENO, "[OBC]  COMMS process exited\n", 28);
            g_pid_comms = -1;
        } else if (pid == g_pid_logger) {
            write(STDOUT_FILENO, "[OBC]  LOGGER process exited\n", 29);
            g_pid_logger = -1;
        }
    }
}

void setup_parent_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sa.sa_handler = handle_sigint;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("[SIG]  sigaction SIGINT failed"); exit(EXIT_FAILURE);
    }
    sa.sa_handler = handle_sigusr1;
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("[SIG]  sigaction SIGUSR1 failed"); exit(EXIT_FAILURE);
    }
    sa.sa_handler = handle_sigusr2_parent;
    if (sigaction(SIGUSR2, &sa, NULL) == -1) {
        perror("[SIG]  sigaction SIGUSR2 failed"); exit(EXIT_FAILURE);
    }
    sa.sa_handler = handle_sigchld;
    sa.sa_flags   = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("[SIG]  sigaction SIGCHLD failed"); exit(EXIT_FAILURE);
    }
    printf("[SIG]  Signal handlers installed\n");
}

void setup_child_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sa.sa_handler = SIG_IGN;
    sigaction(SIGINT, &sa, NULL);

    sa.sa_handler = handle_sigusr1;
    sigaction(SIGUSR1, &sa, NULL);

    /* SIGUSR2 → exit safe mode */
    sa.sa_handler = handle_sigusr2;
    sigaction(SIGUSR2, &sa, NULL);

    sa.sa_handler = handle_sigint;
    sigaction(SIGTERM, &sa, NULL);
}

void broadcast_safe_mode(void) {
    printf(COLOR_YELLOW
           "[OBC]  Broadcasting SAFE MODE to all subsystems...\n"
           COLOR_RESET);
    if (g_pid_power   > 0) kill(g_pid_power,   SIGUSR1);
    if (g_pid_thermal > 0) kill(g_pid_thermal,  SIGUSR1);
    if (g_pid_comms   > 0) kill(g_pid_comms,    SIGUSR1);
    if (g_pid_logger  > 0) kill(g_pid_logger,   SIGUSR1);
}

void broadcast_safe_mode_exit(void) {
    printf(COLOR_GREEN
           "[OBC]  All subsystems resuming NORMAL mode\n"
           COLOR_RESET);
    if (g_pid_power   > 0) kill(g_pid_power,   SIGUSR2);
    if (g_pid_thermal > 0) kill(g_pid_thermal,  SIGUSR2);
    if (g_pid_comms   > 0) kill(g_pid_comms,    SIGUSR2);
    if (g_pid_logger  > 0) kill(g_pid_logger,   SIGUSR2);
}

void broadcast_shutdown(void) {
    printf("[OBC]  Sending SIGTERM to all subsystems...\n");
    if (g_pid_power   > 0) kill(g_pid_power,   SIGTERM);
    if (g_pid_thermal > 0) kill(g_pid_thermal,  SIGTERM);
    if (g_pid_comms   > 0) kill(g_pid_comms,    SIGTERM);
    if (g_pid_logger  > 0) kill(g_pid_logger,   SIGTERM);
}
