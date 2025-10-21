#ifndef NS_H
#define NS_H

#include "../../common.h"
#include <time.h>

#define MAX_FILENAME 256
#define MAX_USERNAME 64
#define MAX_IP 16
#define MAX_USERS 100
#define MAX_STORAGE_SERVERS 10
#define MAX_FILES_PER_SS 1000
#define LOG_FILE "ns_log.txt"

// Operation types
typedef enum {
    OP_REGISTER_CLIENT,
    OP_REGISTER_SS,
    OP_CREATE,
    OP_DELETE,
    OP_READ,
    OP_WRITE,
    OP_STREAM,
    OP_VIEW,
    OP_VIEW_ALL,
    OP_VIEW_DETAILS,
    OP_INFO,
    OP_LIST,
    OP_ADDACCESS_R,
    OP_ADDACCESS_W,
    OP_REMACCESS,
    OP_EXEC,
    OP_UNDO,
    OP_UNKNOWN
} operation_type;

// Storage Server info
typedef struct {
    int id;
    char ip[MAX_IP];
    int nm_port;      // Port for NS-SS communication
    int client_port;  // Port for Client-SS communication
    int active;       // 1 if active, 0 if disconnected
} storage_server;

// File metadata
typedef struct {
    char filepath[MAX_FILENAME];
    char owner[MAX_USERNAME];
    int ss_id;  // Which storage server has this file
    int size;
    time_t last_access;
    int read_access[MAX_USERS];   // Index matches user_index
    int write_access[MAX_USERS];  // Index matches user_index
} file_metadata;

// User info
typedef struct {
    char username[MAX_USERNAME];
    int active;  // 1 if currently connected, 0 otherwise
    int user_index;  // Index in the users array
} user_info;

// Global NS state
typedef struct {
    storage_server ss_list[MAX_STORAGE_SERVERS];
    int ss_count;
    
    file_metadata files[MAX_FILES_PER_SS * MAX_STORAGE_SERVERS];
    int file_count;
    
    user_info users[MAX_USERS];
    int user_count;
    
    pthread_mutex_t lock;  // For thread-safe operations
    FILE *log_file;
} naming_server;

// Global NS instance
extern naming_server ns;

// Function declarations
void init_naming_server();
void log_message(const char *format, ...);
void *handle_connection(void *arg);
void handle_client_request(int client_fd, char *username);
void handle_ss_registration(int ss_fd);
int find_user(const char *username);
int add_user(const char *username);
int find_file(const char *filepath);
int check_read_permission(int file_idx, int user_idx);
int check_write_permission(int file_idx, int user_idx);
operation_type parse_operation(const char *command);

#endif
