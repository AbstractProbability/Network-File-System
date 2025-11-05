# Makefile for Communications Layer
CC = gcc
CFLAGS = -Wall -Wextra -g -pthread -I.
LDFLAGS = -pthread

# Directories
COMMON_DIR = common
COMM_DIR = communications

# Source files
COMMON_SRC = $(COMMON_DIR)/message.c $(COMMON_DIR)/message_queue.c
COMM_SRC = $(COMM_DIR)/comm.c

# Object files
COMMON_OBJ = $(COMMON_SRC:.c=.o)
COMM_OBJ = $(COMM_SRC:.c=.o)
ALL_OBJ = $(COMMON_OBJ) $(COMM_OBJ)

# Targets
.PHONY: all clean test

all: libcomm.a test_comm

# Build static library
libcomm.a: $(ALL_OBJ)
	ar rcs $@ $^
	@echo "Built communications library: libcomm.a"

# Compile object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Test program
test_comm: tests/test_comm.c libcomm.a
	$(CC) $(CFLAGS) $< -L. -lcomm $(LDFLAGS) -o $@
	@echo "Built test program: test_comm"

# Clean
clean:
	rm -f $(ALL_OBJ) libcomm.a test_comm
	@echo "Cleaned build artifacts"

# Help
help:
	@echo "Available targets:"
	@echo "  all        - Build library and tests"
	@echo "  libcomm.a  - Build communications library only"
	@echo "  test_comm  - Build test program"
	@echo "  clean      - Remove build artifacts"
	@echo "  help       - Show this help message"
