# Integration Guide for Business Logic Layer

## Overview

This guide explains how to integrate the communications layer with your business logic (NS, SS, Client implementations). The communications layer handles ALL network operations - you never touch sockets directly.

## Architecture

```
Your Code (NS/SS/Client Logic)
        ↓ calls
Communications API (comm.h)
        ↓ uses
Message Queue + Socket Layer
        ↓
Network (TCP/IP)
```

## Step-by-Step Integration

### Step 1: Link the Library

In your Makefile:

```makefile
# Link with communications library
ns: ns_main.o ns_logic.o
	$(CC) $^ -L../communications -lcomm -pthread -o $@

# Include path
CFLAGS = -I../communications -pthread
```

### Step 2: Include the Header

In your source files:

```c
#include "communications/comm.h"
```

### Step 3: Initialize in main()

```c
int main(int argc, char* argv[]) {
    // Parse command line arguments for port
    uint16_t port = atoi(argv[1]);
    
    // Initialize your business logic
    ns_logic_init();
    
    // Initialize communications layer
    if (comm_init(ENTITY_NS, port) != 0) {
        fprintf(stderr, "Failed to initialize communications\n");
        return 1;
    }
    
    // Set log file
    comm_set_log_file("ns_comm.log");
    
    // Start listening for connections
    if (comm_start_listener(0) != 0) {
        fprintf(stderr, "Failed to start listener\n");
        return 1;
    }
    
    printf("Name Server started on port %d\n", port);
    
    // Your main loop (see Step 4)
    run_main_loop();
    
    // Cleanup
    comm_shutdown();
    ns_logic_cleanup();
    
    return 0;
}
```

### Step 4: Main Message Loop

```c
void run_main_loop(void) {
    int running = 1;
    
    while (running) {
        Message msg;
        
        // Receive next message (5 second timeout)
        int result = comm_receive_message(&msg, 5000);
        
        if (result == 0) {
            // Process message
            handle_message(&msg);
        } else if (result == -2) {
            // Timeout - do periodic tasks
            check_heartbeats();
            cleanup_old_connections();
        } else {
            // Error
            fprintf(stderr, "Error receiving message\n");
        }
    }
}
```

### Step 5: Message Handler

```c
void handle_message(Message* msg) {
    // Log the message
    printf("Received: %s from %s [%s]\n", 
           operation_to_string(msg->operation),
           msg->username,
           msg->source_ip);
    
    // Dispatch based on operation
    switch (msg->operation) {
        case OP_CREATE:
            handle_create(msg);
            break;
            
        case OP_READ:
            handle_read(msg);
            break;
            
        case OP_WRITE:
            handle_write(msg);
            break;
            
        case OP_DELETE:
            handle_delete(msg);
            break;
            
        case OP_VIEW:
            handle_view(msg);
            break;
            
        case OP_INFO:
            handle_info(msg);
            break;
            
        case OP_SS_REGISTER:
            handle_ss_register(msg);
            break;
            
        case OP_HEARTBEAT:
            handle_heartbeat(msg);
            break;
            
        default:
            // Unknown operation
            comm_send_error(msg->source_ip, msg->source_port,
                          STATUS_BAD_REQUEST, "Unknown operation");
    }
}
```

### Step 6: Implement Operation Handlers

#### Example: CREATE Operation

```c
void handle_create(Message* msg) {
    // Extract filename from payload
    char filename[MAX_FILEPATH_LEN];
    strncpy(filename, msg->payload, msg->payload_length);
    filename[msg->payload_length] = '\0';
    
    // Check permissions
    if (!has_write_permission(msg->username, filename)) {
        comm_send_error(msg->source_ip, msg->source_port,
                       STATUS_FORBIDDEN, "No write permission");
        return;
    }
    
    // Check if file exists
    if (file_exists(filename)) {
        comm_send_error(msg->source_ip, msg->source_port,
                       STATUS_FILE_EXISTS, "File already exists");
        return;
    }
    
    // Choose SS to create file
    char ss_ip[MAX_IP_LEN];
    uint16_t ss_port;
    if (!choose_storage_server(ss_ip, &ss_port)) {
        comm_send_error(msg->source_ip, msg->source_port,
                       STATUS_SS_UNAVAILABLE, "No storage servers available");
        return;
    }
    
    // Forward create command to SS
    Message ss_msg;
    comm_create_message(OP_CREATE, "NS", filename, strlen(filename), &ss_msg);
    
    if (comm_send_message(ss_ip, ss_port, &ss_msg) != 0) {
        comm_send_error(msg->source_ip, msg->source_port,
                       STATUS_INTERNAL_ERROR, "Failed to contact storage server");
        return;
    }
    
    // Wait for SS response
    Message ss_response;
    int result = comm_receive_from_specific(ss_ip, ss_port, &ss_response, 5000);
    
    if (result == 0 && ss_response.status == STATUS_OK) {
        // Update file index
        add_file_to_index(filename, ss_ip, ss_port, msg->username);
        
        // Send success to client
        comm_send_response(msg->source_ip, msg->source_port,
                          OP_CREATE, "File created successfully", 24);
    } else {
        comm_send_error(msg->source_ip, msg->source_port,
                       STATUS_INTERNAL_ERROR, "Failed to create file");
    }
}
```

#### Example: READ Operation

```c
void handle_read(Message* msg) {
    char filename[MAX_FILEPATH_LEN];
    strncpy(filename, msg->payload, msg->payload_length);
    filename[msg->payload_length] = '\0';
    
    // Check read permission
    if (!has_read_permission(msg->username, filename)) {
        comm_send_error(msg->source_ip, msg->source_port,
                       STATUS_FORBIDDEN, "No read permission");
        return;
    }
    
    // Find SS with this file
    char ss_ip[MAX_IP_LEN];
    uint16_t ss_port;
    
    if (!find_file_location(filename, ss_ip, &ss_port)) {
        comm_send_error(msg->source_ip, msg->source_port,
                       STATUS_NOT_FOUND, "File not found");
        return;
    }
    
    // Send SS info to client for direct connection
    char response[MAX_PAYLOAD_LEN];
    snprintf(response, sizeof(response), "%s:%u", ss_ip, ss_port);
    
    comm_send_response(msg->source_ip, msg->source_port,
                      OP_READ, response, strlen(response));
}
```

#### Example: SS Registration

```c
void handle_ss_register(Message* msg) {
    // Extract SS info from payload
    // Format: "ss_id:client_port:num_files"
    char ss_id[MAX_USERNAME_LEN];
    uint16_t client_port;
    int num_files;
    
    sscanf(msg->payload, "%[^:]:%hu:%d", ss_id, &client_port, &num_files);
    
    // Register SS connection
    comm_register_connection(ss_id, msg->source_ip, msg->source_port, ENTITY_SS);
    
    // Store SS information
    add_storage_server(ss_id, msg->source_ip, msg->source_port, client_port);
    
    // Send ACK
    comm_send_ack(msg->source_ip, msg->source_port, msg->sequence_num);
    
    printf("Storage Server registered: %s at %s:%d\n",
           ss_id, msg->source_ip, msg->source_port);
    
    // Now SS will send file list...
}
```

### Step 7: Client-SS Direct Connection

For operations like READ, WRITE, STREAM, the client connects directly to SS:

**NS Code (sends SS info to client):**
```c
char response[MAX_PAYLOAD_LEN];
snprintf(response, sizeof(response), "%s:%u", ss_ip, ss_port);
comm_send_response(client_ip, client_port, OP_READ, response, strlen(response));
```

**Client Code (receives SS info and connects):**
```c
Message msg;
comm_receive_message(&msg, 5000);

if (msg.operation == OP_READ && msg.status == STATUS_OK) {
    // Parse SS info
    char ss_ip[MAX_IP_LEN];
    uint16_t ss_port;
    sscanf(msg.payload, "%[^:]:%hu", ss_ip, &ss_port);
    
    // Send read request to SS
    Message read_msg;
    comm_create_message(OP_READ, username, filename, strlen(filename), &read_msg);
    comm_send_message(ss_ip, ss_port, &read_msg);
    
    // Receive file data
    Message data_msg;
    comm_receive_from_specific(ss_ip, ss_port, &data_msg, 10000);
    
    // Display file content
    printf("%.*s\n", data_msg.payload_length, data_msg.payload);
}
```

### Step 8: Broadcasting

Send message to multiple SSes (for replication):

```c
// Collect all SS addresses
char* ss_ips[10];
uint16_t ss_ports[10];
int ss_count = 0;

// Get registered SSes
char entity_ids[MAX_CONNECTIONS][MAX_USERNAME_LEN];
int count = comm_list_connections(entity_ids, MAX_CONNECTIONS);

for (int i = 0; i < count; i++) {
    char ip[MAX_IP_LEN];
    uint16_t port;
    
    if (comm_get_connection_info(entity_ids[i], ip, &port) == 0) {
        ss_ips[ss_count] = strdup(ip);
        ss_ports[ss_count] = port;
        ss_count++;
    }
}

// Broadcast backup sync message
Message sync_msg;
comm_create_message(OP_BSS_SYNC, "NS", file_data, data_len, &sync_msg);

int success = comm_broadcast((const char**)ss_ips, ss_ports, ss_count, &sync_msg);
printf("Broadcast to %d servers, %d successful\n", ss_count, success);

// Free allocated memory
for (int i = 0; i < ss_count; i++) {
    free(ss_ips[i]);
}
```

### Step 9: Streaming Large Files

For files larger than MAX_PAYLOAD_LEN (8192 bytes):

**Sender:**
```c
FILE* file = fopen(filepath, "rb");
char chunk[MAX_PAYLOAD_LEN];
size_t bytes_read;

while ((bytes_read = fread(chunk, 1, sizeof(chunk), file)) > 0) {
    int is_last = (bytes_read < sizeof(chunk));
    comm_send_chunk(dest_ip, dest_port, chunk, bytes_read, is_last);
}

fclose(file);
```

**Receiver:**
```c
char buffer[1024 * 1024];  // 1MB buffer
size_t bytes_received;

if (comm_receive_stream(buffer, sizeof(buffer), &bytes_received, 30000) == 0) {
    // Write to file
    FILE* file = fopen(filepath, "wb");
    fwrite(buffer, 1, bytes_received, file);
    fclose(file);
}
```

## Common Patterns

### Pattern 1: Request-Response

```c
// Send request
comm_send_message(dest_ip, dest_port, &request_msg);

// Wait for response
Message response;
comm_receive_from_specific(dest_ip, dest_port, &response, 5000);
```

### Pattern 2: Heartbeat Monitoring

```c
void check_heartbeats(void) {
    char entity_ids[MAX_CONNECTIONS][MAX_USERNAME_LEN];
    int count = comm_list_connections(entity_ids, MAX_CONNECTIONS);
    
    time_t now = time(NULL);
    
    for (int i = 0; i < count; i++) {
        time_t last_hb = get_last_heartbeat_time(entity_ids[i]);
        
        if (now - last_hb > 30) {  // 30 seconds timeout
            printf("WARNING: %s not responding\n", entity_ids[i]);
            handle_server_failure(entity_ids[i]);
        }
    }
}
```

### Pattern 3: Error Handling

```c
int result = comm_send_message(ip, port, &msg);
if (result != 0) {
    // Log error
    fprintf(stderr, "Failed to send to %s:%d: %s\n", ip, port, strerror(errno));
    
    // Try backup
    if (get_backup_server(ip, &port)) {
        result = comm_send_message(ip, port, &msg);
    }
}
```

## Best Practices

1. **Always check return values** - All functions return status codes
2. **Use timeouts** - Prevent infinite blocking on network issues
3. **Register connections** - Track SSes, clients for failure detection
4. **Log everything** - Use comm_set_log_file() for debugging
5. **Handle errors gracefully** - Network failures are normal
6. **Use enums** - Don't use magic numbers for operations/status
7. **Validate messages** - Check operation, username, payload before processing

## Debugging

### Enable Debug Logging

```c
comm_set_debug(1);
comm_set_log_file("debug.log");
```

### Check Statistics

```c
uint64_t sent, received, bytes_s, bytes_r;
int active;
comm_get_stats(&sent, &received, &bytes_s, &bytes_r, &active);

printf("Stats: %lu sent, %lu received, %d active connections\n",
       sent, received, active);
```

### Test with netcat

```bash
# Listen mode
nc -l 5000

# Send mode
nc 127.0.0.1 5000
```

## Complete Example

See `examples/simple_ns.c` for a complete minimal Name Server implementation using the communications layer.

## Troubleshooting

| Issue | Solution |
|-------|----------|
| "Connection refused" | Check if server is running and port is correct |
| "Address already in use" | Port in use, change port or wait/use SO_REUSEADDR |
| Messages not received | Check if listener started with comm_start_listener() |
| Timeout errors | Increase timeout or check network connectivity |
| Queue full | Increase QUEUE_INITIAL_CAPACITY or process messages faster |

## Next Steps

- Study `tests/test_comm.c` for working examples
- Implement your operation handlers one by one
- Test each operation individually before integrating
- Use logging extensively during development
