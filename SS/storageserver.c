#include "../Comm/communication.h"
#include <unistd.h> // for sleep()

// GLOBAL VARIABLES

int ns_socket = -1; // global variable to hold Name Server socket
int my_port = 5001; // default port for Storage Server where it listens for client connections
char my_name[100] = "SS1"; // name of the Storage Server
char my_ip[INET_ADDRSTRLEN] = "127.0.0.1"; // IP address of the Storage Server
FILE *log_file = NULL; // log file pointer
ConnectionRegistry *registry = NULL; // connection registry
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER; // mutex for log file access, included with pthread.h
// mutex to serialize sends to the Name Server socket
pthread_mutex_t ns_send_lock = PTHREAD_MUTEX_INITIALIZER;

// LOGGING FUNCTION

void ss_log(const char *level, const char *message) {
    // Thread-safe logging with timestamp and log level, use mutex to ensure no interleaving
    pthread_mutex_lock(&log_mutex);
    
    time_t now = time(NULL); // get current time in numerical format
    char *timestamp = ctime(&now); // convert to human readable format
    timestamp[strlen(timestamp) - 1] = '\0'; // ctime returns a string with newline, replace it with null terminator
    
    printf("[%s] %s\n", level, message);
    
    if (log_file) {
        fprintf(log_file, "[%s] [%s] %s\n", timestamp, level, message);
        fflush(log_file); // ensure log is written immediately
    }
    
    pthread_mutex_unlock(&log_mutex);
}

// ARGUMENT PARSING

void parse_arguments(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            strncpy(my_name, argv[i + 1], sizeof(my_name) - 1); // --name <NAME> sets global variable my_name to input NAME 
            i++;
        }
        else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            to_be_my_port = atoi(argv[i + 1]); // --port <PORT> sets global variable my_port to input PORT
            if (to_be_my_port < 1024 || to_be_my_port > 65535) {
                fprintf(stderr, "Invalid port number. Using default: 5001\n");
            } else {
                my_port = to_be_my_port;
            }
            i++;
        } 
    }
}

// REGISTRATION MESSAGE

char* create_ss_registration_message() {
    Message *msg = create_message();
    
    add_string_field(msg, "type", "SS_REGISTER");
    add_string_field(msg, "name", my_name);
    add_string_field(msg, "ip", my_ip);
    add_number_field(msg, "nm_port", 5000); // Name Server port is hardcoded to 5000
    add_number_field(msg, "client_port", my_port);
    add_array_field(msg, "files"); // start with empty file list
    
    char *result = serialize_message(msg);
    free_message(msg);
    
    return result;
}

// HEARTBEAT HANDLER

void handle_heartbeat_from_ns(const char *message) {
    Message *msg = parse_message(message);
    if (!msg) return;
    
    // Verify it's a heartbeat
    char *msg_type = get_string_field(msg, "type");
    if (!msg_type || strcmp(msg_type, "HEARTBEAT") != 0) {
        free_message(msg);
        return;
    }
    
    // Create response
    Message *response = create_message();
    add_string_field(response, "type", "HEARTBEAT_RESPONSE");
    add_string_field(response, "name", my_name);
    add_number_field(response, "timestamp", (double)time(NULL));
    
    char *resp_msg = serialize_message(response);
    free_message(response);
    
    // Send response
    pthread_mutex_lock(&ns_send_lock);
    if (send_message(ns_socket, resp_msg) < 0) {
        ss_log("ERROR", "Failed to send heartbeat response to NS");
    }
    pthread_mutex_unlock(&ns_send_lock);
    free(resp_msg);
    
    ss_log("DEBUG", "Heartbeat from NS - responded");
    free_message(msg);
}

// NS LISTENER THREAD

void* ns_listener_thread(void* arg) {
    ss_log("INFO", "NS listener thread started");
    
    while (1) {
        char *msg = receive_message(ns_socket);
        if (!msg) {
            ss_log("ERROR", "NS connection lost. Attempting to reconnect...");

            // Close old socket if any
            if (ns_socket >= 0) {
                close_client_socket(ns_socket);
                ns_socket = -1;
            }

            // Try reconnecting and re-registering until successful
            while (1) {
                ss_log("INFO", "Attempting to reconnect to Name Server...");
                int new_sock = create_client_socket("127.0.0.1", 5000);
                if (new_sock >= 0) {
                    ns_socket = new_sock;
                    ss_log("INFO", "Reconnected to Name Server");

                    // Re-send registration
                    char *rmsg = create_ss_registration_message();
                    pthread_mutex_lock(&ns_send_lock);
                    if (send_message(ns_socket, rmsg) < 0) {
                        ss_log("ERROR", "Failed to send registration after reconnect");
                        pthread_mutex_unlock(&ns_send_lock);
                        free(rmsg);
                        close_client_socket(ns_socket);
                        ns_socket = -1;
                        sleep(1);
                        continue;
                    }
                    pthread_mutex_unlock(&ns_send_lock);
                    free(rmsg);

                    // Wait for ack
                    char *ack = receive_message(ns_socket);
                    if (ack) {
                        ss_log("INFO", "Re-registration acknowledged by Name Server");
                        free(ack);
                        break; // reconnected and re-registered successfully
                    } else {
                        ss_log("ERROR", "No registration ack after reconnect; retrying");
                        if (ns_socket >= 0) {
                            close_client_socket(ns_socket);
                            ns_socket = -1;
                        }
                        sleep(1);
                        continue;
                    }
                }

                // wait a bit before retrying
                sleep(1);
            }

            // continue listening loop after successful reconnect
            continue;
        }
        
        handle_heartbeat_from_ns(msg);
        free(msg);
    }
    
    pthread_exit(NULL); // exit thread
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

// EVENT LOOP (SIMPLIFIED - JUST ACCEPTS CLIENT CONNECTIONS AND SPAWNS NS LISTENER THREAD)

void ss_event_loop(int client_server_fd) {
    ss_log("INFO", "Entering event loop");
    
    // Start NS listener thread
    pthread_t ns_thread;
    if (pthread_create(&ns_thread, NULL, ns_listener_thread, NULL) != 0) {
        ss_log("ERROR", "Failed to create NS listener thread");
        return;
    }
    pthread_detach(ns_thread); // auto-cleanup on exit, no need to track pthread_t or join the threads
    
    while (1) {
        // Accept new client connection
        Connection *new_conn = accept_client(client_server_fd);
        if (!new_conn) {
            continue;
        } // Accept failed, try again
        
        add_connection(registry, new_conn);
        
        // Create thread to handle this client
        pthread_t thread;
        Connection *conn_copy = (Connection*)malloc(sizeof(Connection));
        if (!conn_copy) {
            ss_log("ERROR", "Failed to allocate memory for client connection");
            close(new_conn->socket_fd);
            remove_connection(registry, new_conn->socket_fd);
            free(new_conn);
            continue;
        }
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
    system("mkdir -p logs"); // create logs directory if not exists
    
    // Open log file
    char log_filename[100];
    snprintf(log_filename, sizeof(log_filename), "logs/%s.log", my_name); // create log file path
    log_file = fopen(log_filename, "a");
    if (!log_file) {
        perror("Failed to open log file");
        return 1;
    }
    
    ss_log("INFO", "Storage Server started");
    
    // Connect to Name Server
    printf("Connecting to Name Server...\n");
    ns_socket = create_client_socket("127.0.0.1", 5000); // connect to NS at localhost:5000
    if (ns_socket < 0) {
        ss_log("ERROR", "Failed to connect to Name Server");
        fclose(log_file);
        return 1;
    }
    
    printf("Connected to Name Server\n");
    
    // Send registration
    char *reg_msg = create_ss_registration_message();
    pthread_mutex_lock(&ns_send_lock);
    if (send_message(ns_socket, reg_msg) < 0) {
        ss_log("ERROR", "Failed to send registration");
        pthread_mutex_unlock(&ns_send_lock);
        free(reg_msg);
        close(ns_socket);
        fclose(log_file);
        return 1;
    }
    pthread_mutex_unlock(&ns_send_lock);
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
