CC = gcc
CFLAGS = -Wall -Werror -pthread -g

# Directories
INCLUDE_DIR = ./include
NS_DIR = ./ns
SS_DIR = ./ss
CLIENT_DIR = ./user_client
TEST_DIR = ./test

# Sources
NS_SOURCES = $(NS_DIR)/ns.c $(NS_DIR)/ns_sessions.c $(NS_DIR)/ns_index.c
SS_SOURCES = $(SS_DIR)/ss.c $(SS_DIR)/ss_comms.c $(SS_DIR)/ss_ops.c
CLIENT_SOURCES = $(CLIENT_DIR)/client.c
TOKENISER_TEST_SOURCES = $(TEST_DIR)/tokeniserTesting/tokenisertests.c

# Executables
NS_EXEC = nameserver
SS_EXEC = storageserver
CLIENT_EXEC = client
TOKENISER_TEST_EXEC = $(TEST_DIR)/tokeniserTesting/tokenisertests

.PHONY: all clean

all: $(NS_EXEC) $(SS_EXEC) $(CLIENT_EXEC) $(TOKENISER_TEST_EXEC)

$(NS_EXEC): $(NS_SOURCES)
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) $^ -o $@

$(SS_EXEC): $(SS_SOURCES)
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) $^ -o $@

$(CLIENT_EXEC): $(CLIENT_SOURCES)
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) $^ -o $@
	
$(TOKENISER_TEST_EXEC): $(TOKENISER_TEST_SOURCES)
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) $^ -o $@

clean:
	rm -f $(NS_EXEC) $(SS_EXEC) $(CLIENT_EXEC) $(TOKENISER_TEST_EXEC)