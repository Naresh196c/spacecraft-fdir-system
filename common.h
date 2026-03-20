#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

/* ─────────────────────────────────────────
   MISSION IDENTITY
───────────────────────────────────────── */
#define MISSION_NAME        "APEX-SAT-01"
#define FDIR_VERSION        "1.0"

/* ─────────────────────────────────────────
   IPC NAMES
───────────────────────────────────────── */
#define SHM_NAME            "/spacecraft_shm"
#define SEM_NAME            "/spacecraft_sem"
#define MQ_NAME             "/spacecraft_mq"
#define FIFO_PATH           "/tmp/groundstation"
#define FDR_LOG_PATH        "fdr.log"

/* ─────────────────────────────────────────
   PROCESS BINARIES
   (exec() will load these)
───────────────────────────────────────── */
#define POWER_BIN           "./bin/power"
#define THERMAL_BIN         "./bin/thermal"
#define ADCS_BIN            "./bin/adcs"
#define COMMS_BIN           "./bin/comms"
#define LOGGER_BIN          "./bin/logger"

/* ─────────────────────────────────────────
   SIMULATION TIMING
───────────────────────────────────────── */
#define SENSOR_INTERVAL     2       /* seconds between sensor reads  */
#define WATCHDOG_INTERVAL   2       /* seconds between watchdog ticks */
#define ECLIPSE_START       10      /* seconds when battery crisis starts */
#define ECLIPSE_END         20      /* seconds when solar panels recover */
#define THERMAL_SPIKE       30      /* seconds when CPU overheats */
#define ADCS_TUMBLE         50     /* seconds when satellite tumbles */

/* ─────────────────────────────────────────
   POWER THRESHOLDS
───────────────────────────────────────── */
#define BATTERY_WARNING     20.0f   /* % — send WARNING alert */
#define BATTERY_CRITICAL    10.0f   /* % — trigger SAFE MODE  */
#define SOLAR_MIN_WATTS     50.0f   /* W  — below this = eclipse */

/* ─────────────────────────────────────────
   THERMAL THRESHOLDS
───────────────────────────────────────── */
#define TEMP_WARNING        85.0f   /* °C — send WARNING alert */
#define TEMP_CRITICAL       95.0f   /* °C — trigger cooling    */
#define TEMP_FREEZE         5.0f    /* °C — turn heater ON     */

/* ─────────────────────────────────────────
   ADCS THRESHOLDS
───────────────────────────────────────── */
#define SPIN_WARNING        5.0f    /* deg/s — send WARNING    */
#define SPIN_CRITICAL       10.0f   /* deg/s — TUMBLE detected */

/* ─────────────────────────────────────────
   COMMS THRESHOLDS
───────────────────────────────────────── */
#define SIGNAL_WARNING      30.0f   /* % — weak signal warning */
#define SIGNAL_CRITICAL     10.0f   /* % — link lost           */

/* ─────────────────────────────────────────
   FAULT FLAGS (bitmask used in shared mem)
───────────────────────────────────────── */
#define FAULT_NONE          0x00
#define FAULT_POWER         0x01
#define FAULT_THERMAL       0x02
#define FAULT_ADCS          0x04
#define FAULT_COMMS         0x08

/* ─────────────────────────────────────────
   SAFE MODE FLAG
───────────────────────────────────────── */
#define MODE_NORMAL         0
#define MODE_SAFE           1

/* ─────────────────────────────────────────
   TELEMETRY BUS STRUCT
   Lives in shared memory.
   All 4 subsystems write here.
   OBC reads from here.
───────────────────────────────────────── */
typedef struct {
    /* Power data */
    float   battery_pct;        /* battery percentage 0-100      */
    float   solar_watts;        /* solar panel output in Watts   */

    /* Thermal data */
    float   temp_cpu;           /* CPU board temperature °C      */
    float   temp_battery;       /* Battery pack temperature °C   */
    float   temp_antenna;       /* Antenna temperature °C        */
    float   temp_body;          /* Outer body temperature °C     */

    /* ADCS data */
    float   spin_rate;          /* rotation rate in deg/s        */
    int     adcs_mode;          /* 0=normal 1=safe 2=tumble      */

    /* COMMS data */
    float   signal_quality;     /* link quality 0-100%           */
    int     uplink_active;      /* 1=receiving commands          */

    /* System status */
    int     fault_flags;        /* bitmask of active faults      */
    int     safe_mode;          /* 0=normal 1=safe mode active   */
    char    timestamp[32];      /* last update time              */
} telemetry_bus_t;

/* ─────────────────────────────────────────
   FAULT REPORT STRUCT
   Sent through message queue.
   Logger saves this to fdr.log.
───────────────────────────────────────── */
typedef struct {
    char    subsystem[16];      /* "POWER" "THERMAL" "ADCS" "COMMS" */
    char    severity[12];       /* "CRITICAL" "WARNING" "INFO"       */
    float   value;              /* the value that caused the fault   */
    float   threshold;          /* the threshold that was crossed    */
    char    description[64];    /* human readable description        */
    char    timestamp[32];      /* when this fault occurred          */
} fault_report_t;

/* ─────────────────────────────────────────
   FDR LOG HEADER STRUCT
   Written at byte 0 of fdr.log
   lseek() jumps here to update count
───────────────────────────────────────── */
typedef struct {
    char    magic[8];           /* "FDRLOG\0" — identifies file  */
    char    mission[16];        /* mission name                  */
    int     version;            /* log format version            */
    int     entry_count;        /* total entries written so far  */
    char    created_at[32];     /* when log was first created    */
    char    reserved[52];       /* padding to make header 128B   */
} fdr_header_t;

/* ─────────────────────────────────────────
   FDR LOG ENTRY STRUCT
   Each fault entry is exactly 128 bytes
   Written after the header
───────────────────────────────────────── */
typedef struct {
    int     entry_id;           /* sequential entry number       */
    char    subsystem[16];      /* which subsystem               */
    char    severity[12];       /* CRITICAL / WARNING / INFO     */
    float   value;              /* sensor value at fault time    */
    float   threshold;          /* threshold that was crossed    */
    char    description[64];    /* fault description             */
    char    timestamp[32];      /* when fault occurred           */
} fdr_entry_t;

/* ─────────────────────────────────────────
   HELPER: GET CURRENT TIMESTAMP
───────────────────────────────────────── */
static inline void get_timestamp(char *buf, int size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", t);
}

/* ─────────────────────────────────────────
   HELPER: PRINT COLORED OUTPUT
───────────────────────────────────────── */
#define COLOR_RED       ""
#define COLOR_YELLOW    ""
#define COLOR_GREEN     ""
#define COLOR_CYAN      ""
#define COLOR_MAGENTA   ""
#define COLOR_BLUE      ""
#define COLOR_RESET     ""

#endif /* COMMON_H */
