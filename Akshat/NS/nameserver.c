#include "../Comm/communication.h"

// GLOBAL VARIABLES

ConnectionRegistry *registry = NULL;
FILE *log_file = NULL;
int ns_port = 5000;

// Heartbeat tracking (thread-safe)
typedef struct {
    char name[100];
    time_t last_heartbeat;
    int is_alive;
} HeartbeatInfo;

HeartbeatInfo heartbeats[MAX_CONNECTIONS];
int heartbeat_count = 0;
pthread_mutex_t heartbeat_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// LOGGING FUNCTION

void ns_log(const char *level, const char *message) {
    pthread_mutex_lock(&log_mutex);
    
    time_t now = time(NULL);
    char *timestamp = ctime(&now);
    timestamp[strlen(timestamp) - 1] = '\0';  // Remove newline
    
    // Print to console
    printf("[%s] %s\n", level, message);
    
    // Write to log file
    if (log_file) {
        fprintf(log_file, "[%s] [%s] %s\n", timestamp, level, message);
        fflush(log_file);
    }
    
    pthread_mutex_unlock(&log_mutex);
}

// Heartbeat code starts here

void send_heartbeats_to_ss() {
    pthread_mutex_lock(&heartbeat_mutex);
    
    for (int i = 0; i < heartbeat_count; i++) {
        if (!heartbeats[i].is_alive) continue;
        
        // Find SS in registry
        Connection *ss_conn = NULL;
        pthread_mutex_lock(&registry->lock);
        for (int j = 0; j < registry->count; j++) {
            if (strcmp(registry->connections[j].type, "STORAGE_SERVER") == 0 &&
                strcmp(registry->connections[j].identifier, heartbeats[i].name) == 0) {
                ss_conn = &registry->connections[j];
                break;
            }
        }
        pthread_mutex_unlock(&registry->lock);
        
        if (!ss_conn) continue;
        
        // Create heartbeat message
        Message *msg = create_message();
        add_string_field(msg, "type", "HEARTBEAT");
        add_number_field(msg, "timestamp", (double)time(NULL));
        
        char *msg_str = serialize_message(msg);
        free_message(msg);
        
        // Send heartbeat
        if (send_message(ss_conn->socket_fd, msg_str) == 0) {
            char log_msg[200];
            snprintf(log_msg, sizeof(log_msg), "Heartbeat sent to %s", heartbeats[i].name);
            ns_log("DEBUG", log_msg);
        }
        
        free(msg_str);
    }
    
    pthread_mutex_unlock(&heartbeat_mutex);
}

void handle_heartbeat_response(const char *message) {
    Message *msg = parse_message(message);
    if (!msg) return;
    
    char *ss_name = get_string_field(msg, "name");
    if (!ss_name) {
        free_message(msg);
        return;
    }
    
    pthread_mutex_lock(&heartbeat_mutex);
    
    // Update heartbeat info
    for (int i = 0; i < heartbeat_count; i++) {
        if (strcmp(heartbeats[i].name, ss_name) == 0) {
            heartbeats[i].last_heartbeat = time(NULL);
            heartbeats[i].is_alive = 1;
            
            char log_msg[200];
            snprintf(log_msg, sizeof(log_msg), "Heartbeat response from %s: OK", ss_name);
            ns_log("INFO", log_msg);
            break;
        }
    }
    
    pthread_mutex_unlock(&heartbeat_mutex);
    free_message(msg);
}

void check_heartbeat_timeouts() {
    pthread_mutex_lock(&heartbeat_mutex);
    
    time_t now = time(NULL);
    
    for (int i = 0; i < heartbeat_count; i++) {
        double elapsed = difftime(now, heartbeats[i].last_heartbeat);
        
        if (elapsed > 10.0 && heartbeats[i].is_alive) {
            heartbeats[i].is_alive = 0;
            char log_msg[200];
            snprintf(log_msg, sizeof(log_msg), 
                     "WARNING: %s failed heartbeat - Status: DOWN", heartbeats[i].name);
            ns_log("WARNING", log_msg);
        }
    }
    
    pthread_mutex_unlock(&heartbeat_mutex);
}

// Heartbeat code ends here

// MESSAGE HANDLERS

// when a new client tries to register (code starts here)

void handle_client_registration(const char *message, int client_socket_fd) {
    Message *msg = parse_message(message);
    if (!msg) {
        ns_log("ERROR", "Failed to parse client registration");
        return;
    }
    
    char *username = get_string_field(msg, "username");
    if (!username) {
        ns_log("ERROR", "Username missing in registration");
        free_message(msg);
        return;
    }
    
    char log_msg[200];
    snprintf(log_msg, sizeof(log_msg), "Client registration: username=%s", username);
    ns_log("INFO", log_msg);
    
    // Update connection in registry
    Connection *conn = get_connection(registry, client_socket_fd);
    if (conn) {
        strcpy(conn->type, "CLIENT");
        strcpy(conn->identifier, username);
    }
    
    snprintf(log_msg, sizeof(log_msg), "Client registered: %s (socket=%d)", 
             username, client_socket_fd);
    ns_log("INFO", log_msg);
    
    // Send response
    char *response = create_response("SUCCESS", "Client registered", 0);
    send_message(client_socket_fd, response);
    free(response);
    
    free_message(msg);
}

// when a new client tries to register (code ends here)

// when a new storage server tries to register (code starts here)

void handle_ss_registration(const char *message, int ss_socket_fd) {
    Message *msg = parse_message(message);
    if (!msg) {
        ns_log("ERROR", "Failed to parse SS registration");
        return;
    }
    
    char *ss_name = get_string_field(msg, "name");
    char *ss_ip = get_string_field(msg, "ip");
    int client_port = get_int_field(msg, "client_port");
    
    if (!ss_name || !ss_ip || client_port == -1) {
        ns_log("ERROR", "SS registration missing required fields");
        free_message(msg);
        return;
    }
    
    char log_msg[200];
    snprintf(log_msg, sizeof(log_msg), "SS registration: name=%s", ss_name);
    ns_log("INFO", log_msg);
    
    // Update connection in registry
    Connection *conn = get_connection(registry, ss_socket_fd);
    if (conn) {
        strcpy(conn->type, "STORAGE_SERVER");
        strcpy(conn->identifier, ss_name);
    }
    
    // Initialize heartbeat tracking
    pthread_mutex_lock(&heartbeat_mutex);
    if (heartbeat_count < MAX_CONNECTIONS) {
        strcpy(heartbeats[heartbeat_count].name, ss_name);
        heartbeats[heartbeat_count].last_heartbeat = time(NULL);
        heartbeats[heartbeat_count].is_alive = 1;
        heartbeat_count++;
    }
    pthread_mutex_unlock(&heartbeat_mutex);
    
    snprintf(log_msg, sizeof(log_msg), "SS registered: %s (socket=%d)", 
             ss_name, ss_socket_fd);
    ns_log("INFO", log_msg);
    
    // Send response
    char *response = create_response("SUCCESS", "Storage Server registered", 0);
    send_message(ss_socket_fd, response);
    free(response);
    
    free_message(msg);
}

// when a new storage server tries to register (code ends here)

// Handles incoming messages and routes to appropriate handlers (code starts here)

void handle_incoming_message(const char *message, int sender_socket_fd) {
    Message *msg = parse_message(message);
    if (!msg) {
        ns_log("ERROR", "Failed to parse incoming message");
        return;
    }
    
    char *msg_type = get_string_field(msg, "type");
    if (!msg_type) {
        ns_log("ERROR", "Message type missing");
        free_message(msg);
        return;
    }
    
    // Route to appropriate handler
    if (strcmp(msg_type, "CLIENT_REGISTER") == 0) {
        handle_client_registration(message, sender_socket_fd);
    }
    else if (strcmp(msg_type, "SS_REGISTER") == 0) {
        handle_ss_registration(message, sender_socket_fd);
    }
    else if (strcmp(msg_type, "HEARTBEAT_RESPONSE") == 0) {
        handle_heartbeat_response(message);
    }
    else {
        char log_msg[200];
        snprintf(log_msg, sizeof(log_msg), "Unknown message type: %s", msg_type);
        ns_log("WARNING", log_msg);
    }
    
    free_message(msg);
}

// Handles incoming messages and routes to appropriate handlers (code ends here)

// CONNECTION HANDLER THREAD
// This thread handles communication with a single client or storage server

void* handle_connection(void* arg) {
    Connection *conn = (Connection*)arg;
    int socket_fd = conn->socket_fd;
    
    char log_msg[200];
    snprintf(log_msg, sizeof(log_msg), "Thread started for connection fd=%d", socket_fd);
    ns_log("DEBUG", log_msg);
    
    while (1) {
        char *msg = receive_message(socket_fd);
        
        if (!msg) {
            // Connection closed or error
            snprintf(log_msg, sizeof(log_msg), "Connection closed: fd=%d", socket_fd);
            ns_log("INFO", log_msg);
            break;
        }
        
        // Handle the message
        handle_incoming_message(msg, socket_fd);
        free(msg);
    }
    
    // Cleanup
    close(socket_fd);
    remove_connection(registry, socket_fd);
    
    free(conn);  // Free the connection structure passed to thread
    pthread_exit(NULL);
}

// HEARTBEAT THREAD
// Periodically sends heartbeats to storage servers and checks for timeouts

void* heartbeat_thread(void* arg) {
    ns_log("INFO", "Heartbeat thread started");
    
    while (1) {
        sleep(5);  // Wait 5 seconds
        
        send_heartbeats_to_ss();
        check_heartbeat_timeouts();
    }
    
    pthread_exit(NULL);
}

// EVENT LOOP (SIMPLIFIED - JUST ACCEPTS CONNECTIONS)
// Listens for incoming connections and spawns handler threads

void event_loop(int server_fd) {
    ns_log("INFO", "Entering event loop");
    
    // Start heartbeat thread
    pthread_t hb_thread;
    if (pthread_create(&hb_thread, NULL, heartbeat_thread, NULL) != 0) {
        ns_log("ERROR", "Failed to create heartbeat thread");
        return;
    }
    pthread_detach(hb_thread);
    
    while (1) {
        // Accept new connection
        Connection *new_conn = accept_client(server_fd);
        if (!new_conn) {
            continue;  // Accept failed, try again
        }
        
        // Add to registry
        add_connection(registry, new_conn);
        
        // Create thread to handle this connection
        pthread_t thread;
        Connection *conn_copy = (Connection*)malloc(sizeof(Connection));
        *conn_copy = *new_conn;
        
        if (pthread_create(&thread, NULL, handle_connection, conn_copy) != 0) {
            ns_log("ERROR", "Failed to create connection thread");
            close(new_conn->socket_fd);
            remove_connection(registry, new_conn->socket_fd);
            free(conn_copy);
            free(new_conn);
            continue;
        }
        
        // Store thread ID in registry
        pthread_mutex_lock(&registry->lock);
        for (int i = 0; i < registry->count; i++) {
            if (registry->connections[i].socket_fd == new_conn->socket_fd) {
                registry->connections[i].thread_id = thread;
                break;
            }
        }
        pthread_mutex_unlock(&registry->lock);
        
        // Detach thread (auto-cleanup on exit)
        pthread_detach(thread);
        
        free(new_conn);  // Free the original, thread has its own copy
    }
}

// MAIN FUNCTION
// Initializes server, starts event loop

int main() {
    printf("=== Name Server Starting ===\n");
    
    // Create registry
    registry = create_registry();
    if (!registry) {
        fprintf(stderr, "Failed to create registry\n");
        return 1;
    }
    
    // Create logs directory
    system("mkdir -p logs");
    
    // Open log file
    log_file = fopen("logs/ns.log", "a");
    if (!log_file) {
        perror("Failed to open log file");
        return 1;
    }
    
    ns_log("INFO", "Name Server started");
    
    // Create server socket
    int server_fd = create_server_socket(ns_port);
    if (server_fd < 0) {
        ns_log("ERROR", "Failed to create server socket");
        fclose(log_file);
        return 1;
    }
    
    printf("Listening on port %d\n", ns_port);
    ns_log("INFO", "Listening for connections");
    
    // Enter event loop
    event_loop(server_fd);
    
    // Cleanup
    close_server_socket(server_fd);
    free(registry->connections);
    free(registry);
    fclose(log_file);
    
    printf("=== Name Server Shutdown ===\n");
    return 0;
}
