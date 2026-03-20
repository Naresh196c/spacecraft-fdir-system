#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "power.h"
#include "../signals/signal_handler.h"

/* ─────────────────────────────────────────
   SOLAR THREAD
   Simulates solar panel output every second.
   During eclipse (t=45s to t=60s):
     → solar watts drops to 0
   Otherwise:
     → random value between 90W and 130W
───────────────────────────────────────── */
void *solar_thread(void *arg) {
    (void)arg;
    srand(time(NULL) ^ (getpid() << 8));

    int elapsed = 0;

    while (!g_shutdown) {
        sleep(SENSOR_INTERVAL);
        elapsed++;

        pthread_mutex_lock(&g_power_mutex);

        /* Simulate eclipse window */
        if (elapsed >= ECLIPSE_START && elapsed <= ECLIPSE_END) {
            g_power.solar_watts = 0.0f;
            g_power.in_eclipse  = 1;
        } else {
            /* Normal sunlight — random between 90W and 130W */
            g_power.solar_watts = (float)(rand() % 40 + 90);
            g_power.in_eclipse  = 0;
        }

        /* Safe mode → print minimal info */
        if (g_safe_mode) {
            printf(COLOR_YELLOW
                   "[POWER] SAFE MODE | Solar: %.0fW\n"
                   COLOR_RESET,
                   g_power.solar_watts);
        } else {
            printf(COLOR_CYAN
                   "[POWER] Solar: %.0fW | Eclipse: %s\n"
                   COLOR_RESET,
                   g_power.solar_watts,
                   g_power.in_eclipse ? "YES" : "NO");
        }

        pthread_mutex_unlock(&g_power_mutex);
    }

    return NULL;
}

/* ─────────────────────────────────────────
   BATTERY THREAD
   Simulates battery charge/drain every second.
   In eclipse  → battery drains by 2-5% per sec
   In sunlight → battery charges by 3-6% per sec
   Sends WARNING at < 20%, CRITICAL at < 10%
───────────────────────────────────────── */
void *battery_thread(void *arg) {
    (void)arg;

    while (!g_shutdown) {
        sleep(SENSOR_INTERVAL);

        pthread_mutex_lock(&g_power_mutex);

        if (g_power.in_eclipse) {
            /* Draining — lose 2 to 5% per second */
            float drain = (float)(rand() % 8 + 8);
            g_power.battery_pct -= drain;
            if (g_power.battery_pct < 0.0f)
                g_power.battery_pct = 0.0f;
        } else {
            /* Charging — gain 3 to 6% per second */
            float charge = (float)(rand() % 4 + 3);
            g_power.battery_pct += charge;
            if (g_power.battery_pct > 100.0f)
                g_power.battery_pct = 100.0f;
        }

        float batt = g_power.battery_pct;

        /* Print battery status */
        if (g_safe_mode) {
            printf(COLOR_YELLOW
                   "[POWER] SAFE MODE | Battery: %.1f%%\n"
                   COLOR_RESET, batt);
        } else {
            printf(COLOR_CYAN
                   "[POWER] Battery: %.1f%% | %s\n"
                   COLOR_RESET,
                   batt,
                   g_power.in_eclipse ? "Discharging" : "Charging");
        }

        /* Check thresholds and send alerts */
        if (batt <= BATTERY_CRITICAL && !g_power.fault_active) {
            g_power.fault_active = 1;
            pthread_mutex_unlock(&g_power_mutex);

            printf(COLOR_RED
                   "[POWER] ⚠⚠ CRITICAL: Battery = %.1f%%\n"
                   COLOR_RESET, batt);

            power_send_fault("CRITICAL",
                             batt,
                             BATTERY_CRITICAL,
                             "Battery critically low — Safe Mode required");
            continue;

        } else if (batt <= BATTERY_WARNING && !g_power.fault_active) {
            pthread_mutex_unlock(&g_power_mutex);

            printf(COLOR_YELLOW
                   "[POWER] ⚠ WARNING: Battery = %.1f%%\n"
                   COLOR_RESET, batt);

            power_send_fault("WARNING",
                             batt,
                             BATTERY_WARNING,
                             "Battery low — monitoring closely");
            continue;

        } else if (batt > BATTERY_WARNING && g_power.fault_active) {
            /* Battery recovered */
            g_power.fault_active = 0;
            printf(COLOR_GREEN
                   "[POWER] Battery recovered: %.1f%%\n"
                   COLOR_RESET, batt);

            power_send_fault("INFO",
                             batt,
                             BATTERY_WARNING,
                             "Battery recovered to normal level");
        }

        pthread_mutex_unlock(&g_power_mutex);
    }

    return NULL;
}

/* ─────────────────────────────────────────
   BUS WRITER THREAD
   Copies power data into shared memory
   every second so OBC can read it.
   Uses semaphore to prevent race conditions.
───────────────────────────────────────── */
void *bus_writer_thread(void *arg) {
    (void)arg;

    while (!g_shutdown) {
        sleep(SENSOR_INTERVAL);

        /* Lock semaphore before writing to shared memory */
        sem_wait(g_sem);

        pthread_mutex_lock(&g_power_mutex);
        g_bus->solar_watts  = g_power.solar_watts;
        g_bus->battery_pct  = g_power.battery_pct;
        g_bus->safe_mode    = g_safe_mode;
        get_timestamp(g_bus->timestamp, sizeof(g_bus->timestamp));
        pthread_mutex_unlock(&g_power_mutex);

        /* Unlock semaphore after writing */
        sem_post(g_sem);
    }

    return NULL;
}
