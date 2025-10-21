#include "../include/ns.h"
#include <stdarg.h>

void init_naming_server() {
    memset(&ns, 0, sizeof(naming_server));
    pthread_mutex_init(&ns.lock, NULL);
    
    ns.log_file = fopen(LOG_FILE, "a");
    if (!ns.log_file) {
        perror("Failed to open log file");
        exit(1);
    }
    
    log_message("Naming Server initialized");
}

void log_message(const char *format, ...) {
    time_t now;
    time(&now);
    char timestamp[26];
    strncpy(timestamp, ctime(&now), 25);
    timestamp[24] = '\0';  // Remove newline
    
    pthread_mutex_lock(&ns.lock);
    
    fprintf(ns.log_file, "[%s] ", timestamp);
    
    va_list args;
    va_start(args, format);
    vfprintf(ns.log_file, format, args);
    va_end(args);
    
    fprintf(ns.log_file, "\n");
    fflush(ns.log_file);
    
    pthread_mutex_unlock(&ns.lock);
}

int find_user(const char *username) {
    for (int i = 0; i < ns.user_count; i++) {
        if (strcmp(ns.users[i].username, username) == 0) {
            return i;
        }
    }
    return -1;
}

int add_user(const char *username) {
    pthread_mutex_lock(&ns.lock);
    
    int idx = find_user(username);
    if (idx >= 0) {
        // User exists, mark as active
        ns.users[idx].active = 1;
        pthread_mutex_unlock(&ns.lock);
        return idx;
    }
    
    // Add new user
    if (ns.user_count >= MAX_USERS) {
        pthread_mutex_unlock(&ns.lock);
        return -1;
    }
    
    strncpy(ns.users[ns.user_count].username, username, MAX_USERNAME - 1);
    ns.users[ns.user_count].active = 1;
    ns.users[ns.user_count].user_index = ns.user_count;
    ns.user_count++;
    
    pthread_mutex_unlock(&ns.lock);
    return ns.user_count - 1;
}

int find_file(const char *filepath) {
    for (int i = 0; i < ns.file_count; i++) {
        if (strcmp(ns.files[i].filepath, filepath) == 0) {
            return i;
        }
    }
    return -1;
}

int check_read_permission(int file_idx, int user_idx) {
    if (file_idx < 0 || file_idx >= ns.file_count || user_idx < 0 || user_idx >= ns.user_count) {
        return 0;
    }
    
    // Owner has all permissions
    if (strcmp(ns.files[file_idx].owner, ns.users[user_idx].username) == 0) {
        return 1;
    }
    
    return ns.files[file_idx].read_access[user_idx];
}

int check_write_permission(int file_idx, int user_idx) {
    if (file_idx < 0 || file_idx >= ns.file_count || user_idx < 0 || user_idx >= ns.user_count) {
        return 0;
    }
    
    // Owner has all permissions
    if (strcmp(ns.files[file_idx].owner, ns.users[user_idx].username) == 0) {
        return 1;
    }
    
    return ns.files[file_idx].write_access[user_idx];
}

operation_type parse_operation(const char *command) {
    if (strncmp(command, "REGISTER_CLIENT", 15) == 0) return OP_REGISTER_CLIENT;
    if (strncmp(command, "REGISTER_SS", 11) == 0) return OP_REGISTER_SS;
    if (strncmp(command, "CREATE", 6) == 0) return OP_CREATE;
    if (strncmp(command, "DELETE", 6) == 0) return OP_DELETE;
    if (strncmp(command, "READ", 4) == 0) return OP_READ;
    if (strncmp(command, "WRITE", 5) == 0) return OP_WRITE;
    if (strncmp(command, "STREAM", 6) == 0) return OP_STREAM;
    if (strncmp(command, "VIEW -al", 8) == 0) return OP_VIEW_DETAILS;
    if (strncmp(command, "VIEW -la", 8) == 0) return OP_VIEW_DETAILS;
    if (strncmp(command, "VIEW -a", 7) == 0) return OP_VIEW_ALL;
    if (strncmp(command, "VIEW -l", 7) == 0) return OP_VIEW_DETAILS;
    if (strncmp(command, "VIEW", 4) == 0) return OP_VIEW;
    if (strncmp(command, "INFO", 4) == 0) return OP_INFO;
    if (strncmp(command, "LIST", 4) == 0) return OP_LIST;
    if (strncmp(command, "ADDACCESS -R", 12) == 0) return OP_ADDACCESS_R;
    if (strncmp(command, "ADDACCESS -W", 12) == 0) return OP_ADDACCESS_W;
    if (strncmp(command, "REMACCESS", 9) == 0) return OP_REMACCESS;
    if (strncmp(command, "EXEC", 4) == 0) return OP_EXEC;
    if (strncmp(command, "UNDO", 4) == 0) return OP_UNDO;
    return OP_UNKNOWN;
}
