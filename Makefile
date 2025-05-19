CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -I./include
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

# List all source files
SRCS = $(SRC_DIR)/main.c \
       $(SRC_DIR)/database.c \
       $(SRC_DIR)/commands.c \
       $(SRC_DIR)/utils.c \
       $(SRC_DIR)/persistence.c

# Generate object file names
OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))

# Main target
TARGET = $(BIN_DIR)/kvstore

all: directories $(TARGET)

# Create necessary directories
directories:
	mkdir -p $(OBJ_DIR) $(BIN_DIR)

# Link object files to create executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Compile source files into object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

# Phony targets
.PHONY: all clean directories