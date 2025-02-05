CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c11
DEBUG_FLAGS = -g -O0
RELEASE_FLAGS = -O2

# Default to libedit (BSD licensed)
LIBS = -ledit

# Optional: Use GNU readline if explicitly requested
ifdef USE_GNU_READLINE
    CFLAGS += -DUSE_GNU_READLINE
    LIBS = -lreadline
endif

SRC_DIR = src
INC_DIR = include
BUILD_DIR = build
BIN_DIR = bin

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
TARGET = $(BIN_DIR)/ghost-shell

.PHONY: all clean debug release readline

all: release

debug: CFLAGS += $(DEBUG_FLAGS)
debug: $(TARGET)

release: CFLAGS += $(RELEASE_FLAGS)
release: $(TARGET)

# Optional target to build with GNU readline
readline: export USE_GNU_READLINE=1
readline: clean all

$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(OBJS) -o $(TARGET) $(LIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) 