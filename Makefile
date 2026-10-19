CC      := gcc
CFLAGS  := -Wall -Wextra -Wpedantic -g -Iinclude -pthread
BIN_DIR := bin
SRC_DIR := src
LOG_DIR := logs

SERVER_TARGET := $(BIN_DIR)/server
CLIENT_TARGET := $(BIN_DIR)/client

SERVER_SRCS := $(SRC_DIR)/server.c \
               $(SRC_DIR)/worker.c \
               $(SRC_DIR)/fifo_ipc.c \
               $(SRC_DIR)/msg_queue.c \
               $(SRC_DIR)/shared_memory.c \
               $(SRC_DIR)/semaphore.c \
               $(SRC_DIR)/logger.c

CLIENT_SRCS := $(SRC_DIR)/client.c \
               $(SRC_DIR)/fifo_ipc.c \
               $(SRC_DIR)/logger.c

.PHONY: all clean clean-ipc test help

all: $(SERVER_TARGET) $(CLIENT_TARGET)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(LOG_DIR):
	mkdir -p $(LOG_DIR)

$(SERVER_TARGET): $(SERVER_SRCS) | $(BIN_DIR) $(LOG_DIR)
	$(CC) $(CFLAGS) $(SERVER_SRCS) -o $@

$(CLIENT_TARGET): $(CLIENT_SRCS) | $(BIN_DIR) $(LOG_DIR)
	$(CC) $(CFLAGS) $(CLIENT_SRCS) -o $@

clean:
	rm -rf $(BIN_DIR) *.o /tmp/ipc_chat_*.fifo
	@echo "Cleaned build artifacts and temporary FIFOs."

clean-ipc:
	@echo "Cleaning up leftover System V IPC resources..."
	@ipcs -q | grep -E "^0x" | awk '{print $$2}' | xargs -r ipcrm -q 2>/dev/null || true
	@ipcs -m | grep -E "^0x" | awk '{print $$2}' | xargs -r ipcrm -m 2>/dev/null || true
	@ipcs -s | grep -E "^0x" | awk '{print $$2}' | xargs -r ipcrm -s 2>/dev/null || true
	@rm -f /tmp/ipc_chat_*.fifo
	@echo "All IPC resources wiped clean."

help:
	@echo "Available targets:"
	@echo "  make all       : Build both server and client binaries"
	@echo "  make clean     : Remove compiled binaries and temp FIFOs"
	@echo "  make clean-ipc : Remove all leftover IPC resources (queues, shm, sem)"
	@echo "  make help      : Display this help message"
