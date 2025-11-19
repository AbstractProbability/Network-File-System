#include "ns_data.h"
#include "../include/serialize.h"
#include <stdlib.h> // For free()

// Simple hash function for file paths
static unsigned long hash_path(const char* path) {
    unsigned long hash = 5381;
    int c;
    while ((c = *path++))
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    return hash % FILE_INDEX_SIZE;
}

// Finds a file in the index.
// Returns a pointer to the FileInfo if found, NULL otherwise.
// NOTE: Assumes file_index lock is held!
FileInfo* ns_file_index_get(const char* path) {
    unsigned long index = hash_path(path);
    FileInfo* current = ns.file_index->table[index];
    
    while (current != NULL) {
        if (strcmp(current->path, path) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

// Get file with LRU cache support
// This function handles locking internally
// Returns a pointer to FileInfo (from cache or index)
// Caller must NOT free this pointer, but should consider it read-only
FileInfo* ns_file_get_cached(const char* path) {
    // First check cache (no lock needed, cache has its own lock)
    FileInfo* cached = lru_cache_get(ns.lru_cache, path);
    if (cached != NULL) {
        // Cache hit - return the cached copy
        printf("NS: ns_file_get_cached('%s') - CACHE HIT (last_accessed=%ld, modified=%ld)\n", 
               path, cached->last_accessed, cached->modified);
        return cached;
    }
    
    // Cache miss - look in file index
    printf("NS: ns_file_get_cached('%s') - CACHE MISS, fetching from file index\n", path);
    pthread_mutex_lock(&ns.file_index->lock);
    FileInfo* file = ns_file_index_get(path);
    
    if (file != NULL) {
        printf("NS: ns_file_get_cached('%s') - Found in index (last_accessed=%ld, modified=%ld)\n", 
               path, file->last_accessed, file->modified);
        // Update cache before releasing lock
        lru_cache_put(ns.lru_cache, path, file);
        // Return a cached copy (lru_cache_put stores a copy)
        pthread_mutex_unlock(&ns.file_index->lock);
        
        // Get from cache again to return a copy
        cached = lru_cache_get(ns.lru_cache, path);
        printf("NS: ns_file_get_cached('%s') - Returning fresh copy from cache\n", path);
        return cached;
    }
    
    pthread_mutex_unlock(&ns.file_index->lock);
    return NULL;
}

// Adds a new file to the index.
// Returns 0 on success, or ERR_FILE_EXISTS if it's already there.
// NOTE: Assumes file_index lock is held!
ErrorCode ns_file_index_add(const char* path, const char* owner, SSInfo* ss) {
    if (ns_file_index_get(path) != NULL) {
        return ERR_FILE_EXISTS;
    }
    
    unsigned long index = hash_path(path);
    
    // Create new FileInfo node
    FileInfo* new_file = (FileInfo*)calloc(1, sizeof(FileInfo));
    strncpy(new_file->path, path, MAX_PATH_LEN);
    strncpy(new_file->owner, owner, MAX_USERNAME_LEN);
    
    // Add the storage server
    new_file->ss_list = (SSInfo**)malloc(sizeof(SSInfo*));
    new_file->ss_list[0] = ss;
    new_file->ss_count = 1;
    
    // TODO: Initialize other metadata (access lists, etc.)
    
    // Insert into hash map (at the head of the chain)
    new_file->next = ns.file_index->table[index];
    ns.file_index->table[index] = new_file;
    
    return ERR_OK;
}

// Adds a file to the index with an existing SS list (used for MOVE operation)
// NOTE: Assumes file_index lock is held!
ErrorCode ns_file_index_add_with_ss(const char* path, const char* owner, SSInfo** ss_list, int ss_count) {
    if (ns_file_index_get(path) != NULL) {
        return ERR_FILE_EXISTS;
    }
    
    unsigned long index = hash_path(path);
    
    // Create new FileInfo node
    FileInfo* new_file = (FileInfo*)calloc(1, sizeof(FileInfo));
    strncpy(new_file->path, path, MAX_PATH_LEN);
    strncpy(new_file->owner, owner, MAX_USERNAME_LEN);
    
    // Copy the SS list
    new_file->ss_list = ss_list;
    new_file->ss_count = ss_count;
    
    // Insert into hash map (at the head of the chain)
    new_file->next = ns.file_index->table[index];
    ns.file_index->table[index] = new_file;
    
    return ERR_OK;
}

// Remove a file from the index
// NOTE: Assumes file_index lock is held!
void ns_file_index_remove(const char* path) {
    unsigned long index = hash_path(path);
    FileInfo* current = ns.file_index->table[index];
    FileInfo* prev = NULL;
    
    while (current != NULL) {
        if (strcmp(current->path, path) == 0) {
            // Found it - remove from chain
            if (prev == NULL) {
                // First in chain
                ns.file_index->table[index] = current->next;
            } else {
                prev->next = current->next;
            }
            
            // Free the FileInfo (but not the SS list - caller manages that)
            // Note: Access lists are freed elsewhere if needed
            free(current);
            return;
        }
        prev = current;
        current = current->next;
    }
}

// --- NEW HELPER: Adds an SS to a file's existing list ---
void ns_file_add_ss(FileInfo* file, SSInfo* ss) {
    // 1. Check if this SS is already in the list (idempotency)
    for (int i = 0; i < file->ss_count; i++) {
        if (file->ss_list[i] == ss) {
            return; // Already registered
        }
    }
    
    // 2. Add the new SS
    file->ss_count++;
    file->ss_list = (SSInfo**)realloc(file->ss_list, file->ss_count * sizeof(SSInfo*));
    if (file->ss_list == NULL) {
        perror("realloc failed in ns_file_add_ss");
        exit(EXIT_FAILURE); // Critical error
    }
    file->ss_list[file->ss_count - 1] = ss;
}

// --- NEW HELPER: Parses info and adds to index ---
void ns_parse_and_index_file(const char* path, const char* info_content, SSInfo* ss) {
    char owner[MAX_USERNAME_LEN] = "unknown";
    time_t created = 0, modified = 0, last_accessed = 0;
    char last_accessed_by[MAX_USERNAME_LEN] = "";
    long size = 0;
    int word_count = 0, char_count = 0;
    char read_users[MAX_BUFFER_LEN] = "";
    char write_users[MAX_BUFFER_LEN] = "";
    char exec_users[MAX_BUFFER_LEN] = "";
    
    // Parse info_content
    const char* line = info_content;
    char line_buf[512];
    
    while (*line) {
        // Read one line
        int i = 0;
        while (*line && *line != '\n' && i < 511) {
            line_buf[i++] = *line++;
        }
        line_buf[i] = '\0';
        if (*line == '\n') line++;
        
        // Parse the line
        if (sscanf(line_buf, "owner: %s", owner) == 1) continue;
        if (sscanf(line_buf, "created: %ld", &created) == 1) continue;
        if (sscanf(line_buf, "modified: %ld", &modified) == 1) continue;
        if (sscanf(line_buf, "last_accessed: %ld", &last_accessed) == 1) continue;
        if (sscanf(line_buf, "last_accessed_by: %s", last_accessed_by) == 1) continue;
        if (sscanf(line_buf, "size: %ld", &size) == 1) continue;
        if (sscanf(line_buf, "word_count: %d", &word_count) == 1) continue;
        if (sscanf(line_buf, "char_count: %d", &char_count) == 1) continue;
        if (strncmp(line_buf, "read_access: ", 13) == 0) {
            strncpy(read_users, line_buf + 13, MAX_BUFFER_LEN - 1);
        }
        if (strncmp(line_buf, "write_access: ", 14) == 0) {
            strncpy(write_users, line_buf + 14, MAX_BUFFER_LEN - 1);
        }
        if (strncmp(line_buf, "exec_access: ", 13) == 0) {
            strncpy(exec_users, line_buf + 13, MAX_BUFFER_LEN - 1);
        }
    }
    
    pthread_mutex_lock(&ns.file_index->lock);
    
    // Check if file already exists
    FileInfo* file = ns_file_index_get(path);
    
    if (file != NULL) {
        // File exists, just add this SS and update metadata
        printf("NS: File '%s' already indexed, adding SS %s\n", path, ss->ip);
        ns_file_add_ss(file, ss);
        
        // Update metadata if this info is newer
        if (modified > file->modified) {
            ns_update_file_metadata(file, info_content);
            // Invalidate cache to ensure fresh data
            lru_cache_invalidate(ns.lru_cache, path);
        }
    } else {
        // New file, create and add to index
        printf("NS: Indexing new file '%s' from SS %s (Owner: %s)\n", path, ss->ip, owner);
        
        unsigned long index = hash_path(path);
        
        FileInfo* new_file = (FileInfo*)calloc(1, sizeof(FileInfo));
        strncpy(new_file->path, path, MAX_PATH_LEN);
        strncpy(new_file->owner, owner, MAX_USERNAME_LEN);
        
        // Set metadata
        new_file->created = created;
        new_file->modified = modified;
        new_file->last_accessed = last_accessed;
        strncpy(new_file->last_accessed_by, last_accessed_by, MAX_USERNAME_LEN);
        new_file->size = size;
        new_file->word_count = word_count;
        new_file->char_count = char_count;
        
        // Parse access lists from comma-separated strings
        new_file->read_access = NULL;
        new_file->read_count = 0;
        new_file->write_access = NULL;
        new_file->write_count = 0;
        new_file->exec_access = NULL;
        new_file->exec_count = 0;
        
        // Parse read users
        if (strlen(read_users) > 0) {
            char* read_copy = strdup(read_users);
            char* user = strtok(read_copy, ",");
            while (user != NULL) {
                while (*user == ' ') user++; // Trim leading space
                if (strlen(user) > 0) {
                    new_file->read_access = (char**)realloc(new_file->read_access,
                                                           (new_file->read_count + 1) * sizeof(char*));
                    new_file->read_access[new_file->read_count] = strdup(user);
                    new_file->read_count++;
                }
                user = strtok(NULL, ",");
            }
            free(read_copy);
        }
        
        // Parse write users
        if (strlen(write_users) > 0) {
            char* write_copy = strdup(write_users);
            char* user = strtok(write_copy, ",");
            while (user != NULL) {
                while (*user == ' ') user++;
                if (strlen(user) > 0) {
                    new_file->write_access = (char**)realloc(new_file->write_access,
                                                            (new_file->write_count + 1) * sizeof(char*));
                    new_file->write_access[new_file->write_count] = strdup(user);
                    new_file->write_count++;
                }
                user = strtok(NULL, ",");
            }
            free(write_copy);
        }
        
        // Parse exec users
        if (strlen(exec_users) > 0) {
            char* exec_copy = strdup(exec_users);
            char* user = strtok(exec_copy, ",");
            while (user != NULL) {
                while (*user == ' ') user++;
                if (strlen(user) > 0) {
                    new_file->exec_access = (char**)realloc(new_file->exec_access,
                                                           (new_file->exec_count + 1) * sizeof(char*));
                    new_file->exec_access[new_file->exec_count] = strdup(user);
                    new_file->exec_count++;
                }
                user = strtok(NULL, ",");
            }
            free(exec_copy);
        }
        
        // Add the storage server
        new_file->ss_list = (SSInfo**)malloc(sizeof(SSInfo*));
        new_file->ss_list[0] = ss;
        new_file->ss_count = 1;
        
        // Insert into hash map
        new_file->next = ns.file_index->table[index];
        ns.file_index->table[index] = new_file;
    }
    
    pthread_mutex_unlock(&ns.file_index->lock);
}

// --- NEW FUNCTION ---
// Iterates the entire file index and removes all references to
// the dead_ss. If a file's ss_count drops to 0, it is deleted.
void ns_cleanup_ss_from_index(SSInfo* dead_ss) {
    printf("NS: Cleaning up SS %s from file index...\n", dead_ss->ip);
    
    // Clear the entire cache to avoid dangling SS pointers
    lru_cache_clear(ns.lru_cache);
    
    pthread_mutex_lock(&ns.file_index->lock);

    for (int i = 0; i < FILE_INDEX_SIZE; i++) {
        FileInfo* current = ns.file_index->table[i];
        FileInfo* prev = NULL;

        while (current != NULL) {
            int ss_index_to_remove = -1;
            
            // 1. Find the dead SS in this file's list
            for (int j = 0; j < current->ss_count; j++) {
                if (current->ss_list[j] == dead_ss) {
                    ss_index_to_remove = j;
                    break;
                }
            }

            // 2. If it was found, remove it
            if (ss_index_to_remove != -1) {
                // Shift all subsequent elements left
                for (int k = ss_index_to_remove; k < current->ss_count - 1; k++) {
                    current->ss_list[k] = current->ss_list[k+1];
                }
                current->ss_count--;
                printf("NS: Removed dead SS from file '%s' (ss_count now %d)\n", 
                       current->path, current->ss_count);
            }

            // 3. Check if the file is now orphaned
            if (current->ss_count == 0) {
                printf("NS: File '%s' is orphaned. Deleting from index.\n", current->path);
                
                // Remove from hash map chain
                if (prev == NULL) {
                    // It was the head of the list
                    ns.file_index->table[i] = current->next;
                } else {
                    // It was in the middle
                    prev->next = current->next;
                }
                
                FileInfo* to_free = current;
                current = current->next; // Move to next node

                // Free the orphaned file's memory
                // TODO: Free access lists when they are implemented
                free(to_free->ss_list);
                free(to_free);
                
                continue; // Skip the "prev = current" at the end
            }

            prev = current;
            current = current->next;
        }
    }
    
    pthread_mutex_unlock(&ns.file_index->lock);
    printf("NS: Cleanup for SS %s complete.\n", dead_ss->ip);
}

// --- Add partner SS to all files served by source SS ---
void ns_add_partner_to_files(SSInfo* source_ss, SSInfo* partner_ss) {
    if (source_ss == NULL || partner_ss == NULL) {
        return;
    }
    
    printf("NS: Adding partner '%s' to all files of '%s'\n", 
           partner_ss->name, source_ss->name);
    
    pthread_mutex_lock(&ns.file_index->lock);
    
    int files_updated = 0;
    for (int i = 0; i < FILE_INDEX_SIZE; i++) {
        FileInfo* current = ns.file_index->table[i];
        
        while (current != NULL) {
            // Check if this file is served by source_ss
            int has_source = 0;
            int has_partner = 0;
            
            for (int j = 0; j < current->ss_count; j++) {
                if (current->ss_list[j] == source_ss) {
                    has_source = 1;
                }
                if (current->ss_list[j] == partner_ss) {
                    has_partner = 1;
                }
            }
            
            // If file has source but not partner, add partner
            if (has_source && !has_partner) {
                ns_file_add_ss(current, partner_ss);
                files_updated++;
            }
            
            current = current->next;
        }
    }
    
    pthread_mutex_unlock(&ns.file_index->lock);
    printf("NS: Added partner to %d files\n", files_updated);
}

// --- Update file metadata from info_content ---
void ns_update_file_metadata(FileInfo* file, const char* info_content) {
    time_t created = 0, modified = 0, last_accessed = 0;
    char last_accessed_by[MAX_USERNAME_LEN] = "";
    long size = 0;
    int word_count = 0, char_count = 0;
    
    const char* line = info_content;
    char line_buf[512];
    
    while (*line) {
        int i = 0;
        while (*line && *line != '\n' && i < 511) {
            line_buf[i++] = *line++;
        }
        line_buf[i] = '\0';
        if (*line == '\n') line++;
        
        if (sscanf(line_buf, "created: %ld", &created) == 1) continue;
        if (sscanf(line_buf, "modified: %ld", &modified) == 1) continue;
        if (sscanf(line_buf, "last_accessed: %ld", &last_accessed) == 1) continue;
        if (sscanf(line_buf, "last_accessed_by: %s", last_accessed_by) == 1) continue;
        if (sscanf(line_buf, "size: %ld", &size) == 1) continue;
        if (sscanf(line_buf, "word_count: %d", &word_count) == 1) continue;
        if (sscanf(line_buf, "char_count: %d", &char_count) == 1) continue;
    }
    
    if (created > 0) file->created = created;
    if (modified > 0) file->modified = modified;
    if (last_accessed > 0) file->last_accessed = last_accessed;
    if (strlen(last_accessed_by) > 0) strncpy(file->last_accessed_by, last_accessed_by, MAX_USERNAME_LEN);
    file->size = size;
    file->word_count = word_count;
    file->char_count = char_count;
    
    printf("NS: Updated metadata for '%s': modified=%ld, last_accessed=%ld by '%s'\n",
           file->path, file->modified, file->last_accessed, file->last_accessed_by);
}

// --- Broadcast info file update to all SSs that have this file ---
void ns_broadcast_info_update(FileInfo* file) {
    // Copy file path and SS list to avoid races (caller should NOT hold file_index lock)
    char path[MAX_PATH_LEN];
    strncpy(path, file->path, MAX_PATH_LEN);
    
    int ss_count = file->ss_count;
    SSInfo* ss_list[ss_count];
    for (int i = 0; i < ss_count; i++) {
        ss_list[i] = file->ss_list[i];
    }
    
    // Generate info content from FileInfo
    char info_content[MAX_INFO_LEN];
    int len = snprintf(info_content, MAX_INFO_LEN,
             "owner: %s\n"
             "created: %ld\n"
             "modified: %ld\n"
             "last_accessed: %ld\n"
             "last_accessed_by: %s\n"
             "size: %ld\n"
             "word_count: %d\n"
             "char_count: %d\n",
             file->owner,
             file->created,
             file->modified,
             file->last_accessed,
             file->last_accessed_by,
             file->size,
             file->word_count,
             file->char_count);
    
    // Add access lists (comma-separated)
    if (file->read_count > 0 && len < MAX_INFO_LEN - 20) {
        len += snprintf(info_content + len, MAX_INFO_LEN - len, "read_access: ");
        for (int i = 0; i < file->read_count && len < MAX_INFO_LEN - 50; i++) {
            if (i > 0) len += snprintf(info_content + len, MAX_INFO_LEN - len, ",");
            len += snprintf(info_content + len, MAX_INFO_LEN - len, "%s", file->read_access[i]);
        }
        len += snprintf(info_content + len, MAX_INFO_LEN - len, "\n");
    }
    
    if (file->write_count > 0 && len < MAX_INFO_LEN - 20) {
        len += snprintf(info_content + len, MAX_INFO_LEN - len, "write_access: ");
        for (int i = 0; i < file->write_count && len < MAX_INFO_LEN - 50; i++) {
            if (i > 0) len += snprintf(info_content + len, MAX_INFO_LEN - len, ",");
            len += snprintf(info_content + len, MAX_INFO_LEN - len, "%s", file->write_access[i]);
        }
        len += snprintf(info_content + len, MAX_INFO_LEN - len, "\n");
    }
    
    if (file->exec_count > 0 && len < MAX_INFO_LEN - 20) {
        len += snprintf(info_content + len, MAX_INFO_LEN - len, "exec_access: ");
        for (int i = 0; i < file->exec_count && len < MAX_INFO_LEN - 50; i++) {
            if (i > 0) len += snprintf(info_content + len, MAX_INFO_LEN - len, ",");
            len += snprintf(info_content + len, MAX_INFO_LEN - len, "%s", file->exec_access[i]);
        }
        len += snprintf(info_content + len, MAX_INFO_LEN - len, "\n");
    }
    
    // Send update to all active SSs (using copied SS list)
    for (int i = 0; i < ss_count; i++) {
        if (ss_list[i]->is_active) {
            NSOpCode op = NS_SS_UPDATE_INFO;
            
            pthread_mutex_lock(&ss_list[i]->cmd_lock);
            SEND_OPCODE(ss_list[i]->cmd_sock, op);
            send_full(ss_list[i]->cmd_sock, path, MAX_PATH_LEN);
            send_full(ss_list[i]->cmd_sock, info_content, MAX_INFO_LEN);
            
            // Receive acknowledgment
            ServerResponse ss_res;
            RECV_SERVER_RESPONSE(ss_list[i]->cmd_sock, &ss_res);
            pthread_mutex_unlock(&ss_list[i]->cmd_lock);
            
            printf("NS: Sent info update for '%s' to SS %s: %s\n", 
                   path, ss_list[i]->ip,
                   (ss_res.status == ERR_OK) ? "OK" : "FAILED");
        }
    }
}

// --- Access Control Functions ---

// Check if user has permission of given type for the file
// Returns: 1 if has permission, 0 if not
int ns_check_permission(FileInfo* file, const char* username, char type) {
    // Owner always has full access
    if (strcmp(file->owner, username) == 0) {
        return 1;
    }
    
    // Check appropriate access list
    char** access_list = NULL;
    int count = 0;
    
    if (type == 'R') {
        access_list = file->read_access;
        count = file->read_count;
    } else if (type == 'W') {
        access_list = file->write_access;
        count = file->write_count;
    } else if (type == 'X') {
        access_list = file->exec_access;
        count = file->exec_count;
    }
    
    for (int i = 0; i < count; i++) {
        if (strcmp(access_list[i], username) == 0) {
            return 1;
        }
    }
    
    return 0;
}

// Helper: Check if user has read access (owner or explicit read/write access)
int ns_has_read_access(FileInfo* file, const char* username) {
    if (strcmp(file->owner, username) == 0) return 1;
    
    // Check read access list
    for (int i = 0; i < file->read_count; i++) {
        if (strcmp(file->read_access[i], username) == 0) return 1;
    }
    
    // Check write access list (write implies read)
    for (int i = 0; i < file->write_count; i++) {
        if (strcmp(file->write_access[i], username) == 0) return 1;
    }
    
    return 0;
}

// Helper: Check if user has write access (owner or explicit write access)
int ns_has_write_access(FileInfo* file, const char* username) {
    if (strcmp(file->owner, username) == 0) return 1;
    
    for (int i = 0; i < file->write_count; i++) {
        if (strcmp(file->write_access[i], username) == 0) return 1;
    }
    
    return 0;
}

// Add access permission for a user
// Returns: ERR_OK on success, ERR_FILE_EXISTS if already has access
ErrorCode ns_add_access(FileInfo* file, const char* username, char type) {
    char*** access_list_ptr = NULL;
    int* count_ptr = NULL;
    
    if (type == 'R') {
        access_list_ptr = &file->read_access;
        count_ptr = &file->read_count;
    } else if (type == 'W') {
        access_list_ptr = &file->write_access;
        count_ptr = &file->write_count;
    } else if (type == 'X') {
        access_list_ptr = &file->exec_access;
        count_ptr = &file->exec_count;
    } else {
        return ERR_INVALID_PATH; // Invalid type
    }
    
    // Check if user already has this access
    for (int i = 0; i < *count_ptr; i++) {
        if (strcmp((*access_list_ptr)[i], username) == 0) {
            return ERR_FILE_EXISTS; // Already has access
        }
    }
    
    // Add to list
    *access_list_ptr = (char**)realloc(*access_list_ptr, (*count_ptr + 1) * sizeof(char*));
    (*access_list_ptr)[*count_ptr] = strdup(username);
    (*count_ptr)++;
    
    return ERR_OK;
}

// Remove access permission for a user
// Returns: ERR_OK on success, ERR_FILE_NOT_FOUND if user doesn't have access
ErrorCode ns_remove_access(FileInfo* file, const char* username, char type) {
    char*** access_list_ptr = NULL;
    int* count_ptr = NULL;
    
    if (type == 'R') {
        access_list_ptr = &file->read_access;
        count_ptr = &file->read_count;
    } else if (type == 'W') {
        access_list_ptr = &file->write_access;
        count_ptr = &file->write_count;
    } else if (type == 'X') {
        access_list_ptr = &file->exec_access;
        count_ptr = &file->exec_count;
    } else {
        return ERR_INVALID_PATH;
    }
    
    // Find and remove user
    for (int i = 0; i < *count_ptr; i++) {
        if (strcmp((*access_list_ptr)[i], username) == 0) {
            free((*access_list_ptr)[i]);
            // Shift remaining elements
            for (int j = i; j < *count_ptr - 1; j++) {
                (*access_list_ptr)[j] = (*access_list_ptr)[j + 1];
            }
            (*count_ptr)--;
            return ERR_OK;
        }
    }
    
    return ERR_FILE_NOT_FOUND; // User didn't have access
}

// Add an access request
// Returns: request ID
int ns_add_access_request(const char* username, const char* path, char type) {
    pthread_mutex_lock(&ns.access_requests->lock);
    
    AccessRequest* req = (AccessRequest*)malloc(sizeof(AccessRequest));
    req->id = ns.access_requests->next_id++;
    strncpy(req->username, username, MAX_USERNAME_LEN - 1);
    req->username[MAX_USERNAME_LEN - 1] = '\0';
    strncpy(req->path, path, MAX_PATH_LEN - 1);
    req->path[MAX_PATH_LEN - 1] = '\0';
    req->type = type;
    
    // Add to head of list
    req->next = ns.access_requests->head;
    ns.access_requests->head = req;
    
    int id = req->id;
    pthread_mutex_unlock(&ns.access_requests->lock);
    
    return id;
}

// Get an access request by ID
AccessRequest* ns_get_access_request(int request_id) {
    pthread_mutex_lock(&ns.access_requests->lock);
    
    AccessRequest* current = ns.access_requests->head;
    while (current != NULL) {
        if (current->id == request_id) {
            pthread_mutex_unlock(&ns.access_requests->lock);
            return current;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&ns.access_requests->lock);
    return NULL;
}

// Remove an access request by ID
void ns_remove_access_request(int request_id) {
    pthread_mutex_lock(&ns.access_requests->lock);
    
    AccessRequest* current = ns.access_requests->head;
    AccessRequest* prev = NULL;
    
    while (current != NULL) {
        if (current->id == request_id) {
            if (prev == NULL) {
                ns.access_requests->head = current->next;
            } else {
                prev->next = current->next;
            }
            free(current);
            pthread_mutex_unlock(&ns.access_requests->lock);
            return;
        }
        prev = current;
        current = current->next;
    }
    
    pthread_mutex_unlock(&ns.access_requests->lock);
}

// ==================== PERSISTENCE FUNCTIONS ====================

// Save all_users list to all_users.txt
void ns_save_all_users() {
    FILE* f = fopen("nameserver_data/all_users.txt", "w");
    if (f == NULL) {
        printf("NS: Failed to save all_users.txt\n");
        return;
    }
    
    pthread_mutex_lock(&ns.all_clients->lock);
    
    ClientInfo* current = ns.all_clients->head;
    while (current != NULL) {
        fprintf(f, "%s\n", current->username);
        current = current->next;
    }
    
    pthread_mutex_unlock(&ns.all_clients->lock);
    fclose(f);
    printf("NS: Saved all_users list\n");
}

// Load all_users list from all_users.txt
void ns_load_all_users() {
    FILE* f = fopen("nameserver_data/all_users.txt", "r");
    if (f == NULL) {
        printf("NS: No existing all_users.txt, starting fresh\n");
        return;
    }
    
    char username[MAX_USERNAME_LEN];
    while (fgets(username, sizeof(username), f) != NULL) {
        // Remove newline
        username[strcspn(username, "\n")] = 0;
        
        if (strlen(username) > 0) {
            ns_add_client(ns.all_clients, username, -1); // -1 sock since not connected
        }
    }
    
    fclose(f);
    printf("NS: Loaded all_users list\n");
}

// Save access requests to access_requests.txt
void ns_save_access_requests() {
    FILE* f = fopen("nameserver_data/access_requests.txt", "w");
    if (f == NULL) {
        printf("NS: Failed to save access_requests.txt\n");
        return;
    }
    
    pthread_mutex_lock(&ns.access_requests->lock);
    
    // Save next_id first
    fprintf(f, "NEXT_ID:%d\n", ns.access_requests->next_id);
    
    // Save each request
    AccessRequest* current = ns.access_requests->head;
    while (current != NULL) {
        fprintf(f, "%d,%s,%s,%c\n", current->id, current->username, current->path, current->type);
        current = current->next;
    }
    
    pthread_mutex_unlock(&ns.access_requests->lock);
    fclose(f);
    printf("NS: Saved access requests\n");
}

// Load access requests from access_requests.txt
void ns_load_access_requests() {
    FILE* f = fopen("nameserver_data/access_requests.txt", "r");
    if (f == NULL) {
        printf("NS: No existing access_requests.txt, starting fresh\n");
        return;
    }
    
    char line[MAX_PATH_LEN + MAX_USERNAME_LEN + 20];
    
    // Read next_id
    if (fgets(line, sizeof(line), f) != NULL) {
        if (sscanf(line, "NEXT_ID:%d", &ns.access_requests->next_id) != 1) {
            ns.access_requests->next_id = 1;
        }
    }
    
    // Read each request
    while (fgets(line, sizeof(line), f) != NULL) {
        AccessRequest* req = (AccessRequest*)malloc(sizeof(AccessRequest));
        if (req == NULL) continue;
        
        if (sscanf(line, "%d,%[^,],%[^,],%c", &req->id, req->username, req->path, &req->type) == 4) {
            pthread_mutex_lock(&ns.access_requests->lock);
            req->next = ns.access_requests->head;
            ns.access_requests->head = req;
            pthread_mutex_unlock(&ns.access_requests->lock);
        } else {
            free(req);
        }
    }
    
    fclose(f);
    printf("NS: Loaded access requests\n");
}

// ============================================================================
// LRU Cache Implementation
// ============================================================================

// Helper function to create a deep copy of FileInfo
static FileInfo* copy_file_info(FileInfo* src) {
    if (src == NULL) return NULL;
    
    FileInfo* copy = (FileInfo*)calloc(1, sizeof(FileInfo));
    strncpy(copy->path, src->path, MAX_PATH_LEN);
    strncpy(copy->owner, src->owner, MAX_USERNAME_LEN);
    
    copy->created = src->created;
    copy->modified = src->modified;
    copy->last_accessed = src->last_accessed;
    strncpy(copy->last_accessed_by, src->last_accessed_by, MAX_USERNAME_LEN);
    copy->size = src->size;
    copy->word_count = src->word_count;
    copy->char_count = src->char_count;
    
    // Copy access lists
    if (src->read_count > 0) {
        copy->read_access = (char**)malloc(src->read_count * sizeof(char*));
        copy->read_count = src->read_count;
        for (int i = 0; i < src->read_count; i++) {
            copy->read_access[i] = strdup(src->read_access[i]);
        }
    }
    
    if (src->write_count > 0) {
        copy->write_access = (char**)malloc(src->write_count * sizeof(char*));
        copy->write_count = src->write_count;
        for (int i = 0; i < src->write_count; i++) {
            copy->write_access[i] = strdup(src->write_access[i]);
        }
    }
    
    if (src->exec_count > 0) {
        copy->exec_access = (char**)malloc(src->exec_count * sizeof(char*));
        copy->exec_count = src->exec_count;
        for (int i = 0; i < src->exec_count; i++) {
            copy->exec_access[i] = strdup(src->exec_access[i]);
        }
    }
    
    // Copy SS list
    if (src->ss_count > 0) {
        copy->ss_list = (SSInfo**)malloc(src->ss_count * sizeof(SSInfo*));
        copy->ss_count = src->ss_count;
        for (int i = 0; i < src->ss_count; i++) {
            copy->ss_list[i] = src->ss_list[i];  // Shallow copy of pointers
        }
    }
    
    return copy;
}

// Helper function to free cached FileInfo
void free_file_info_copy(FileInfo* file) {
    if (file == NULL) return;
    
    for (int i = 0; i < file->read_count; i++) {
        free(file->read_access[i]);
    }
    free(file->read_access);
    
    for (int i = 0; i < file->write_count; i++) {
        free(file->write_access[i]);
    }
    free(file->write_access);
    
    for (int i = 0; i < file->exec_count; i++) {
        free(file->exec_access[i]);
    }
    free(file->exec_access);
    
    free(file->ss_list);
    free(file);
}

// Initialize LRU cache
LRUCache* lru_cache_init() {
    LRUCache* cache = (LRUCache*)malloc(sizeof(LRUCache));
    cache->head = NULL;
    cache->tail = NULL;
    cache->size = 0;
    pthread_mutex_init(&cache->lock, NULL);
    return cache;
}

// Move node to head (most recently used)
static void move_to_head(LRUCache* cache, LRUNode* node) {
    if (node == cache->head) return;  // Already at head
    
    // Remove from current position
    if (node->prev) node->prev->next = node->next;
    if (node->next) node->next->prev = node->prev;
    if (node == cache->tail) cache->tail = node->prev;
    
    // Insert at head
    node->prev = NULL;
    node->next = cache->head;
    if (cache->head) cache->head->prev = node;
    cache->head = node;
    if (cache->tail == NULL) cache->tail = node;
}

// Get file from cache (returns copy, caller must free)
FileInfo* lru_cache_get(LRUCache* cache, const char* path) {
    pthread_mutex_lock(&cache->lock);
    
    LRUNode* current = cache->head;
    while (current != NULL) {
        if (strcmp(current->path, path) == 0) {
            // Cache hit - move to head and return copy
            move_to_head(cache, current);
            FileInfo* result = copy_file_info(current->file_info);
            pthread_mutex_unlock(&cache->lock);
            printf("NS: LRU Cache HIT for '%s'\n", path);
            return result;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&cache->lock);
    printf("NS: LRU Cache MISS for '%s'\n", path);
    return NULL;  // Cache miss
}

// Put file into cache
void lru_cache_put(LRUCache* cache, const char* path, FileInfo* file) {
    pthread_mutex_lock(&cache->lock);
    
    // Check if already exists
    LRUNode* current = cache->head;
    while (current != NULL) {
        if (strcmp(current->path, path) == 0) {
            // Update existing entry
            free_file_info_copy(current->file_info);
            current->file_info = copy_file_info(file);
            move_to_head(cache, current);
            pthread_mutex_unlock(&cache->lock);
            printf("NS: LRU Cache UPDATED '%s'\n", path);
            return;
        }
        current = current->next;
    }
    
    // Create new node
    LRUNode* new_node = (LRUNode*)malloc(sizeof(LRUNode));
    strncpy(new_node->path, path, MAX_PATH_LEN);
    new_node->file_info = copy_file_info(file);
    new_node->prev = NULL;
    new_node->next = cache->head;
    
    if (cache->head) cache->head->prev = new_node;
    cache->head = new_node;
    if (cache->tail == NULL) cache->tail = new_node;
    
    cache->size++;
    
    // Evict LRU if cache is full
    if (cache->size > LRU_CACHE_SIZE) {
        LRUNode* lru = cache->tail;
        cache->tail = lru->prev;
        if (cache->tail) cache->tail->next = NULL;
        
        printf("NS: LRU Cache EVICTING '%s'\n", lru->path);
        free_file_info_copy(lru->file_info);
        free(lru);
        cache->size--;
    }
    
    pthread_mutex_unlock(&cache->lock);
    printf("NS: LRU Cache ADDED '%s'\n", path);
}

// Invalidate cache entry (called when file is modified/deleted)
void lru_cache_invalidate(LRUCache* cache, const char* path) {
    pthread_mutex_lock(&cache->lock);
    
    LRUNode* current = cache->head;
    while (current != NULL) {
        if (strcmp(current->path, path) == 0) {
            // Remove from list
            if (current->prev) current->prev->next = current->next;
            if (current->next) current->next->prev = current->prev;
            if (current == cache->head) cache->head = current->next;
            if (current == cache->tail) cache->tail = current->prev;
            
            free_file_info_copy(current->file_info);
            free(current);
            cache->size--;
            
            pthread_mutex_unlock(&cache->lock);
            printf("NS: LRU Cache INVALIDATED '%s'\n", path);
            return;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&cache->lock);
}

// Clear all entries from cache (used when SS disconnects to avoid dangling pointers)
void lru_cache_clear(LRUCache* cache) {
    pthread_mutex_lock(&cache->lock);
    
    LRUNode* current = cache->head;
    while (current != NULL) {
        LRUNode* next = current->next;
        free_file_info_copy(current->file_info);
        free(current);
        current = next;
    }
    
    cache->head = NULL;
    cache->tail = NULL;
    cache->size = 0;
    
    pthread_mutex_unlock(&cache->lock);
    printf("NS: LRU Cache CLEARED (all entries invalidated)\n");
}

// Free entire cache
void lru_cache_free(LRUCache* cache) {
    if (cache == NULL) return;
    
    LRUNode* current = cache->head;
    while (current != NULL) {
        LRUNode* next = current->next;
        free_file_info_copy(current->file_info);
        free(current);
        current = next;
    }
    
    pthread_mutex_destroy(&cache->lock);
    free(cache);
}
