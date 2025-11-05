#include "../Comm/communication.h"

// GLOBAL VARIABLES

int ns_socket = -1;
int my_port = 5001;
char my_name[100] = "SS1";
char my_ip[INET_ADDRSTRLEN] = "127.0.0.1";
FILE *log_file = NULL;
ConnectionRegistry *registry = NULL;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// LOGGING FUNCTION

void ss_log(const char *level, const char *message) {
    // Thread-safe logging with timestamp and log level, use mutex to ensure no interleaving
    pthread_mutex_lock(&log_mutex);
    
    time_t now = time(NULL);
    char *timestamp = ctime(&now);
    timestamp[strlen(timestamp) - 1] = '\0';
    
    printf("[%s] %s\n", level, message);
    
    if (log_file) {
        fprintf(log_file, "[%s] [%s] %s\n", timestamp, level, message);
        fflush(log_file);
    }
    
    pthread_mutex_unlock(&log_mutex);
}

// ARGUMENT PARSING

void parse_arguments(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            strncpy(my_name, argv[i + 1], sizeof(my_name) - 1);
            i++;
        }
        else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            my_port = atoi(argv[i + 1]);
            if (my_port < 1024 || my_port > 65535) {
                fprintf(stderr, "Invalid port number. Using default: 5001\n");
                my_port = 5001;
            }
            i++;
        }
    }
}

// REGISTRATION MESSAGE

char* create_ss_registration_message() {
    cJSON *json = cJSON_CreateObject();
    
    cJSON_AddStringToObject(json, "type", "SS_REGISTER");
    cJSON_AddStringToObject(json, "name", my_name);
    cJSON_AddStringToObject(json, "ip", my_ip);
    cJSON_AddNumberToObject(json, "nm_port", 5000);
    cJSON_AddNumberToObject(json, "client_port", my_port);
    
    // Add empty files array for now
    cJSON *files = cJSON_CreateArray();
    cJSON_AddItemToObject(json, "files", files);
    
    char *msg = cJSON_Print(json);
    cJSON_Delete(json);
    
    return msg;
}

// HEARTBEAT HANDLER

void handle_heartbeat_from_ns(const char *message) {
    cJSON *json = parse_message(message);
    if (!json) return;
    
    // Verify it's a heartbeat
    char *msg_type = get_string_field(json, "type");
    if (!msg_type || strcmp(msg_type, "HEARTBEAT") != 0) {
        cJSON_Delete(json);
        return;
    }
    
    // Create response
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "type", "HEARTBEAT_RESPONSE");
    cJSON_AddStringToObject(response, "name", my_name);
    cJSON_AddNumberToObject(response, "timestamp", (double)time(NULL));
    
    char *resp_msg = cJSON_Print(response);
    cJSON_Delete(response);
    
    // Send response
    send_message(ns_socket, resp_msg);
    free(resp_msg);
    
    ss_log("DEBUG", "Heartbeat from NS - responded");
    cJSON_Delete(json);
}

// NS LISTENER THREAD

void* ns_listener_thread(void* arg) {
    ss_log("INFO", "NS listener thread started");
    
    while (1) {
        char *msg = receive_message(ns_socket);
        if (!msg) {
            ss_log("ERROR", "NS connection lost");
            break;
        }
        
        handle_heartbeat_from_ns(msg);
        free(msg);
    }
    
    pthread_exit(NULL);
}

// CLIENT HANDLER THREAD

void* handle_client_connection(void* arg) {
    Connection *conn = (Connection*)arg;
    int socket_fd = conn->socket_fd;
    
    ss_log("INFO", "Client handler thread started");
    
    while (1) {
        char *msg = receive_message(socket_fd);
        if (!msg) {
            ss_log("INFO", "Client disconnected");
            break;
        }
        
        // For now, just log (partner adds file operations here)
        ss_log("INFO", "Message from client (not handled yet)");
        free(msg);
    }
    
    close(socket_fd);
    remove_connection(registry, socket_fd);
    free(conn);
    
    pthread_exit(NULL);
}

// EVENT LOOP (SIMPLIFIED - JUST ACCEPTS CLIENT CONNECTIONS)

void ss_event_loop(int client_server_fd) {
    ss_log("INFO", "Entering event loop");
    
    // Start NS listener thread
    pthread_t ns_thread;
    if (pthread_create(&ns_thread, NULL, ns_listener_thread, NULL) != 0) {
        ss_log("ERROR", "Failed to create NS listener thread");
        return;
    }
    pthread_detach(ns_thread);
    
    while (1) {
        // Accept new client connection
        Connection *new_conn = accept_client(client_server_fd);
        if (!new_conn) {
            continue;
        }
        
        add_connection(registry, new_conn);
        
        // Create thread to handle this client
        pthread_t thread;
        Connection *conn_copy = (Connection*)malloc(sizeof(Connection));
        *conn_copy = *new_conn;
        
        if (pthread_create(&thread, NULL, handle_client_connection, conn_copy) != 0) {
            ss_log("ERROR", "Failed to create client handler thread");
            close(new_conn->socket_fd);
            remove_connection(registry, new_conn->socket_fd);
            free(conn_copy);
            free(new_conn);
            continue;
        }
        
        pthread_detach(thread);
        free(new_conn);
    }
}

// MAIN FUNCTION

int main(int argc, char *argv[]) {
    parse_arguments(argc, argv);
    
    printf("=== Storage Server: %s ===\n", my_name);
    
    // Create registry
    registry = create_registry();
    if (!registry) {
        fprintf(stderr, "Failed to create registry\n");
        return 1;
    }
    
    // Create logs directory
    system("mkdir -p logs");
    
    // Open log file
    char log_filename[100];
    snprintf(log_filename, sizeof(log_filename), "logs/%s.log", my_name);
    log_file = fopen(log_filename, "a");
    if (!log_file) {
        perror("Failed to open log file");
        return 1;
    }
    
    ss_log("INFO", "Storage Server started");
    
    // Connect to Name Server
    printf("Connecting to Name Server...\n");
    ns_socket = create_client_socket("127.0.0.1", 5000);
    if (ns_socket < 0) {
        ss_log("ERROR", "Failed to connect to Name Server");
        fclose(log_file);
        return 1;
    }
    
    printf("Connected to Name Server\n");
    
    // Send registration
    char *reg_msg = create_ss_registration_message();
    if (send_message(ns_socket, reg_msg) < 0) {
        ss_log("ERROR", "Failed to send registration");
        free(reg_msg);
        close(ns_socket);
        fclose(log_file);
        return 1;
    }
    free(reg_msg);
    
    // Receive acknowledgment
    char *ack = receive_message(ns_socket);
    if (ack) {
        printf("Registration successful\n");
        ss_log("INFO", "Registration acknowledged");
        free(ack);
    } else {
        ss_log("ERROR", "No registration acknowledgment");
        close(ns_socket);
        fclose(log_file);
        return 1;
    }
    
    // Create server socket for clients
    int client_server_fd = create_server_socket(my_port);
    if (client_server_fd < 0) {
        ss_log("ERROR", "Failed to create client server socket");
        close(ns_socket);
        fclose(log_file);
        return 1;
    }
    
    printf("Listening on port %d\n", my_port);
    ss_log("INFO", "Ready to accept client connections");
    
    // Enter event loop
    ss_event_loop(client_server_fd);
    
    // Cleanup
    close_server_socket(client_server_fd);
    close_client_socket(ns_socket);
    free(registry->connections);
    free(registry);
    fclose(log_file);
    
    printf("=== Storage Server Shutdown ===\n");
    return 0;
}
