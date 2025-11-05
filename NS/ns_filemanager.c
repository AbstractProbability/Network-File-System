#include "ns_filemanager.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// ============================================================================
// ACTIVE USERS LIST IMPLEMENTATION
// ============================================================================

active_users_list* create_active_users_list() {
    active_users_list *list = (active_users_list*)malloc(sizeof(active_users_list));
    if (!list) {
        perror("malloc failed for active_users_list");
        return NULL;
    }
    
    list->head = NULL;
    pthread_mutex_init(&list->lock, NULL); 
    return list;
}

void add_active_user(active_users_list *list, const char *username) {
    if (!list || !username) return;
    
    pthread_mutex_lock(&list->lock);
    
    // Check if user already exists
    active_user_node *current = list->head;
    while (current) {
        if (strcmp(current->username, username) == 0) {
            pthread_mutex_unlock(&list->lock);
            return; // User already active
        }
        current = current->next;
    }
    
    // Add new user
    active_user_node *new_node = (active_user_node*)malloc(sizeof(active_user_node));
    if (!new_node) {
        perror("malloc failed for active_user_node");
        pthread_mutex_unlock(&list->lock);
        return;
    }
    
    strncpy(new_node->username, username, sizeof(new_node->username) - 1);
    new_node->username[sizeof(new_node->username) - 1] = '\0';
    new_node->next = list->head;
    list->head = new_node;
    
    pthread_mutex_unlock(&list->lock);
}

void remove_active_user(active_users_list *list, const char *username) {
    if (!list || !username) return;
    
    pthread_mutex_lock(&list->lock);
    
    active_user_node *current = list->head;
    active_user_node *prev = NULL;
    
    while (current) {
        if (strcmp(current->username, username) == 0) {
            if (prev) {
                prev->next = current->next;
            } else {
                list->head = current->next;
            }
            free(current);
            pthread_mutex_unlock(&list->lock);
            return;
        }
        prev = current;
        current = current->next;
    }
    
    pthread_mutex_unlock(&list->lock);
}

int is_user_active(active_users_list *list, const char *username) {
    if (!list || !username) return 0;
    
    pthread_mutex_lock(&list->lock);
    
    active_user_node *current = list->head;
    while (current) {
        if (strcmp(current->username, username) == 0) {
            pthread_mutex_unlock(&list->lock);
            return 1;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&list->lock);
    return 0;
}

void print_active_users(active_users_list *list) {
    if (!list) return;
    
    pthread_mutex_lock(&list->lock);
    
    printf("\n=== Active Users ===\n");
    active_user_node *current = list->head;
    int count = 0;
    
    while (current) {
        printf("  - %s\n", current->username);
        count++;
        current = current->next;
    }
    
    printf("Total: %d active users\n\n", count);
    pthread_mutex_unlock(&list->lock);
}

void free_active_users_list(active_users_list *list) {
    if (!list) return;
    
    pthread_mutex_lock(&list->lock);
    
    active_user_node *current = list->head;
    while (current) {
        active_user_node *next = current->next;
        free(current);
        current = next;
    }
    
    pthread_mutex_unlock(&list->lock);
    pthread_mutex_destroy(&list->lock);
    free(list);
}

// ============================================================================
// FILE PATH LIST IMPLEMENTATION
// ============================================================================

file_path_list* create_file_path_list() {
    file_path_list *list = (file_path_list*)malloc(sizeof(file_path_list));
    if (!list) {
        perror("malloc failed for file_path_list");
        return NULL;
    }
    
    list->head = NULL;
    pthread_mutex_init(&list->lock, NULL);
    
    return list;
}

// Helper function to create a new ss_node
static ss_node* create_ss_node(const char *ss_name, const char *ss_ip, int ss_client_port) {
    ss_node *node = (ss_node*)malloc(sizeof(ss_node));
    if (!node) {
        perror("malloc failed for ss_node");
        return NULL;
    }
    
    strncpy(node->ss_name, ss_name, sizeof(node->ss_name) - 1);
    node->ss_name[sizeof(node->ss_name) - 1] = '\0';
    
    strncpy(node->ss_ip, ss_ip, sizeof(node->ss_ip) - 1);
    node->ss_ip[sizeof(node->ss_ip) - 1] = '\0';
    
    node->ss_client_port = ss_client_port;
    node->is_alive = 1; // Assume alive when added
    node->next = NULL;
    
    return node;
}

// Helper function to add SS to a file entry
static void add_ss_to_file_entry(file_entry *entry, const char *ss_name, 
                                  const char *ss_ip, int ss_client_port) {
    pthread_mutex_lock(&entry->lock);
    
    // Check if SS already exists for this file
    ss_node *current = entry->storage_servers;
    while (current) {
        if (strcmp(current->ss_name, ss_name) == 0) {
            // Update existing SS info
            strncpy(current->ss_ip, ss_ip, sizeof(current->ss_ip) - 1);
            current->ss_ip[sizeof(current->ss_ip) - 1] = '\0';
            current->ss_client_port = ss_client_port;
            current->is_alive = 1;
            pthread_mutex_unlock(&entry->lock);
            return;
        }
        current = current->next;
    }
    
    // Add new SS
    ss_node *new_ss = create_ss_node(ss_name, ss_ip, ss_client_port);
    if (new_ss) {
        new_ss->next = entry->storage_servers;
        entry->storage_servers = new_ss;
    }
    
    pthread_mutex_unlock(&entry->lock);
}

void add_file_path(file_path_list *list, const char *file_path, const char *ss_name,
                   const char *ss_ip, int ss_client_port) {
    if (!list || !file_path || !ss_name || !ss_ip) return;
    
    pthread_mutex_lock(&list->lock);
    
    // Check if file entry already exists
    file_entry *current = list->head;
    while (current) {
        if (strcmp(current->file_path, file_path) == 0) {
            pthread_mutex_unlock(&list->lock);
            add_ss_to_file_entry(current, ss_name, ss_ip, ss_client_port);
            return;
        }
        current = current->next;
    }
    
    // Create new file entry
    file_entry *new_entry = (file_entry*)malloc(sizeof(file_entry));
    if (!new_entry) {
        perror("malloc failed for file_entry");
        pthread_mutex_unlock(&list->lock);
        return;
    }
    
    strncpy(new_entry->file_path, file_path, sizeof(new_entry->file_path) - 1);
    new_entry->file_path[sizeof(new_entry->file_path) - 1] = '\0';
    
    new_entry->storage_servers = create_ss_node(ss_name, ss_ip, ss_client_port);
    pthread_mutex_init(&new_entry->lock, NULL);
    new_entry->next = list->head;
    list->head = new_entry;
    
    pthread_mutex_unlock(&list->lock);
}

void remove_file_path(file_path_list *list, const char *file_path) {
    if (!list || !file_path) return;
    
    pthread_mutex_lock(&list->lock);
    
    file_entry *current = list->head;
    file_entry *prev = NULL;
    
    while (current) {
        if (strcmp(current->file_path, file_path) == 0) {
            if (prev) {
                prev->next = current->next;
            } else {
                list->head = current->next;
            }
            
            // Free all SS nodes
            pthread_mutex_lock(&current->lock);
            ss_node *ss = current->storage_servers;
            while (ss) {
                ss_node *next_ss = ss->next;
                free(ss);
                ss = next_ss;
            }
            pthread_mutex_unlock(&current->lock);
            pthread_mutex_destroy(&current->lock);
            
            free(current);
            pthread_mutex_unlock(&list->lock);
            return;
        }
        prev = current;
        current = current->next;
    }
    
    pthread_mutex_unlock(&list->lock);
}

void remove_ss_from_file(file_path_list *list, const char *file_path, const char *ss_name) {
    if (!list || !file_path || !ss_name) return;
    
    pthread_mutex_lock(&list->lock);
    
    file_entry *entry = list->head;
    while (entry) {
        if (strcmp(entry->file_path, file_path) == 0) {
            pthread_mutex_lock(&entry->lock);
            
            ss_node *current = entry->storage_servers;
            ss_node *prev = NULL;
            
            while (current) {
                if (strcmp(current->ss_name, ss_name) == 0) {
                    if (prev) {
                        prev->next = current->next;
                    } else {
                        entry->storage_servers = current->next;
                    }
                    free(current);
                    pthread_mutex_unlock(&entry->lock);
                    pthread_mutex_unlock(&list->lock);
                    return;
                }
                prev = current;
                current = current->next;
            }
            
            pthread_mutex_unlock(&entry->lock);
            break;
        }
        entry = entry->next;
    }
    
    pthread_mutex_unlock(&list->lock);
}

void mark_ss_status(file_path_list *list, const char *ss_name, int is_alive) {
    if (!list || !ss_name) return;
    
    pthread_mutex_lock(&list->lock);
    
    file_entry *entry = list->head;
    while (entry) {
        pthread_mutex_lock(&entry->lock);
        
        ss_node *ss = entry->storage_servers;
        while (ss) {
            if (strcmp(ss->ss_name, ss_name) == 0) {
                ss->is_alive = is_alive;
            }
            ss = ss->next;
        }
        
        pthread_mutex_unlock(&entry->lock);
        entry = entry->next;
    }
    
    pthread_mutex_unlock(&list->lock);
}

void print_file_path_list(file_path_list *list) {
    if (!list) return;
    
    pthread_mutex_lock(&list->lock);
    
    printf("\n=== File Path Mapping ===\n");
    file_entry *entry = list->head;
    int file_count = 0;
    
    while (entry) {
        pthread_mutex_lock(&entry->lock);
        
        printf("\nFile: %s\n", entry->file_path);
        printf("  Storage Servers:\n");
        
        ss_node *ss = entry->storage_servers;
        while (ss) {
            printf("    - %s (%s:%d) [%s]\n", 
                   ss->ss_name, ss->ss_ip, ss->ss_client_port,
                   ss->is_alive ? "ALIVE" : "DOWN");
            ss = ss->next;
        }
        
        pthread_mutex_unlock(&entry->lock);
        file_count++;
        entry = entry->next;
    }
    
    printf("\nTotal: %d files tracked\n\n", file_count);
    pthread_mutex_unlock(&list->lock);
}

void free_file_path_list(file_path_list *list) {
    if (!list) return;
    
    pthread_mutex_lock(&list->lock);
    
    file_entry *entry = list->head;
    while (entry) {
        file_entry *next_entry = entry->next;
        
        pthread_mutex_lock(&entry->lock);
        
        // Free all SS nodes
        ss_node *ss = entry->storage_servers;
        while (ss) {
            ss_node *next_ss = ss->next;
            free(ss);
            ss = next_ss;
        }
        
        pthread_mutex_unlock(&entry->lock);
        pthread_mutex_destroy(&entry->lock);
        free(entry);
        
        entry = next_entry;
    }
    
    pthread_mutex_unlock(&list->lock);
    pthread_mutex_destroy(&list->lock);
    free(list);
}

// ============================================================================
// FILE REQUEST HANDLER IMPLEMENTATION
// ============================================================================

file_request_result* get_active_ss_for_file(file_path_list *list, const char *file_path) {
    if (!list || !file_path) {
        fprintf(stderr, "get_active_ss_for_file: Invalid parameters\n");
        return NULL;
    }
    
    pthread_mutex_lock(&list->lock);
    
    // Find the file entry
    file_entry *entry = list->head;
    while (entry) {
        if (strcmp(entry->file_path, file_path) == 0) {
            break;
        }
        entry = entry->next;
    }
    
    if (!entry) {
        pthread_mutex_unlock(&list->lock);
        fprintf(stderr, "get_active_ss_for_file: File '%s' not found in NS index\n", file_path);
        return NULL;
    }
    
    pthread_mutex_lock(&entry->lock);
    pthread_mutex_unlock(&list->lock);
    
    // Find first alive storage server
    ss_node *ss = entry->storage_servers;
    ss_node *selected_ss = NULL;
    
    while (ss) {
        if (ss->is_alive) {
            selected_ss = ss;
            break;
        }
        ss = ss->next;
    }
    
    if (!selected_ss) {
        pthread_mutex_unlock(&entry->lock);
        fprintf(stderr, "get_active_ss_for_file: No active storage server found for file '%s'\n", file_path);
        return NULL;
    }
    
    // Allocate result structure
    file_request_result *result = (file_request_result*)malloc(sizeof(file_request_result));
    if (!result) {
        perror("malloc failed for file_request_result");
        pthread_mutex_unlock(&entry->lock);
        return NULL;
    }
    
    // Copy SS information
    strncpy(result->ss_name, selected_ss->ss_name, sizeof(result->ss_name) - 1);
    result->ss_name[sizeof(result->ss_name) - 1] = '\0';
    
    strncpy(result->ss_ip, selected_ss->ss_ip, sizeof(result->ss_ip) - 1);
    result->ss_ip[sizeof(result->ss_ip) - 1] = '\0';
    
    result->ss_client_port = selected_ss->ss_client_port;
    
    pthread_mutex_unlock(&entry->lock);
    
    // Read the info file
    // Note: This assumes the info file can be read from NS side
    // You may need to request this from the SS instead
    result->file_info = read_info_file((char*)file_path);
    
    if (!result->file_info) {
        fprintf(stderr, "Warning: Could not read info file for '%s'\n", file_path);
        // Don't fail completely - return SS info even if info file can't be read
        // The caller can handle this case
    }
    
    return result;
}

void free_file_request_result(file_request_result *result) {
    if (!result) return;
    
    if (result->file_info) {
        free_info_file(result->file_info);
    }
    
    free(result);
}
