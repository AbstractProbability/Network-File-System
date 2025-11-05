#ifndef COMMUNICATION_H
#define COMMUNICATION_H

#include <stdio.h> // used for perror
#include <stdlib.h> // used for malloc, free, realloc
#include <string.h> // used for memset, strcpy, strcmp
#include <unistd.h> // used for close
#include <sys/socket.h> // used for socket functions
#include <netinet/in.h> // used for sockaddr_in
#include <arpa/inet.h> // used for inet_pton, inet_ntop
#include <time.h> // used for time
#include <cjson/cJSON.h> // used for JSON parsing
#include <sys/select.h> // used for select
#include <errno.h> // used for errno
#include <pthread.h> // used for mutexes and threading

#define BUFFER_SIZE 8192 // Buffer size for message receiving is 8192
#define MAX_CONNECTIONS 100 // Maximum number of concurrent connections is 100
#define INET_ADDRSTRLEN 16 // Maximum length of IPv4 address string is 16

// Connection structure - represents one active connection
typedef struct {
    int socket_fd;
    char client_ip[INET_ADDRSTRLEN];
    int client_port;
    char type[20];              // "CLIENT" or "STORAGE_SERVER"
    char identifier[100];       // username or SS name
    time_t connected_at;
    pthread_t thread_id;        // Thread handling this connection
} Connection;

// Connection registry - tracks all active connections (thread-safe)
typedef struct {
    Connection *connections;
    int count;
    int capacity;
    pthread_mutex_t lock;       // Protects registry operations
} ConnectionRegistry;

// Server socket functions
int create_server_socket(int port);
Connection* accept_client(int server_fd);
void close_server_socket(int server_fd);

// Client socket functions
int create_client_socket(const char *server_ip, int server_port);
void close_client_socket(int client_fd);

// Message functions
int send_message(int socket_fd, const char *message);
char* receive_message(int socket_fd);

// JSON helper functions
cJSON* parse_message(const char *json_string);
char* get_string_field(cJSON *json, const char *field_name);
int get_int_field(cJSON *json, const char *field_name);
char* create_response(const char *status, const char *message, int error_code);

// Connection registry functions
ConnectionRegistry* create_registry();
void add_connection(ConnectionRegistry *registry, Connection *conn);
Connection* get_connection(ConnectionRegistry *registry, int socket_fd);
void remove_connection(ConnectionRegistry *registry, int socket_fd);
void print_all_connections(ConnectionRegistry *registry);

#endif // COMMUNICATION_H
