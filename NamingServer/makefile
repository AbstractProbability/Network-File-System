CC = gcc
CFLAGS = -Wall -Wextra -pthread -I./include -I../
SRCDIR = src
INCDIR = include
OBJDIR = obj
BINDIR = bin

# Common files
COMMON_SRC = ../common.c
COMMON_OBJ = $(OBJDIR)/common.o

# NS source files
SOURCES = $(wildcard $(SRCDIR)/*.c)
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o) $(COMMON_OBJ)
TARGET = $(BINDIR)/naming_server
TEST_CLIENT = $(BINDIR)/test_client
TEST_SS = $(BINDIR)/test_ss

# Create necessary directories
$(shell mkdir -p $(OBJDIR) $(BINDIR))

all: $(TARGET) $(TEST_CLIENT) $(TEST_SS)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $^ -o $@
	@echo "Naming Server built successfully!"

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(COMMON_OBJ): $(COMMON_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

$(TEST_CLIENT): test_client.c $(COMMON_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

$(TEST_SS): test_ss.c $(COMMON_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -rf $(OBJDIR) $(BINDIR)
	rm -f ns_log.txt
	@echo "Cleaned build files"

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
