#ifndef NS_FILEMANAGER_H
#define NS_FILEMANAGER_H

#include "../Comm/communication.h"
#include "../File/include/infofile.h"

// ============================================================================
// ACTIVE USERS LIST
// ============================================================================

typedef struct active_user_node {
    char username[100];
    struct active_user_node *next;
} active_user_node;

typedef struct {
    active_user_node *head;
    pthread_mutex_t lock;
} active_users_list;

// Active users functions
active_users_list* create_active_users_list();
void add_active_user(active_users_list *list, const char *username);
void remove_active_user(active_users_list *list, const char *username);
int is_user_active(active_users_list *list, const char *username);
void print_active_users(active_users_list *list);
void free_active_users_list(active_users_list *list);

// ============================================================================
// STORAGE SERVER LIST (for a specific file)
// ============================================================================

typedef struct ss_node {
    char ss_name[100];
    char ss_ip[INET_ADDRSTRLEN];
    int ss_client_port;
    int is_alive;  // Based on heartbeat status
    struct ss_node *next;
} ss_node;

// ============================================================================
// FILE PATH MAPPING
// ============================================================================

typedef struct file_entry {
    char file_path[512];
    ss_node *storage_servers;  // Linked list of storage servers having this file
    pthread_mutex_t lock;
    struct file_entry *next;
} file_entry;

typedef struct {
    file_entry *head;
    pthread_mutex_t lock;
} file_path_list;

// File path list functions
file_path_list* create_file_path_list();
void add_file_path(file_path_list *list, const char *file_path, const char *ss_name, 
                   const char *ss_ip, int ss_client_port);
void remove_file_path(file_path_list *list, const char *file_path);
void remove_ss_from_file(file_path_list *list, const char *file_path, const char *ss_name);
void mark_ss_status(file_path_list *list, const char *ss_name, int is_alive);
void print_file_path_list(file_path_list *list);
void free_file_path_list(file_path_list *list);

// ============================================================================
// FILE REQUEST HANDLER
// ============================================================================

typedef struct {
    char ss_name[100];
    char ss_ip[INET_ADDRSTRLEN];
    int ss_client_port;
    info_file *file_info;  // Metadata and permissions for the file
} file_request_result;

/**
 * Main function to get an active storage server for a file path
 * 
 * @param list - The file path list maintained by NS
 * @param file_path - The path of the file requested
 * @return file_request_result* - Contains SS info and file metadata, or NULL if not found/no active SS
 * 
 * Caller must free the returned result using free_file_request_result()
 */
file_request_result* get_active_ss_for_file(file_path_list *list, const char *file_path);

/**
 * Free the file request result
 */
void free_file_request_result(file_request_result *result);

#endif // NS_FILEMANAGER_H
