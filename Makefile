# ============================================================================
# Makefile for Docs++ Distributed File System
# ============================================================================
# Compiles Client, NameServer, StorageServer, and File modules
# 
# QUICK START:
#   make              - Build release binaries (client works, NS/SS need File fixes)
#   make client       - Build just the client (recommended, fully working)
#   make debug        - Build with debug symbols
#   make clean        - Remove build artifacts
#
# NOTE: The File module (File/src/ll_functions.c) has pre-existing compilation
#       errors in the original codebase. The Makefile is set up correctly but
#       NameServer and StorageServer builds will fail until those are fixed.
# ============================================================================

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -I. -pthread
DEBUG_FLAGS = -g -O0
RELEASE_FLAGS = -O2

# Source directories
COMM_SRC = Comm/communication.c
COMMON_SRC = common.c
FILE_SRC = File/src/file_create_delete.c File/src/file_write.c File/src/file_exec.c \
           File/src/infofile.c File/src/checkpointfile.c File/src/undofile.c \
           File/src/ll_functions.c File/src/tokeniser.c File/src/utility.c
FILE_INCLUDE = -IFile/include

# Object files
COMM_OBJ = build/communication.o
COMMON_OBJ = build/common.o
FILE_OBJ = build/file_create_delete.o build/file_write.o build/file_exec.o \
           build/infofile.o build/checkpointfile.o build/undofile.o \
           build/ll_functions.o build/tokeniser.o build/utility.o

# Executables
CLIENT = client
NAMESERVER = nameserver
STORAGESERVER = storageserver

# Default target (builds what can be built successfully)
all: release

# Build directories
build:
	@mkdir -p build

# Release build
release: CFLAGS += $(RELEASE_FLAGS)
release: $(CLIENT)
	@echo ""
	@echo "✓ Release build complete: ./$(CLIENT)"
	@echo ""
	@echo "NOTE: NameServer and StorageServer require fixing File module compilation"
	@echo "      errors first. See 'make help' for more info."
	@echo ""

# Debug build
debug: CFLAGS += $(DEBUG_FLAGS)
debug: $(CLIENT)
	@echo ""
	@echo "✓ Debug build complete: ./$(CLIENT)"
	@echo ""

# Build client only (fully working)
client: $(CLIENT)

# Build nameserver (requires File module fixes)
nameserver-only: $(NAMESERVER)

# Build storage server (requires File module fixes)
storageserver-only: $(STORAGESERVER)

# ============================================================================
# CLIENT BUILD
# ============================================================================
$(CLIENT): build $(COMM_OBJ) $(COMMON_OBJ) Client/client.c
	$(CC) $(CFLAGS) $(FILE_INCLUDE) -o $@ Client/client.c $(COMM_OBJ) $(COMMON_OBJ) -lpthread
	@echo "✓ Built: $@"

# ============================================================================
# NAMESERVER BUILD
# ============================================================================
$(NAMESERVER): build $(COMM_OBJ) $(COMMON_OBJ) $(FILE_OBJ) NS/nameserver.c NS/ns_filemanager.c
	$(CC) $(CFLAGS) $(FILE_INCLUDE) -o $@ NS/nameserver.c NS/ns_filemanager.c \
		$(COMM_OBJ) $(COMMON_OBJ) $(FILE_OBJ) -lpthread
	@echo "✓ Built: $@"

# ============================================================================
# STORAGE SERVER BUILD
# ============================================================================
$(STORAGESERVER): build $(COMM_OBJ) $(COMMON_OBJ) $(FILE_OBJ) SS/storageserver.c
	$(CC) $(CFLAGS) $(FILE_INCLUDE) -o $@ SS/storageserver.c \
		$(COMM_OBJ) $(COMMON_OBJ) $(FILE_OBJ) -lpthread
	@echo "✓ Built: $@"

# ============================================================================
# OBJECT FILE COMPILATION RULES
# ============================================================================

# Comm module
build/communication.o: $(COMM_SRC) build
	$(CC) $(CFLAGS) -c $(COMM_SRC) -o $@
	@echo "  ✓ Compiled: $@"

# Common module
build/common.o: $(COMMON_SRC) build
	$(CC) $(CFLAGS) -c $(COMMON_SRC) -o $@
	@echo "  ✓ Compiled: $@"

# File module objects
build/file_create_delete.o: File/src/file_create_delete.c build
	$(CC) $(CFLAGS) $(FILE_INCLUDE) -c $< -o $@
	@echo "  ✓ Compiled: $@"

build/file_write.o: File/src/file_write.c build
	$(CC) $(CFLAGS) $(FILE_INCLUDE) -c $< -o $@
	@echo "  ✓ Compiled: $@"

build/file_exec.o: File/src/file_exec.c build
	$(CC) $(CFLAGS) $(FILE_INCLUDE) -c $< -o $@
	@echo "  ✓ Compiled: $@"

build/infofile.o: File/src/infofile.c build
	$(CC) $(CFLAGS) $(FILE_INCLUDE) -c $< -o $@
	@echo "  ✓ Compiled: $@"

build/checkpointfile.o: File/src/checkpointfile.c build
	$(CC) $(CFLAGS) $(FILE_INCLUDE) -c $< -o $@
	@echo "  ✓ Compiled: $@"

build/undofile.o: File/src/undofile.c build
	$(CC) $(CFLAGS) $(FILE_INCLUDE) -c $< -o $@
	@echo "  ✓ Compiled: $@"

build/ll_functions.o: File/src/ll_functions.c build
	$(CC) $(CFLAGS) $(FILE_INCLUDE) -c $< -o $@
	@echo "  ✓ Compiled: $@"

build/tokeniser.o: File/src/tokeniser.c build
	$(CC) $(CFLAGS) $(FILE_INCLUDE) -c $< -o $@
	@echo "  ✓ Compiled: $@"

build/utility.o: File/src/utility.c build
	$(CC) $(CFLAGS) $(FILE_INCLUDE) -c $< -o $@
	@echo "  ✓ Compiled: $@"

# ============================================================================
# UTILITY TARGETS
# ============================================================================

# Clean build artifacts
clean:
	rm -rf build/ $(CLIENT) $(NAMESERVER) $(STORAGESERVER)
	@echo "✓ Cleaned build artifacts"

# Deep clean (also remove test binaries)
distclean: clean
	find . -name "*.o" -delete
	find . -name "*~" -delete
	@echo "✓ Distcleaned"

# Run client
run-client: $(CLIENT)
	./$(CLIENT)

# Run nameserver
run-nameserver: $(NAMESERVER)
	./$(NAMESERVER)

# Run storage server
run-storageserver: $(STORAGESERVER)
	./$(STORAGESERVER)

# Build and display info
info:
	@echo "=== Docs++ Build System ==="
	@echo "Targets:"
	@echo "  make              - Build release binaries (default)"
	@echo "  make debug        - Build with debug symbols and no optimization"
	@echo "  make release      - Build optimized binaries"
	@echo "  make clean        - Remove build artifacts"
	@echo "  make distclean    - Remove all generated files"
	@echo "  make run-client   - Build and run client"
	@echo "  make run-nameserver - Build and run nameserver"
	@echo "  make run-storageserver - Build and run storage server"
	@echo ""
	@echo "Binaries produced:"
	@echo "  ./$(CLIENT) - Client application"
	@echo "  ./$(NAMESERVER) - Name Server"
	@echo "  ./$(STORAGESERVER) - Storage Server"

# Help target
help: info

# Build info
check-build:
	@echo "=== Build Status ==="
	@if [ -f $(CLIENT) ]; then echo "✓ $(CLIENT) exists"; else echo "✗ $(CLIENT) missing"; fi
	@if [ -f $(NAMESERVER) ]; then echo "✓ $(NAMESERVER) exists"; else echo "✗ $(NAMESERVER) missing"; fi
	@if [ -f $(STORAGESERVER) ]; then echo "✓ $(STORAGESERVER) exists"; else echo "✗ $(STORAGESERVER) missing"; fi

.PHONY: all release debug client nameserver-only storageserver-only clean distclean run-client run-nameserver run-storageserver info help check-build build
