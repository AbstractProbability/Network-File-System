#include "../include/ns.h"

void *handle_connection(void *arg) {
    int client_fd = *((int *)arg);
    free(arg);
    
    char buffer[4096];
    memset(buffer, 0, sizeof(buffer));
    
    // First message should identify the connection type
    int bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read <= 0) {
        close(client_fd);
        return NULL;
    }
    
    buffer[bytes_read] = '\0';
    log_message("Received: %s", buffer);
    
    operation_type op = parse_operation(buffer);
    
    if (op == OP_REGISTER_SS) {
        handle_ss_registration(client_fd);
    } else if (op == OP_REGISTER_CLIENT) {
        // Extract username
        char username[MAX_USERNAME];
        if (sscanf(buffer, "REGISTER_CLIENT %s", username) == 1) {
            int user_idx = add_user(username);
            if (user_idx >= 0) {
                log_message("Client registered: %s", username);
                send(client_fd, "OK\n", 3, 0);
                handle_client_request(client_fd, username);
            } else {
                send(client_fd, "ERROR: User limit reached\n", 26, 0);
            }
        } else {
            send(client_fd, "ERROR: Invalid registration\n", 28, 0);
        }
    } else {
        send(client_fd, "ERROR: Unknown connection type\n", 31, 0);
    }
    
    close(client_fd);
    return NULL;
}

void handle_ss_registration(int ss_fd) {
    char buffer[4096];
    storage_server ss_info;
    
    // Receive SS details
    memset(buffer, 0, sizeof(buffer));
    int bytes = recv(ss_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0) {
        return;
    }
    
    // Parse: REGISTER_SS <ip> <nm_port> <client_port> <num_files>
    int num_files;
    if (sscanf(buffer, "REGISTER_SS %s %d %d %d", 
               ss_info.ip, &ss_info.nm_port, &ss_info.client_port, &num_files) != 4) {
        send(ss_fd, "ERROR: Invalid SS registration\n", 31, 0);
        return;
    }
    
    pthread_mutex_lock(&ns.lock);
    
    if (ns.ss_count >= MAX_STORAGE_SERVERS) {
        pthread_mutex_unlock(&ns.lock);
        send(ss_fd, "ERROR: SS limit reached\n", 24, 0);
        return;
    }
    
    ss_info.id = ns.ss_count;
    ss_info.active = 1;
    ns.ss_list[ns.ss_count] = ss_info;
    int ss_id = ns.ss_count;
    ns.ss_count++;
    
    pthread_mutex_unlock(&ns.lock);
    
    log_message("Storage Server registered: %s:%d (SS_ID=%d)", 
                ss_info.ip, ss_info.nm_port, ss_id);
    
    // Send acknowledgement
    send(ss_fd, "OK\n", 3, 0);
    
    // Receive file list
    for (int i = 0; i < num_files; i++) {
        memset(buffer, 0, sizeof(buffer));
        bytes = recv(ss_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) break;
        
        // Parse: FILE <filepath> <owner> <size>
        char filepath[MAX_FILENAME], owner[MAX_USERNAME];
        int size;
        if (sscanf(buffer, "FILE %s %s %d", filepath, owner, &size) == 3) {
            pthread_mutex_lock(&ns.lock);
            
            if (ns.file_count < MAX_FILES_PER_SS * MAX_STORAGE_SERVERS) {
                file_metadata *file = &ns.files[ns.file_count];
                strncpy(file->filepath, filepath, MAX_FILENAME - 1);
                strncpy(file->owner, owner, MAX_USERNAME - 1);
                file->ss_id = ss_id;
                file->size = size;
                time(&file->last_access);
                
                // Owner gets full access
                int owner_idx = find_user(owner);
                if (owner_idx < 0) {
                    owner_idx = add_user(owner);
                }
                if (owner_idx >= 0) {
                    file->read_access[owner_idx] = 1;
                    file->write_access[owner_idx] = 1;
                }
                
                ns.file_count++;
                log_message("File registered: %s (owner=%s, ss=%d)", filepath, owner, ss_id);
            }
            
            pthread_mutex_unlock(&ns.lock);
        }
        
        send(ss_fd, "OK\n", 3, 0);
    }
    
    log_message("SS registration complete for SS_ID=%d", ss_id);
}

void handle_client_request(int client_fd, char *username) {
    char buffer[4096];
    char response[4096];
    int user_idx = find_user(username);
    
    if (user_idx < 0) {
        send(client_fd, "ERROR: User not found\n", 22, 0);
        return;
    }
    
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        int bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes <= 0) {
            // Client disconnected
            pthread_mutex_lock(&ns.lock);
            ns.users[user_idx].active = 0;
            pthread_mutex_unlock(&ns.lock);
            log_message("Client disconnected: %s", username);
            break;
        }
        
        buffer[bytes] = '\0';
        log_message("Request from %s: %s", username, buffer);
        
        operation_type op = parse_operation(buffer);
        
        switch (op) {
            case OP_LIST: {
                // List all users
                memset(response, 0, sizeof(response));
                strcat(response, "USERS:\n");
                for (int i = 0; i < ns.user_count; i++) {
                    if (ns.users[i].active) {
                        strcat(response, ns.users[i].username);
                        strcat(response, " (online)\n");
                    } else {
                        strcat(response, ns.users[i].username);
                        strcat(response, " (offline)\n");
                    }
                }
                send(client_fd, response, strlen(response), 0);
                log_message("Response to %s: LIST command", username);
                break;
            }
            
            case OP_VIEW:
            case OP_VIEW_ALL:
            case OP_VIEW_DETAILS: {
                // View files
                memset(response, 0, sizeof(response));
                strcat(response, "FILES:\n");
                
                for (int i = 0; i < ns.file_count; i++) {
                    int has_access = check_read_permission(i, user_idx);
                    
                    if (op == OP_VIEW && !has_access) continue;
                    
                    strcat(response, ns.files[i].filepath);
                    
                    if (op == OP_VIEW_DETAILS) {
                        char details[256];
                        snprintf(details, sizeof(details), " [owner=%s, size=%d, ss=%d]",
                                ns.files[i].owner, ns.files[i].size, ns.files[i].ss_id);
                        strcat(response, details);
                    }
                    
                    if (op == OP_VIEW_ALL && !has_access) {
                        strcat(response, " (no access)");
                    }
                    
                    strcat(response, "\n");
                }
                
                send(client_fd, response, strlen(response), 0);
                log_message("Response to %s: VIEW command", username);
                break;
            }
            
            case OP_READ:
            case OP_WRITE:
            case OP_STREAM: {
                // Offload to SS
                char filepath[MAX_FILENAME];
                sscanf(buffer, "%*s %s", filepath);
                
                int file_idx = find_file(filepath);
                if (file_idx < 0) {
                    send(client_fd, "ERROR: File not found\n", 22, 0);
                    log_message("Error: File not found - %s", filepath);
                    break;
                }
                
                // Check permissions
                int has_permission = 0;
                if (op == OP_READ || op == OP_STREAM) {
                    has_permission = check_read_permission(file_idx, user_idx);
                } else {
                    has_permission = check_write_permission(file_idx, user_idx);
                }
                
                if (!has_permission) {
                    send(client_fd, "ERROR: Permission denied\n", 25, 0);
                    log_message("Error: Permission denied for %s on %s", username, filepath);
                    break;
                }
                
                // Send SS info to client
                int ss_id = ns.files[file_idx].ss_id;
                snprintf(response, sizeof(response), "SS_INFO %s %d\n",
                        ns.ss_list[ss_id].ip, ns.ss_list[ss_id].client_port);
                send(client_fd, response, strlen(response), 0);
                log_message("Response to %s: Offloaded to SS %d", username, ss_id);
                break;
            }
            
            case OP_INFO: {
                // Display file info
                char filepath[MAX_FILENAME];
                sscanf(buffer, "INFO %s", filepath);
                
                int file_idx = find_file(filepath);
                if (file_idx < 0) {
                    send(client_fd, "ERROR: File not found\n", 22, 0);
                    break;
                }
                
                file_metadata *file = &ns.files[file_idx];
                snprintf(response, sizeof(response),
                        "File: %s\nOwner: %s\nSize: %d bytes\nLast Access: %s",
                        file->filepath, file->owner, file->size, ctime(&file->last_access));
                
                send(client_fd, response, strlen(response), 0);
                log_message("Response to %s: INFO for %s", username, filepath);
                break;
            }
            
            case OP_ADDACCESS_R:
            case OP_ADDACCESS_W: {
                char filepath[MAX_FILENAME], target_user[MAX_USERNAME];
                sscanf(buffer, "%*s %*s %s %s", filepath, target_user);
                
                int file_idx = find_file(filepath);
                if (file_idx < 0) {
                    send(client_fd, "ERROR: File not found\n", 22, 0);
                    break;
                }
                
                // Only owner can modify access
                if (strcmp(ns.files[file_idx].owner, username) != 0) {
                    send(client_fd, "ERROR: Only owner can modify access\n", 36, 0);
                    break;
                }
                
                int target_idx = find_user(target_user);
                if (target_idx < 0) {
                    target_idx = add_user(target_user);
                }
                
                if (target_idx >= 0) {
                    pthread_mutex_lock(&ns.lock);
                    if (op == OP_ADDACCESS_R) {
                        ns.files[file_idx].read_access[target_idx] = 1;
                    } else {
                        ns.files[file_idx].write_access[target_idx] = 1;
                    }
                    pthread_mutex_unlock(&ns.lock);
                    
                    send(client_fd, "OK: Access granted\n", 19, 0);
                    log_message("Access granted to %s for %s", target_user, filepath);
                } else {
                    send(client_fd, "ERROR: Could not add user\n", 26, 0);
                }
                break;
            }
            
            case OP_REMACCESS: {
                char filepath[MAX_FILENAME], target_user[MAX_USERNAME];
                sscanf(buffer, "REMACCESS %s %s", filepath, target_user);
                
                int file_idx = find_file(filepath);
                if (file_idx < 0) {
                    send(client_fd, "ERROR: File not found\n", 22, 0);
                    break;
                }
                
                // Only owner can modify access
                if (strcmp(ns.files[file_idx].owner, username) != 0) {
                    send(client_fd, "ERROR: Only owner can modify access\n", 36, 0);
                    break;
                }
                
                int target_idx = find_user(target_user);
                if (target_idx >= 0) {
                    pthread_mutex_lock(&ns.lock);
                    ns.files[file_idx].read_access[target_idx] = 0;
                    ns.files[file_idx].write_access[target_idx] = 0;
                    pthread_mutex_unlock(&ns.lock);
                    
                    send(client_fd, "OK: Access removed\n", 19, 0);
                    log_message("Access removed from %s for %s", target_user, filepath);
                } else {
                    send(client_fd, "ERROR: User not found\n", 22, 0);
                }
                break;
            }
            
            case OP_CREATE:
            case OP_DELETE:
            case OP_UNDO: {
                // Modulator mode - forward to SS
                char filepath[MAX_FILENAME];
                
                if (op == OP_CREATE) {
                    sscanf(buffer, "CREATE %s", filepath);
                } else if (op == OP_DELETE) {
                    sscanf(buffer, "DELETE %s", filepath);
                } else {
                    sscanf(buffer, "UNDO %s", filepath);
                }
                
                // For CREATE, send to any available SS
                int ss_id = 0;
                if (op == OP_CREATE) {
                    if (ns.ss_count == 0) {
                        send(client_fd, "ERROR: No storage servers available\n", 36, 0);
                        break;
                    }
                    ss_id = 0; // Simple: use first SS
                } else {
                    // For DELETE/UNDO, find the file
                    int file_idx = find_file(filepath);
                    if (file_idx < 0) {
                        send(client_fd, "ERROR: File not found\n", 22, 0);
                        break;
                    }
                    
                    // Check permissions for DELETE
                    if (op == OP_DELETE && strcmp(ns.files[file_idx].owner, username) != 0) {
                        send(client_fd, "ERROR: Only owner can delete\n", 29, 0);
                        break;
                    }
                    
                    ss_id = ns.files[file_idx].ss_id;
                }
                
                // Forward request to SS (simplified - in real implementation would connect to SS)
                snprintf(response, sizeof(response), "FORWARD_TO_SS %s %d\n",
                        ns.ss_list[ss_id].ip, ns.ss_list[ss_id].nm_port);
                send(client_fd, response, strlen(response), 0);
                log_message("Response to %s: Forwarding %s to SS %d", username, buffer, ss_id);
                
                // If CREATE was successful, add to file index
                if (op == OP_CREATE) {
                    pthread_mutex_lock(&ns.lock);
                    if (ns.file_count < MAX_FILES_PER_SS * MAX_STORAGE_SERVERS) {
                        file_metadata *file = &ns.files[ns.file_count];
                        strncpy(file->filepath, filepath, MAX_FILENAME - 1);
                        strncpy(file->owner, username, MAX_USERNAME - 1);
                        file->ss_id = ss_id;
                        file->size = 0;
                        time(&file->last_access);
                        file->read_access[user_idx] = 1;
                        file->write_access[user_idx] = 1;
                        ns.file_count++;
                    }
                    pthread_mutex_unlock(&ns.lock);
                }
                break;
            }
            
            default:
                send(client_fd, "ERROR: Unknown command\n", 23, 0);
                log_message("Error: Unknown command from %s: %s", username, buffer);
                break;
        }
    }
}
