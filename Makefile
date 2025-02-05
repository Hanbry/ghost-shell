CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c11
DEBUG_FLAGS = -g -O0
RELEASE_FLAGS = -O2

LIBS = -ledit -lcurl

SRC_DIR = src
INC_DIR = include
BUILD_DIR = build
BIN_DIR = bin

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
TARGET = $(BIN_DIR)/ghost-shell

.PHONY: all clean debug release

all: release

debug: CFLAGS += $(DEBUG_FLAGS)
debug: $(TARGET)

release: CFLAGS += $(RELEASE_FLAGS)
release: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(OBJS) -o $(TARGET) $(LIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) 