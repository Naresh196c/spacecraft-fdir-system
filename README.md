# Spacecraft Onboard Fault Detection, Isolation & Recovery (FDIR) System

## Overview

The **Spacecraft FDIR System** is a multi-process Linux application that simulates
a real-time fault detection and recovery system for a spacecraft managing
3 critical subsystems — Power, Thermal, and Communication.

The system simulates sensor data, monitors thresholds, detects faults automatically,
enters Safe Mode during emergencies, and recovers autonomously.

This project demonstrates several **core Operating System concepts**, including:

- Process creation and management
- Multithreading
- Inter-Process Communication (IPC)
- Signal handling
- File logging
- Modular software design

---

## Architecture Diagram

<img width="1536" height="1024" alt="diagram" src="https://github.com/user-attachments/assets/650029b0-c260-4126-828d-de4f77a7f3ed" />


---

## System Workflow

### 1. OBC Supervisor (main.c)

The **OBC Supervisor** is the parent process that manages everything.

Responsibilities:
- Forks all 4 subsystem processes using fork() + exec()
- Runs a watchdog every 2 seconds reading shared memory
- Detects critical faults and triggers Safe Mode via SIGUSR1
- Reaps all children using waitpid() on shutdown

---

### 2. Power Subsystem

The **Power process** simulates solar panels and battery.

Threads:
- solar_thread — generates solar panel output (90–130W)
- battery_thread — drains or charges battery based on eclipse
- bus_writer_thread — writes power data to shared memory

At **t=10s** eclipse begins — solar drops to 0W, battery drains fast.
When battery hits critical → sends CRITICAL alert to message queue.

---

### 3. Thermal Subsystem

The **Thermal process** simulates component temperatures.

Threads:
- sensor_thread — generates CPU temperature
- heater_thread — turns heater ON if temperature drops below 5°C
- bus_writer_thread — writes thermal data to shared memory

At **t=30s** CPU temperature spikes to 110°C automatically.
System cools down 5°C per second after spike.

---

### 4. COMMS Subsystem

The **COMMS process** handles ground station communication.

Threads:
- uplink_thread — reads commands from FIFO (Terminal 2)
- downlink_thread — simulates signal quality, sends periodic snapshots

Accepts commands from Terminal 2:
```
STATUS    → print current readings
SAFE_MODE → manually trigger safe mode
SNAPSHOT  → save current state to fdr.log
```

---

### 5. Logger

The **Logger process** records all fault events to fdr.log.

- Reads fault alerts from message queue
- Writes structured binary entries using open/write/lseek/close
- CRITICAL faults always logged even in Safe Mode

Example log output:
```
Entry #1 | WARNING  | POWER   | Battery low
Entry #2 | CRITICAL | POWER   | Battery critically low
Entry #3 | CRITICAL | THERMAL | CPU overheat
Entry #4 | INFO     | COMMS   | Periodic telemetry snapshot
```

---

## Technologies Used

| Component | Technology |
|---|---|
| Language | C |
| Build System | Makefile |
| Threading | POSIX pthread |
| IPC | Shared Memory, Message Queue, FIFO |
| Synchronization | POSIX Semaphores, Mutex |
| Signal Handling | sigaction |
| Logging | File I/O (open/write/lseek/close) |

---

## System Calls Used

| System Call | Purpose |
|---|---|
| fork() | Create 4 child processes |
| exec() | Replace each child with its binary |
| waitpid() | Reap all children on shutdown |
| pthread_create() | Create sensor and writer threads |
| pthread_mutex_lock() | Protect shared data in each subsystem |
| shm_open() | Create shared memory telemetry bus |
| mmap() | Map shared memory into process |
| sem_open() | Create semaphore to protect shared memory |
| mq_open() | Create priority message queue |
| mq_send() | Send fault alerts with priority |
| mq_receive() | Logger receives alerts |
| mkfifo() | Create FIFO for ground commands |
| sigaction() | Install signal handlers |
| open() | Create fdr.log |
| write() | Write fault entries to log |
| lseek() | Update entry count in log header |
| read() | Read log header on startup |
| close() | Close log file on shutdown |

---

## Signal Handling

| Signal | Purpose |
|---|---|
| SIGINT | Graceful shutdown of all processes |
| SIGUSR1 | Trigger Safe Mode in all subsystems |
| SIGUSR2 | Exit Safe Mode in all subsystems |
| SIGCHLD | Reap dead child processes |

---

## Project Structure
```
spacecraft_fdir/
│
├── main.c
├── common.h
├── Makefile
│
├── power/
│   ├── power.c
│   ├── power_threads.c
│   └── power.h
│
├── thermal/
│   ├── thermal.c
│   ├── thermal_threads.c
│   └── thermal.h
│
├── comms/
│   ├── comms.c
│   ├── comms_threads.c
│   └── comms.h
│
├── logger/
│   ├── logger.c
│   ├── file_io.c
│   └── logger.h
│
├── ipc/
│   ├── shm_manager.c
│   ├── msgq_manager.c
│   ├── fifo_manager.c
│   └── ipc.h
│
└── signals/
    ├── signal_handler.c
    └── signal_handler.h
```

---

## Build Instructions
```bash
make all
```

---

## Run the System
```bash
make run
```

---

## Terminal 2 Commands
```bash
echo "STATUS"    > /tmp/groundstation
echo "SAFE_MODE" > /tmp/groundstation
echo "SNAPSHOT"  > /tmp/groundstation
```

---

## Clean Build Files
```bash
make clean
```

---

## Read Flight Log
```bash
strings fdr.log
```

---

## Testing Scenarios

### Normal Operation
```
Solar: 90-130W
Battery: 100%
CPU: 28-45°C
Signal: 60-100%
```

### Eclipse + Battery Critical
```
t=10s → Solar: 0W → Battery drains fast
t=17s → Battery < 10% → CRITICAL → Safe Mode
t=20s → Solar back → Battery charges → Safe Mode exits
```

### CPU Overheat
```
t=30s → CPU spikes to 110°C → CRITICAL
t=31s → Cooling begins → 5°C per second
t=38s → CPU back to normal → Safe Mode exits
```

---

## Demo Steps

1. Build
```bash
make all
```

2. Run in Terminal 1
```bash
make run
```

3. Send commands from Terminal 2
```bash
echo "STATUS"    > /tmp/groundstation
echo "SAFE_MODE" > /tmp/groundstation
echo "SNAPSHOT"  > /tmp/groundstation
```

4. Read log
```bash
strings fdr.log
```

5. Shutdown
```
Ctrl + C
```

---

## Reflection

### What Worked
- All 4 IPC mechanisms working together seamlessly
- Automatic fault detection and Safe Mode recovery
- Clean shutdown with zero zombie processes
- Real-time ground station command interface via FIFO

### Challenges Faced
- SIGINT propagating to child processes — fixed using SIG_IGN in children
- Logger blocking on mq_receive() — fixed using mq_timedreceive()
- Safe Mode not exiting in children — fixed using SIGUSR2 broadcast

### What I Learned
- Linux process management using fork/exec/waitpid
- Thread synchronization using mutex
- All 4 IPC mechanisms — Shared Memory, Semaphore, Message Queue, FIFO
- Signal handling using sigaction
- Structured binary file I/O using lseek

---

## Author

**Naresh Chaudhari**
Embedded Linux System Programming | KPIT APEX Lab
