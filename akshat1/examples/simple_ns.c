/**
 * Simple Name Server Example
 * 
 * This shows the minimal code needed to create a functioning Name Server
 * using the communications layer. Your teammate can use this as a starting point.
 */

#include "../communications/comm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

// Simple file index (in real implementation, use hashmap/trie)
typedef struct {
    char filename[MAX_FILEPATH_LEN];
    char ss_ip[MAX_IP_LEN];
    uint16_t ss_port;
    char owner[MAX_USERNAME_LEN];
} FileEntry;

FileEntry file_index[1000];
int file_count = 0;

// Simple user list
char all_users[100][MAX_USERNAME_LEN];
int user_count = 0;

int running = 1;

void signal_handler(int sig) {
    (void)sig;
    printf("\nShutting down...\n");
    running = 0;
}

void add_user(const char* username) {
    // Check if already exists
    for (int i = 0; i < user_count; i++) {
        if (strcmp(all_users[i], username) == 0) {
            return;  // Already exists
        }
    }
    
    // Add new user
    if (user_count < 100) {
        strncpy(all_users[user_count], username, MAX_USERNAME_LEN - 1);
        user_count++;
        printf("Added user: %s (total: %d)\n", username, user_count);
    }
}

void add_file(const char* filename, const char* ss_ip, uint16_t ss_port, const char* owner) {
    if (file_count < 1000) {
        strncpy(file_index[file_count].filename, filename, MAX_FILEPATH_LEN - 1);
        strncpy(file_index[file_count].ss_ip, ss_ip, MAX_IP_LEN - 1);
        file_index[file_count].ss_port = ss_port;
        strncpy(file_index[file_count].owner, owner, MAX_USERNAME_LEN - 1);
        file_count++;
        printf("Added file: %s on %s:%d (owner: %s)\n", filename, ss_ip, ss_port, owner);
    }
}

FileEntry* find_file(const char* filename) {
    for (int i = 0; i < file_count; i++) {
        if (strcmp(file_index[i].filename, filename) == 0) {
            return &file_index[i];
        }
    }
    return NULL;
}

void handle_view(Message* msg) {
    printf("VIEW request from %s\n", msg->username);
    
    // Build file list
    char response[MAX_PAYLOAD_LEN];
    int offset = 0;
    
    for (int i = 0; i < file_count && offset < MAX_PAYLOAD_LEN - 100; i++) {
        offset += snprintf(response + offset, MAX_PAYLOAD_LEN - offset,
                          "%s\n", file_index[i].filename);
    }
    
    comm_send_response(msg->source_ip, msg->source_port, 
                      OP_VIEW, response, offset);
}

void handle_list(Message* msg) {
    printf("LIST request from %s\n", msg->username);
    
    // Build user list
    char response[MAX_PAYLOAD_LEN];
    int offset = 0;
    
    for (int i = 0; i < user_count && offset < MAX_PAYLOAD_LEN - 100; i++) {
        offset += snprintf(response + offset, MAX_PAYLOAD_LEN - offset,
                          "%s\n", all_users[i]);
    }
    
    comm_send_response(msg->source_ip, msg->source_port, 
                      OP_LIST, response, offset);
}

void handle_create(Message* msg) {
    char filename[MAX_FILEPATH_LEN];
    strncpy(filename, msg->payload, msg->payload_length);
    filename[msg->payload_length] = '\0';
    
    printf("CREATE request from %s for file: %s\n", msg->username, filename);
    
    // Check if file exists
    if (find_file(filename)) {
        comm_send_error(msg->source_ip, msg->source_port,
                       STATUS_FILE_EXISTS, "File already exists");
        return;
    }
    
    // For this example, assume we have one SS at 127.0.0.1:6000
    // In real implementation, choose SS with load balancing
    add_file(filename, "127.0.0.1", 6000, msg->username);
    
    comm_send_response(msg->source_ip, msg->source_port,
                      OP_CREATE, "File created successfully", 24);
}

void handle_read(Message* msg) {
    char filename[MAX_FILEPATH_LEN];
    strncpy(filename, msg->payload, msg->payload_length);
    filename[msg->payload_length] = '\0';
    
    printf("READ request from %s for file: %s\n", msg->username, filename);
    
    // Find file
    FileEntry* file = find_file(filename);
    if (!file) {
        comm_send_error(msg->source_ip, msg->source_port,
                       STATUS_NOT_FOUND, "File not found");
        return;
    }
    
    // Send SS info to client
    char response[MAX_PAYLOAD_LEN];
    snprintf(response, sizeof(response), "%s:%u", file->ss_ip, file->ss_port);
    
    comm_send_response(msg->source_ip, msg->source_port,
                      OP_READ, response, strlen(response));
}

void handle_ss_register(Message* msg) {
    printf("Storage Server registration from %s:%d\n", 
           msg->source_ip, msg->source_port);
    
    // Register connection
    comm_register_connection(msg->username, msg->source_ip, 
                            msg->source_port, ENTITY_SS);
    
    // Send ACK
    comm_send_ack(msg->source_ip, msg->source_port, msg->sequence_num);
    
    printf("Storage Server %s registered successfully\n", msg->username);
}

void handle_heartbeat(Message* msg) {
    // Just log it (communications layer already tracks it)
    if (msg->payload_length > 0) {
        printf("Heartbeat from %s\n", msg->username);
    }
    
    // Send ACK
    comm_send_ack(msg->source_ip, msg->source_port, msg->sequence_num);
}

void process_message(Message* msg) {
    switch (msg->operation) {
        case OP_VIEW:
            handle_view(msg);
            break;
            
        case OP_LIST:
            handle_list(msg);
            break;
            
        case OP_CREATE:
            handle_create(msg);
            break;
            
        case OP_READ:
            handle_read(msg);
            break;
            
        case OP_SS_REGISTER:
            handle_ss_register(msg);
            break;
            
        case OP_HEARTBEAT:
            handle_heartbeat(msg);
            break;
            
        default:
            printf("Unknown operation: %s\n", operation_to_string(msg->operation));
            comm_send_error(msg->source_ip, msg->source_port,
                           STATUS_BAD_REQUEST, "Unknown operation");
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }
    
    uint16_t port = atoi(argv[1]);
    
    // Setup signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize communications
    printf("Initializing Name Server on port %d...\n", port);
    
    if (comm_init(ENTITY_NS, port) != 0) {
        fprintf(stderr, "Failed to initialize communications\n");
        return 1;
    }
    
    // Set log file
    comm_set_log_file("ns.log");
    
    // Start listener
    if (comm_start_listener(0) != 0) {
        fprintf(stderr, "Failed to start listener\n");
        return 1;
    }
    
    printf("Name Server started successfully!\n");
    printf("Listening on port %d\n", port);
    printf("Press Ctrl+C to stop\n\n");
    
    // Main message loop
    while (running) {
        Message msg;
        
        // Receive message with 5 second timeout
        int result = comm_receive_message(&msg, 5000);
        
        if (result == 0) {
            // Add user if not exists
            if (strlen(msg.username) > 0) {
                add_user(msg.username);
            }
            
            // Process message
            process_message(&msg);
            
        } else if (result == -2) {
            // Timeout - do periodic tasks
            // (In real implementation: check heartbeats, cleanup, etc.)
            
        } else {
            // Error
            if (running) {
                fprintf(stderr, "Error receiving message\n");
            }
        }
    }
    
    // Cleanup
    printf("\nShutting down Name Server...\n");
    comm_shutdown();
    
    // Print statistics
    uint64_t sent, received, bytes_s, bytes_r;
    int active;
    comm_get_stats(&sent, &received, &bytes_s, &bytes_r, &active);
    
    printf("\nStatistics:\n");
    printf("  Messages sent: %lu\n", sent);
    printf("  Messages received: %lu\n", received);
    printf("  Bytes sent: %lu\n", bytes_s);
    printf("  Bytes received: %lu\n", bytes_r);
    printf("  Users registered: %d\n", user_count);
    printf("  Files created: %d\n", file_count);
    
    printf("\nName Server stopped.\n");
    
    return 0;
}
