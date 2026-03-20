#ifndef SIGNAL_HANDLER_H
#define SIGNAL_HANDLER_H

#include <signal.h>
#include <sys/types.h>
#include "../common.h"

/* ─────────────────────────────────────────
   GLOBAL FLAGS
   Checked in every process main loop.
───────────────────────────────────────── */
extern volatile sig_atomic_t g_shutdown;   /* 1 = exit main loop     */
extern volatile sig_atomic_t g_safe_mode;  /* 1 = enter safe mode    */
extern volatile sig_atomic_t g_restart;    /* 1 = restart a process  */

/* ─────────────────────────────────────────
   CHILD PROCESS PIDs
   Set by main.c after each fork().
───────────────────────────────────────── */
extern pid_t g_pid_power;
extern pid_t g_pid_thermal;
extern pid_t g_pid_comms;
extern pid_t g_pid_logger;

/* ─────────────────────────────────────────
   SIGNAL HANDLERS
───────────────────────────────────────── */
void handle_sigint(int sig);    /* Ctrl+C  → shutdown          */
void handle_sigusr1(int sig);   /* SIGUSR1 → safe mode         */
void handle_sigusr2(int sig);   /* SIGUSR2 → restart subsystem */
void handle_sigchld(int sig);   /* SIGCHLD → reap dead child   */

/* ─────────────────────────────────────────
   SETUP FUNCTIONS
───────────────────────────────────────── */
void setup_parent_signals(void);  /* called by main.c           */
void setup_child_signals(void);   /* called by each child       */

/* ─────────────────────────────────────────
   BROADCAST FUNCTIONS
───────────────────────────────────────── */
void broadcast_safe_mode(void);   /* sends SIGUSR1 to all children */
void broadcast_safe_mode_exit(void); /* sends SIGUSR2 to all children */
void broadcast_shutdown(void);    /* sends SIGTERM to all children */

#endif /* SIGNAL_HANDLER_H */
