#include "comm.h"
#include "../common/message_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <stdarg.h>

// Configuration
#define MAX_CONNECTIONS 1024
#define SOCKET_TIMEOUT_SEC 30
#define LISTENER_BACKLOG 128
#define LOG_BUFFER_SIZE 2048

// Connection registry entry
typedef struct ConnectionEntry {
    char entity_id[MAX_USERNAME_LEN];
    char ip[MAX_IP_LEN];
    uint16_t port;
    EntityType type;
    time_t last_heartbeat;
    int active;
} ConnectionEntry;

// Global state
static struct {
    int initialized;
    int running;
    EntityType role;
    uint16_t listen_port;
    int listen_socket;
    
    // Message queue
    MessageQueue message_queue;
    
    // Connection registry
    ConnectionEntry connections[MAX_CONNECTIONS];
    int connection_count;
    pthread_mutex_t connections_lock;
    
    // Listener thread
    pthread_t listener_thread;
    
    // Statistics
    uint64_t messages_sent;
    uint64_t messages_received;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    pthread_mutex_t stats_lock;
    
    // Sequence number
    uint32_t next_seq_num;
    pthread_mutex_t seq_lock;
    
    // Logging
    FILE* log_file;
    pthread_mutex_t log_lock;
    int debug_enabled;
    char my_ip[MAX_IP_LEN];
    
} g_comm;

// Forward declarations
static void* listener_thread_func(void* arg);
static void* connection_handler_thread_func(void* arg);
static int safe_send(int sockfd, const void* buffer, size_t length);
static int safe_receive(int sockfd, void* buffer, size_t length, int timeout_ms);
static void comm_log(const char* level, const char* format, ...);
static uint32_t get_next_seq_num(void);
static int create_connection_socket(const char* dest_ip, uint16_t dest_port);
static void get_local_ip(char* ip_buffer);

// ============ Initialization & Lifecycle ============

int comm_init(EntityType role, uint16_t port) {
    if (g_comm.initialized) {
        return 0;  // Already initialized
    }
    
    memset(&g_comm, 0, sizeof(g_comm));
    
    g_comm.role = role;
    g_comm.listen_port = port;
    g_comm.listen_socket = -1;
    g_comm.running = 0;
    g_comm.connection_count = 0;
    g_comm.next_seq_num = 1;
    g_comm.debug_enabled = 1;
    
    // Get local IP
    get_local_ip(g_comm.my_ip);
    
    // Initialize message queue
    if (queue_init(&g_comm.message_queue, QUEUE_INITIAL_CAPACITY) != 0) {
        fprintf(stderr, "Failed to initialize message queue\n");
        return -1;
    }
    
    // Initialize mutexes
    if (pthread_mutex_init(&g_comm.connections_lock, NULL) != 0) {
        queue_destroy(&g_comm.message_queue);
        return -1;
    }
    
    if (pthread_mutex_init(&g_comm.stats_lock, NULL) != 0) {
        pthread_mutex_destroy(&g_comm.connections_lock);
        queue_destroy(&g_comm.message_queue);
        return -1;
    }
    
    if (pthread_mutex_init(&g_comm.seq_lock, NULL) != 0) {
        pthread_mutex_destroy(&g_comm.stats_lock);
        pthread_mutex_destroy(&g_comm.connections_lock);
        queue_destroy(&g_comm.message_queue);
        return -1;
    }
    
    if (pthread_mutex_init(&g_comm.log_lock, NULL) != 0) {
        pthread_mutex_destroy(&g_comm.seq_lock);
        pthread_mutex_destroy(&g_comm.stats_lock);
        pthread_mutex_destroy(&g_comm.connections_lock);
        queue_destroy(&g_comm.message_queue);
        return -1;
    }
    
    // Ignore SIGPIPE
    signal(SIGPIPE, SIG_IGN);
    
    g_comm.initialized = 1;
    
    comm_log("INFO", "Communication layer initialized [Role: %s, Port: %d, IP: %s]",
             entity_type_to_string(role), port, g_comm.my_ip);
    
    return 0;
}

int comm_start_listener(int max_connections) {
    if (!g_comm.initialized) {
        return -1;
    }
    
    if (g_comm.listen_port == 0) {
        // Client-only mode, no listener needed
        return 0;
    }
    
    if (g_comm.running) {
        return 0;  // Already running
    }
    
    // Create listening socket
    g_comm.listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (g_comm.listen_socket < 0) {
        comm_log("ERROR", "Failed to create listening socket: %s", strerror(errno));
        return -1;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(g_comm.listen_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        comm_log("ERROR", "Failed to set SO_REUSEADDR: %s", strerror(errno));
        close(g_comm.listen_socket);
        return -1;
    }
    
    // Bind socket
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(g_comm.listen_port);
    
    if (bind(g_comm.listen_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        comm_log("ERROR", "Failed to bind to port %d: %s", g_comm.listen_port, strerror(errno));
        close(g_comm.listen_socket);
        return -1;
    }
    
    // Listen
    if (listen(g_comm.listen_socket, LISTENER_BACKLOG) < 0) {
        comm_log("ERROR", "Failed to listen: %s", strerror(errno));
        close(g_comm.listen_socket);
        return -1;
    }
    
    g_comm.running = 1;
    
    // Start listener thread
    if (pthread_create(&g_comm.listener_thread, NULL, listener_thread_func, NULL) != 0) {
        comm_log("ERROR", "Failed to create listener thread");
        close(g_comm.listen_socket);
        g_comm.running = 0;
        return -1;
    }
    
    pthread_detach(g_comm.listener_thread);
    
    comm_log("INFO", "Listener started on port %d", g_comm.listen_port);
    
    return 0;
}

void comm_shutdown(void) {
    if (!g_comm.initialized) {
        return;
    }
    
    comm_log("INFO", "Shutting down communication layer");
    
    // Stop listener
    g_comm.running = 0;
    
    // Close listening socket
    if (g_comm.listen_socket >= 0) {
        close(g_comm.listen_socket);
        g_comm.listen_socket = -1;
    }
    
    // Shutdown queue
    queue_shutdown(&g_comm.message_queue);
    
    // Small delay to let threads finish
    usleep(100000);  // 100ms
    
    // Cleanup
    queue_destroy(&g_comm.message_queue);
    pthread_mutex_destroy(&g_comm.connections_lock);
    pthread_mutex_destroy(&g_comm.stats_lock);
    pthread_mutex_destroy(&g_comm.seq_lock);
    pthread_mutex_destroy(&g_comm.log_lock);
    
    if (g_comm.log_file) {
        fclose(g_comm.log_file);
        g_comm.log_file = NULL;
    }
    
    g_comm.initialized = 0;
    
    printf("Communication layer shut down\n");
}

// ============ Message Operations ============

int comm_send_message(const char* dest_ip, uint16_t dest_port, const Message* msg) {
    if (!g_comm.initialized || !msg || !dest_ip) {
        return -1;
    }
    
    // Create connection
    int sockfd = create_connection_socket(dest_ip, dest_port);
    if (sockfd < 0) {
        comm_log("ERROR", "Failed to connect to %s:%d", dest_ip, dest_port);
        return -1;
    }
    
    // Fill in source information
    Message send_msg;
    memcpy(&send_msg, msg, sizeof(Message));
    strncpy(send_msg.source_ip, g_comm.my_ip, MAX_IP_LEN - 1);
    send_msg.source_port = g_comm.listen_port;
    send_msg.source_type = g_comm.role;
    send_msg.timestamp = time(NULL);
    if (send_msg.sequence_num == 0) {
        send_msg.sequence_num = get_next_seq_num();
    }
    
    // Send message
    if (safe_send(sockfd, &send_msg, sizeof(Message)) < 0) {
        comm_log("ERROR", "Failed to send message to %s:%d", dest_ip, dest_port);
        close(sockfd);
        return -1;
    }
    
    close(sockfd);
    
    // Update statistics
    pthread_mutex_lock(&g_comm.stats_lock);
    g_comm.messages_sent++;
    g_comm.bytes_sent += sizeof(Message);
    pthread_mutex_unlock(&g_comm.stats_lock);
    
    comm_log("INFO", "SENT: %s -> %s:%d [%s/%s]",
             entity_type_to_string(g_comm.role), dest_ip, dest_port,
             message_type_to_string(send_msg.type),
             operation_to_string(send_msg.operation));
    
    return 0;
}

int comm_receive_message(Message* msg, int timeout_ms) {
    if (!g_comm.initialized || !msg) {
        return -1;
    }
    
    int result;
    if (timeout_ms == 0) {
        result = queue_dequeue(&g_comm.message_queue, msg);
    } else {
        result = queue_dequeue_timeout(&g_comm.message_queue, msg, timeout_ms);
    }
    
    if (result == 0) {
        comm_log("INFO", "RECV: %s <- %s:%d [%s/%s]",
                 entity_type_to_string(g_comm.role),
                 msg->source_ip, msg->source_port,
                 message_type_to_string(msg->type),
                 operation_to_string(msg->operation));
    }
    
    return result;
}

int comm_receive_from_specific(const char* source_ip, uint16_t source_port, 
                                Message* msg, int timeout_ms) {
    if (!g_comm.initialized || !msg) {
        return -1;
    }
    
    time_t start_time = time(NULL);
    int effective_timeout = timeout_ms;
    
    while (1) {
        Message temp_msg;
        int result = queue_dequeue_timeout(&g_comm.message_queue, &temp_msg, effective_timeout);
        
        if (result < 0) {
            return result;  // Error or timeout
        }
        
        // Check if this message is from the expected source
        int ip_match = (source_ip == NULL || strcmp(temp_msg.source_ip, source_ip) == 0);
        int port_match = (source_port == 0 || temp_msg.source_port == source_port);
        
        if (ip_match && port_match) {
            memcpy(msg, &temp_msg, sizeof(Message));
            return 0;
        }
        
        // Not the message we want, re-enqueue it
        queue_enqueue(&g_comm.message_queue, &temp_msg);
        
        // Update timeout
        if (timeout_ms > 0) {
            time_t elapsed = time(NULL) - start_time;
            effective_timeout = timeout_ms - (elapsed * 1000);
            if (effective_timeout <= 0) {
                return -2;  // Timeout
            }
        }
    }
}

// ============ Convenience Wrappers ============

int comm_send_response(const char* dest_ip, uint16_t dest_port, 
                       OperationID op_id, const char* payload, size_t payload_len) {
    Message msg;
    message_create_response(&msg, STATUS_OK, payload, payload_len);
    msg.operation = op_id;
    return comm_send_message(dest_ip, dest_port, &msg);
}

int comm_send_error(const char* dest_ip, uint16_t dest_port, 
                    StatusCode status, const char* error_msg) {
    Message msg;
    message_create_error(&msg, status, error_msg);
    return comm_send_message(dest_ip, dest_port, &msg);
}

int comm_send_ack(const char* dest_ip, uint16_t dest_port, uint32_t seq_num) {
    Message msg;
    message_create_ack(&msg, seq_num);
    return comm_send_message(dest_ip, dest_port, &msg);
}

int comm_broadcast(const char** ips, const uint16_t* ports, int count, const Message* msg) {
    if (!ips || !ports || count <= 0 || !msg) {
        return 0;
    }
    
    int success_count = 0;
    for (int i = 0; i < count; i++) {
        if (comm_send_message(ips[i], ports[i], msg) == 0) {
            success_count++;
        }
    }
    
    comm_log("INFO", "Broadcast to %d destinations, %d succeeded", count, success_count);
    
    return success_count;
}

// ============ Streaming Support ============

int comm_send_chunk(const char* dest_ip, uint16_t dest_port, 
                    const char* chunk_data, size_t chunk_size, int is_last) {
    if (!chunk_data || chunk_size == 0 || chunk_size > MAX_PAYLOAD_LEN) {
        return -1;
    }
    
    Message msg;
    message_init(&msg);
    msg.type = MSG_TYPE_DATA_CHUNK;
    msg.operation = OP_NONE;
    msg.status = STATUS_OK;
    msg.payload_length = chunk_size;
    memcpy(msg.payload, chunk_data, chunk_size);
    
    if (is_last) {
        msg.flags |= MSG_FLAG_IS_LAST_CHUNK;
    }
    
    return comm_send_message(dest_ip, dest_port, &msg);
}

int comm_receive_stream(char* buffer, size_t buffer_size, 
                        size_t* bytes_received, int timeout_ms) {
    if (!buffer || buffer_size == 0 || !bytes_received) {
        return -1;
    }
    
    *bytes_received = 0;
    
    while (*bytes_received < buffer_size) {
        Message msg;
        int result = comm_receive_message(&msg, timeout_ms);
        
        if (result < 0) {
            return result;
        }
        
        if (msg.type != MSG_TYPE_DATA_CHUNK) {
            // Re-enqueue non-chunk message
            queue_enqueue(&g_comm.message_queue, &msg);
            continue;
        }
        
        // Copy chunk data
        size_t copy_size = msg.payload_length;
        if (*bytes_received + copy_size > buffer_size) {
            copy_size = buffer_size - *bytes_received;
        }
        
        memcpy(buffer + *bytes_received, msg.payload, copy_size);
        *bytes_received += copy_size;
        
        // Check if this was the last chunk
        if (msg.flags & MSG_FLAG_IS_LAST_CHUNK) {
            break;
        }
    }
    
    return 0;
}

// ============ Connection Management ============

int comm_register_connection(const char* entity_id, const char* ip, 
                             uint16_t port, EntityType type) {
    if (!entity_id || !ip) {
        return -1;
    }
    
    pthread_mutex_lock(&g_comm.connections_lock);
    
    // Check if already exists
    for (int i = 0; i < g_comm.connection_count; i++) {
        if (g_comm.connections[i].active &&
            strcmp(g_comm.connections[i].entity_id, entity_id) == 0) {
            // Update existing entry
            strncpy(g_comm.connections[i].ip, ip, MAX_IP_LEN - 1);
            g_comm.connections[i].port = port;
            g_comm.connections[i].type = type;
            g_comm.connections[i].last_heartbeat = time(NULL);
            pthread_mutex_unlock(&g_comm.connections_lock);
            comm_log("INFO", "Updated connection: %s at %s:%d", entity_id, ip, port);
            return 0;
        }
    }
    
    // Find free slot
    int slot = -1;
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (!g_comm.connections[i].active) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        pthread_mutex_unlock(&g_comm.connections_lock);
        comm_log("ERROR", "Connection registry full");
        return -1;
    }
    
    // Add new connection
    strncpy(g_comm.connections[slot].entity_id, entity_id, MAX_USERNAME_LEN - 1);
    strncpy(g_comm.connections[slot].ip, ip, MAX_IP_LEN - 1);
    g_comm.connections[slot].port = port;
    g_comm.connections[slot].type = type;
    g_comm.connections[slot].last_heartbeat = time(NULL);
    g_comm.connections[slot].active = 1;
    
    if (slot >= g_comm.connection_count) {
        g_comm.connection_count = slot + 1;
    }
    
    pthread_mutex_unlock(&g_comm.connections_lock);
    
    comm_log("INFO", "Registered connection: %s [%s] at %s:%d", 
             entity_id, entity_type_to_string(type), ip, port);
    
    return 0;
}

int comm_unregister_connection(const char* entity_id) {
    if (!entity_id) {
        return -1;
    }
    
    pthread_mutex_lock(&g_comm.connections_lock);
    
    for (int i = 0; i < g_comm.connection_count; i++) {
        if (g_comm.connections[i].active &&
            strcmp(g_comm.connections[i].entity_id, entity_id) == 0) {
            g_comm.connections[i].active = 0;
            pthread_mutex_unlock(&g_comm.connections_lock);
            comm_log("INFO", "Unregistered connection: %s", entity_id);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_comm.connections_lock);
    return -1;
}

int comm_get_connection_info(const char* entity_id, char* ip, uint16_t* port) {
    if (!entity_id || !ip || !port) {
        return -1;
    }
    
    pthread_mutex_lock(&g_comm.connections_lock);
    
    for (int i = 0; i < g_comm.connection_count; i++) {
        if (g_comm.connections[i].active &&
            strcmp(g_comm.connections[i].entity_id, entity_id) == 0) {
            strncpy(ip, g_comm.connections[i].ip, MAX_IP_LEN - 1);
            *port = g_comm.connections[i].port;
            pthread_mutex_unlock(&g_comm.connections_lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_comm.connections_lock);
    return -1;
}

int comm_list_connections(char entity_ids[][MAX_USERNAME_LEN], int max_entries) {
    if (!entity_ids || max_entries <= 0) {
        return 0;
    }
    
    pthread_mutex_lock(&g_comm.connections_lock);
    
    int count = 0;
    for (int i = 0; i < g_comm.connection_count && count < max_entries; i++) {
        if (g_comm.connections[i].active) {
            strncpy(entity_ids[count], g_comm.connections[i].entity_id, MAX_USERNAME_LEN - 1);
            count++;
        }
    }
    
    pthread_mutex_unlock(&g_comm.connections_lock);
    
    return count;
}

// ============ Utilities ============

void comm_create_message(OperationID op_id, const char* username, 
                        const char* payload, size_t payload_len, Message* msg) {
    message_create_request(msg, op_id, username, payload, payload_len);
}

void comm_get_stats(uint64_t* msgs_sent, uint64_t* msgs_received,
                    uint64_t* bytes_sent, uint64_t* bytes_received,
                    int* active_connections) {
    pthread_mutex_lock(&g_comm.stats_lock);
    if (msgs_sent) *msgs_sent = g_comm.messages_sent;
    if (msgs_received) *msgs_received = g_comm.messages_received;
    if (bytes_sent) *bytes_sent = g_comm.bytes_sent;
    if (bytes_received) *bytes_received = g_comm.bytes_received;
    pthread_mutex_unlock(&g_comm.stats_lock);
    
    if (active_connections) {
        pthread_mutex_lock(&g_comm.connections_lock);
        int count = 0;
        for (int i = 0; i < g_comm.connection_count; i++) {
            if (g_comm.connections[i].active) count++;
        }
        *active_connections = count;
        pthread_mutex_unlock(&g_comm.connections_lock);
    }
}

void comm_set_debug(int enable) {
    g_comm.debug_enabled = enable;
}

int comm_set_log_file(const char* filepath) {
    if (!filepath) {
        return -1;
    }
    
    pthread_mutex_lock(&g_comm.log_lock);
    
    if (g_comm.log_file) {
        fclose(g_comm.log_file);
    }
    
    g_comm.log_file = fopen(filepath, "a");
    
    pthread_mutex_unlock(&g_comm.log_lock);
    
    if (!g_comm.log_file) {
        return -1;
    }
    
    comm_log("INFO", "Log file set to: %s", filepath);
    return 0;
}

int comm_is_initialized(void) {
    return g_comm.initialized;
}

// ============ Internal Functions ============

static void* listener_thread_func(void* arg) {
    (void)arg;
    
    comm_log("INFO", "Listener thread started");
    
    while (g_comm.running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(g_comm.listen_socket, &read_fds);
        
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        int activity = select(g_comm.listen_socket + 1, &read_fds, NULL, NULL, &tv);
        
        if (activity < 0) {
            if (errno == EINTR) continue;
            comm_log("ERROR", "select() failed: %s", strerror(errno));
            break;
        }
        
        if (activity == 0) {
            // Timeout, continue
            continue;
        }
        
        if (FD_ISSET(g_comm.listen_socket, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            
            int client_fd = accept(g_comm.listen_socket, (struct sockaddr*)&client_addr, &addr_len);
            if (client_fd < 0) {
                comm_log("ERROR", "accept() failed: %s", strerror(errno));
                continue;
            }
            
            char client_ip[MAX_IP_LEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
            uint16_t client_port = ntohs(client_addr.sin_port);
            
            comm_log("INFO", "Accepted connection from %s:%d", client_ip, client_port);
            
            // Create handler thread
            int* sockfd_ptr = malloc(sizeof(int));
            *sockfd_ptr = client_fd;
            
            pthread_t handler_thread;
            if (pthread_create(&handler_thread, NULL, connection_handler_thread_func, sockfd_ptr) != 0) {
                comm_log("ERROR", "Failed to create handler thread");
                close(client_fd);
                free(sockfd_ptr);
            } else {
                pthread_detach(handler_thread);
            }
        }
    }
    
    comm_log("INFO", "Listener thread stopped");
    return NULL;
}

static void* connection_handler_thread_func(void* arg) {
    int sockfd = *(int*)arg;
    free(arg);
    
    // Receive message
    Message msg;
    if (safe_receive(sockfd, &msg, sizeof(Message), SOCKET_TIMEOUT_SEC * 1000) < 0) {
        comm_log("ERROR", "Failed to receive message from connection");
        close(sockfd);
        return NULL;
    }
    
    // Validate message
    if (!message_validate(&msg)) {
        comm_log("WARN", "Received invalid message");
        close(sockfd);
        return NULL;
    }
    
    // Enqueue message
    if (queue_enqueue(&g_comm.message_queue, &msg) < 0) {
        comm_log("ERROR", "Failed to enqueue message (queue full)");
        close(sockfd);
        return NULL;
    }
    
    // Update statistics
    pthread_mutex_lock(&g_comm.stats_lock);
    g_comm.messages_received++;
    g_comm.bytes_received += sizeof(Message);
    pthread_mutex_unlock(&g_comm.stats_lock);
    
    close(sockfd);
    return NULL;
}

static int safe_send(int sockfd, const void* buffer, size_t length) {
    size_t total_sent = 0;
    const char* ptr = (const char*)buffer;
    
    while (total_sent < length) {
        ssize_t sent = send(sockfd, ptr + total_sent, length - total_sent, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(1000);  // 1ms
                continue;
            }
            return -1;
        }
        if (sent == 0) {
            return -1;  // Connection closed
        }
        total_sent += sent;
    }
    
    return 0;
}

static int safe_receive(int sockfd, void* buffer, size_t length, int timeout_ms) {
    size_t total_received = 0;
    char* ptr = (char*)buffer;
    
    // Set socket timeout
    if (timeout_ms > 0) {
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
    
    while (total_received < length) {
        ssize_t received = recv(sockfd, ptr + total_received, length - total_received, 0);
        if (received < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return -2;  // Timeout
            }
            return -1;
        }
        if (received == 0) {
            return -1;  // Connection closed
        }
        total_received += received;
    }
    
    return 0;
}

static void comm_log(const char* level, const char* format, ...) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    
    char buffer[LOG_BUFFER_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    pthread_mutex_lock(&g_comm.log_lock);
    
    // Write to log file
    if (g_comm.log_file) {
        fprintf(g_comm.log_file, "[%s] [%s] [%s] %s\n", 
                timestamp, level, entity_type_to_string(g_comm.role), buffer);
        fflush(g_comm.log_file);
    }
    
    // Write to console for INFO and above
    if (strcmp(level, "INFO") == 0 || strcmp(level, "WARN") == 0 || strcmp(level, "ERROR") == 0) {
        printf("[%s] [%s] [%s] %s\n", 
               timestamp, level, entity_type_to_string(g_comm.role), buffer);
    }
    
    pthread_mutex_unlock(&g_comm.log_lock);
}

static uint32_t get_next_seq_num(void) {
    pthread_mutex_lock(&g_comm.seq_lock);
    uint32_t seq = g_comm.next_seq_num++;
    pthread_mutex_unlock(&g_comm.seq_lock);
    return seq;
}

static int create_connection_socket(const char* dest_ip, uint16_t dest_port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        return -1;
    }
    
    // Set socket timeout
    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    // Connect
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(dest_port);
    
    if (inet_pton(AF_INET, dest_ip, &server_addr.sin_addr) <= 0) {
        close(sockfd);
        return -1;
    }
    
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(sockfd);
        return -1;
    }
    
    return sockfd;
}

static void get_local_ip(char* ip_buffer) {
    // Simple implementation: just use localhost for now
    // In production, would query network interfaces
    strncpy(ip_buffer, "127.0.0.1", MAX_IP_LEN - 1);
    
    // TODO: Implement proper local IP detection using getifaddrs()
}
