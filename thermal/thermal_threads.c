#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "thermal.h"
#include "../signals/signal_handler.h"

/* ─────────────────────────────────────────
   SENSOR THREAD
   Reads 4 zone temperatures every second.
   At t=THERMAL_SPIKE (90s): CPU spikes to
   simulate an overheat event.
   Normal range: 25°C to 45°C per zone.
───────────────────────────────────────── */
void *sensor_thread(void *arg) {
    (void)arg;
    srand(time(NULL) ^ (getpid() << 4));

    int elapsed = 0;

    while (!g_shutdown) {
        sleep(SENSOR_INTERVAL);
        elapsed++;

        pthread_mutex_lock(&g_thermal_mutex);

        /* Normal random temperatures for all zones */
        g_thermal.temp_battery = (float)(rand() % 15 + 15);  /* 15-30°C  */
        g_thermal.temp_antenna = (float)(rand() % 20 + 20);  /* 20-40°C  */
        g_thermal.temp_body    = (float)(rand() % 15 + 18);  /* 18-33°C  */

        /* CPU temperature — spikes at t=THERMAL_SPIKE */
        if (elapsed == THERMAL_SPIKE) {
            /* Inject overheat anomaly */
            g_thermal.temp_cpu = 110.0f;
        } else if (elapsed > THERMAL_SPIKE &&
                   elapsed < THERMAL_SPIKE + 8) {
            /* Gradual cooling after spike */
            g_thermal.temp_cpu -= 5.0f;
            if (g_thermal.temp_cpu < 35.0f)
                g_thermal.temp_cpu = 35.0f;
        } else {
            /* Normal CPU temp: 28-45°C */
            g_thermal.temp_cpu = (float)(rand() % 18 + 28);
        }

        /* Print temperatures */
        if (g_safe_mode) {
            printf(COLOR_YELLOW
                   "[THERMAL] SAFE MODE | CPU: %.1f°C\n"
                   COLOR_RESET,
                   g_thermal.temp_cpu);
        } else {
                printf("[THERMAL] CPU: %.1f°C\n",
                       g_thermal.temp_cpu);
        }

        /* Check CPU overheat thresholds */
        if (g_thermal.temp_cpu >= TEMP_CRITICAL &&
            !g_thermal.fault_active) {
            g_thermal.fault_active = 1;
            float val = g_thermal.temp_cpu;
            pthread_mutex_unlock(&g_thermal_mutex);

            printf(COLOR_RED
                   "[THERMAL] ⚠⚠ CRITICAL: CPU = %.1f°C\n"
                   COLOR_RESET, val);

            thermal_send_fault("CRITICAL",
                               val,
                               TEMP_CRITICAL,
                               "CPU overheat — cooling required");
            continue;

        } else if (g_thermal.temp_cpu >= TEMP_WARNING &&
                   !g_thermal.fault_active) {
            float val = g_thermal.temp_cpu;
            pthread_mutex_unlock(&g_thermal_mutex);

            printf(COLOR_YELLOW
                   "[THERMAL] ⚠ WARNING: CPU = %.1f°C\n"
                   COLOR_RESET, val);

            thermal_send_fault("WARNING",
                               val,
                               TEMP_WARNING,
                               "CPU temperature elevated");
            continue;

        } else if (g_thermal.temp_cpu < TEMP_WARNING &&
                   g_thermal.fault_active) {
            /* CPU recovered */
            g_thermal.fault_active = 0;
            float val = g_thermal.temp_cpu;
            pthread_mutex_unlock(&g_thermal_mutex);

            printf(COLOR_GREEN
                   "[THERMAL] CPU temperature recovered: %.1f°C\n"
                   COLOR_RESET, val);

            thermal_send_fault("INFO",
                               val,
                               TEMP_WARNING,
                               "CPU temperature back to normal");
            continue;
        }

        pthread_mutex_unlock(&g_thermal_mutex);
    }

    return NULL;
}

/* ─────────────────────────────────────────
   HEATER THREAD
   Monitors all zone temperatures.
   If any zone drops below TEMP_FREEZE (5°C)
   → turns heater ON.
   If all zones above 10°C → heater OFF.
───────────────────────────────────────── */
void *heater_thread(void *arg) {
    (void)arg;

    while (!g_shutdown) {
        sleep(SENSOR_INTERVAL);

        pthread_mutex_lock(&g_thermal_mutex);

        float min_temp = g_thermal.temp_battery;
        if (g_thermal.temp_antenna < min_temp)
            min_temp = g_thermal.temp_antenna;
        if (g_thermal.temp_body < min_temp)
            min_temp = g_thermal.temp_body;

        if (min_temp <= TEMP_FREEZE && !g_thermal.heater_on) {
            g_thermal.heater_on = 1;
            printf(COLOR_YELLOW
                   "[THERMAL] Heater ON  — min zone temp: %.1f°C\n"
                   COLOR_RESET, min_temp);

            thermal_send_fault("WARNING",
                               min_temp,
                               TEMP_FREEZE,
                               "Component near freezing — heater activated");

        } else if (min_temp > 10.0f && g_thermal.heater_on) {
            g_thermal.heater_on = 0;
            printf(COLOR_GREEN
                   "[THERMAL] Heater OFF — temp nominal: %.1f°C\n"
                   COLOR_RESET, min_temp);
        }

        pthread_mutex_unlock(&g_thermal_mutex);
    }

    return NULL;
}

/* ─────────────────────────────────────────
   BUS WRITER THREAD
   Copies thermal data into shared memory
   every second so OBC can read it.
───────────────────────────────────────── */
void *bus_writer_thread(void *arg) {
    (void)arg;

    while (!g_shutdown) {
        sleep(SENSOR_INTERVAL);

        /* Lock semaphore before writing shared memory */
        sem_wait(g_sem);

        pthread_mutex_lock(&g_thermal_mutex);
        g_bus->temp_cpu     = g_thermal.temp_cpu;
        g_bus->temp_battery = g_thermal.temp_battery;
        g_bus->temp_antenna = g_thermal.temp_antenna;
        g_bus->temp_body    = g_thermal.temp_body;
        pthread_mutex_unlock(&g_thermal_mutex);

        /* Unlock semaphore */
        sem_post(g_sem);
    }

    return NULL;
}
