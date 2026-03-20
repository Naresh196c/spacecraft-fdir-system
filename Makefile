# ══════════════════════════════════════════════════════════════
#  Spacecraft FDIR System — Makefile
#  Usage:
#    make all    → build everything
#    make run    → run the simulation
#    make clean  → delete all binaries
#    make test   → run a quick 30 second test
# ══════════════════════════════════════════════════════════════

CC      = gcc
CFLAGS  = -Wall -Wextra -g -I.
LDFLAGS = -lpthread -lrt

# ── Output directories ────────────────────────────────────────
BIN_DIR = bin

# ── Signal object (shared by all binaries) ───────────────────
SIG_OBJ = signals/signal_handler.o

# ── IPC objects (shared by all binaries) ─────────────────────
IPC_OBJS = ipc/shm_manager.o \
           ipc/msgq_manager.o \
           ipc/fifo_manager.o

# ── All binaries to build ─────────────────────────────────────
TARGETS = $(BIN_DIR)/obc     \
          $(BIN_DIR)/power   \
          $(BIN_DIR)/thermal \
          $(BIN_DIR)/comms   \
          $(BIN_DIR)/logger

# ══════════════════════════════════════════════════════════════
#  DEFAULT TARGET — build everything
# ══════════════════════════════════════════════════════════════
all: $(BIN_DIR) $(TARGETS)
	@echo ""
	@echo "✅ Build complete. All binaries in ./bin/"
	@echo "   Run with: make run"
	@echo ""

# ── Create bin/ directory ─────────────────────────────────────
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# ══════════════════════════════════════════════════════════════
#  COMPILE SHARED OBJECTS
# ══════════════════════════════════════════════════════════════

signals/signal_handler.o: signals/signal_handler.c \
                           signals/signal_handler.h \
                           common.h
	$(CC) $(CFLAGS) -c signals/signal_handler.c \
	    -o signals/signal_handler.o

ipc/shm_manager.o: ipc/shm_manager.c ipc/ipc.h common.h
	$(CC) $(CFLAGS) -c ipc/shm_manager.c \
	    -o ipc/shm_manager.o

ipc/msgq_manager.o: ipc/msgq_manager.c ipc/ipc.h common.h
	$(CC) $(CFLAGS) -c ipc/msgq_manager.c \
	    -o ipc/msgq_manager.o

ipc/fifo_manager.o: ipc/fifo_manager.c ipc/ipc.h common.h
	$(CC) $(CFLAGS) -c ipc/fifo_manager.c \
	    -o ipc/fifo_manager.o

# ══════════════════════════════════════════════════════════════
#  BUILD EACH BINARY
# ══════════════════════════════════════════════════════════════

# ── OBC Supervisor (main) ─────────────────────────────────────
$(BIN_DIR)/obc: main.c $(IPC_OBJS) $(SIG_OBJ) common.h
	$(CC) $(CFLAGS) main.c \
	    $(IPC_OBJS) $(SIG_OBJ) \
	    -o $(BIN_DIR)/obc \
	    $(LDFLAGS)
	@echo "  [BUILD] obc ✓"

# ── Power Subsystem ───────────────────────────────────────────
$(BIN_DIR)/power: power/power.c power/power_threads.c \
                  power/power.h \
                  $(IPC_OBJS) $(SIG_OBJ) common.h
	$(CC) $(CFLAGS) power/power.c power/power_threads.c \
	    $(IPC_OBJS) $(SIG_OBJ) \
	    -o $(BIN_DIR)/power \
	    $(LDFLAGS)
	@echo "  [BUILD] power ✓"

# ── Thermal Subsystem ─────────────────────────────────────────
$(BIN_DIR)/thermal: thermal/thermal.c thermal/thermal_threads.c \
                    thermal/thermal.h \
                    $(IPC_OBJS) $(SIG_OBJ) common.h
	$(CC) $(CFLAGS) thermal/thermal.c thermal/thermal_threads.c \
	    $(IPC_OBJS) $(SIG_OBJ) \
	    -o $(BIN_DIR)/thermal \
	    $(LDFLAGS)
	@echo "  [BUILD] thermal ✓"

# ── COMMS Subsystem ───────────────────────────────────────────
$(BIN_DIR)/comms: comms/comms.c comms/comms_threads.c \
                  comms/comms.h \
                  $(IPC_OBJS) $(SIG_OBJ) common.h
	$(CC) $(CFLAGS) comms/comms.c comms/comms_threads.c \
	    $(IPC_OBJS) $(SIG_OBJ) \
	    -o $(BIN_DIR)/comms \
	    $(LDFLAGS)
	@echo "  [BUILD] comms ✓"

# ── Logger ────────────────────────────────────────────────────
$(BIN_DIR)/logger: logger/logger.c logger/file_io.c \
                   logger/logger.h \
                   $(IPC_OBJS) $(SIG_OBJ) common.h
	$(CC) $(CFLAGS) logger/logger.c logger/file_io.c \
	    $(IPC_OBJS) $(SIG_OBJ) \
	    -o $(BIN_DIR)/logger \
	    $(LDFLAGS)
	@echo "  [BUILD] logger ✓"

# ══════════════════════════════════════════════════════════════
#  RUN
# ══════════════════════════════════════════════════════════════
run: all
	@echo ""
	@echo "🚀 Launching APEX-SAT-01..."
	@echo "   Terminal 2 commands:"
	@echo "   echo STATUS     > /tmp/groundstation"
	@echo "   echo SAFE_MODE  > /tmp/groundstation"
	@echo "   echo SNAPSHOT   > /tmp/groundstation"
	@echo ""
	./$(BIN_DIR)/obc

# ══════════════════════════════════════════════════════════════
#  TEST — 30 second automated test
# ══════════════════════════════════════════════════════════════
test: all
	@echo "🧪 Running 30 second test..."
	timeout 30 ./$(BIN_DIR)/obc || true
	@echo ""
	@echo "✅ Test complete. Check fdr.log for entries."

# ══════════════════════════════════════════════════════════════
#  CLEAN — remove all build artifacts
# ══════════════════════════════════════════════════════════════
clean:
	@echo "🧹 Cleaning build artifacts..."
	rm -f $(BIN_DIR)/obc
	rm -f $(BIN_DIR)/power
	rm -f $(BIN_DIR)/thermal
	rm -f $(BIN_DIR)/comms
	rm -f $(BIN_DIR)/logger
	rm -f signals/signal_handler.o
	rm -f ipc/shm_manager.o
	rm -f ipc/msgq_manager.o
	rm -f ipc/fifo_manager.o
	rm -f fdr.log
	rm -f /tmp/groundstation
	@echo "✅ Clean complete."

# ══════════════════════════════════════════════════════════════
#  PHONY TARGETS
# ══════════════════════════════════════════════════════════════
.PHONY: all run test clean
