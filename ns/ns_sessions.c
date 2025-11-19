#include "../include/common.h"
#include "../include/serialize.h"
#include "ns_data.h"
#include <sys/wait.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>

// --- Helper: Connect to a server (used for fetching files from SS) ---
int connect_to_server(const char* ip, int port) {
    int sock;
    struct sockaddr_in addr;
    
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("NS: Socket creation error");
        return -1;
    }
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        perror("NS: Invalid address");
        close(sock);
        return -1;
    }
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("NS: Connection Failed");
        close(sock);
        return -1;
    }
    return sock;
}

// --- Forward Declarations for Request Handlers ---
const char* op_to_string(ClientOpCode op); // Helper prototype
void handle_op_list(int sock, ClientRequest* req);
void handle_op_view(int sock, ClientRequest* req);
void handle_op_create(int sock, ClientRequest* req); 
void handle_op_delete(int sock, ClientRequest* req);
void handle_op_undo(int sock, ClientRequest* req);
void handle_op_read(int sock, ClientRequest* req); 
void handle_op_write(int sock, ClientRequest* req);
void handle_op_info(int sock, ClientRequest* req);
void handle_op_addaccess(int sock, ClientRequest* req);
void handle_op_remaccess(int sock, ClientRequest* req);
void handle_op_reqaccess(int sock, ClientRequest* req);
void handle_op_reqlist(int sock, ClientRequest* req);
void handle_op_approve(int sock, ClientRequest* req);
void handle_op_reject(int sock, ClientRequest* req);
void handle_op_checkpoint(int sock, ClientRequest* req);
void handle_op_viewcheckpoint(int sock, ClientRequest* req);
void handle_op_revert(int sock, ClientRequest* req);
void handle_op_listcheckpoints(int sock, ClientRequest* req);
void handle_op_exec(int sock, ClientRequest* req);
void handle_op_createfolder(int sock, ClientRequest* req);
void handle_op_move(int sock, ClientRequest* req);
void handle_op_viewfolder(int sock, ClientRequest* req);
int ns_is_client_in_list(ClientList* list, const char* username); 

// --- NEW: Helper function for logging ---
const char* op_to_string(ClientOpCode op) {
    switch (op) {
        case OP_VIEW: return "VIEW";
        case OP_LIST: return "LIST";
        case OP_ADDACCESS: return "ADDACCESS";
        case OP_REMACCESS: return "REMACCESS";
        case OP_REQACCESS: return "REQACCESS";
        case OP_REQLIST: return "REQLIST";
        case OP_APPROVE: return "APPROVE";
        case OP_REJECT: return "REJECT";
        case OP_INFO: return "INFO";
        case OP_CREATE: return "CREATE";
        case OP_DELETE: return "DELETE";
        case OP_CREATEFOLDER: return "CREATEFOLDER";
        case OP_MOVE: return "MOVE";
        case OP_VIEWFOLDER: return "VIEWFOLDER";
        case OP_CHECKPOINT: return "CHECKPOINT";
        case OP_VIEWCHECKPOINT: return "VIEWCHECKPOINT";
        case OP_REVERT: return "REVERT";
        case OP_LISTCHECKPOINTS: return "LISTCHECKPOINTS";
        case OP_UNDO: return "UNDO";
        case OP_EXEC: return "EXEC";
        case OP_READ: return "READ";
        case OP_WRITE: return "WRITE";
        case OP_STREAM: return "STREAM";
        case OP_EXIT: return "EXIT";
        case OP_WRITER_DONE: return "WRITER_DONE";
        default: return "UNKNOWN";
    }
}
// --- END NEW ---

// --- Client Session Logic ---
void* handle_client_session(void* arg) {
    ClientInfo* client = (ClientInfo*)arg;
    int sock = client->sock;
    char* username = client->username;
    
    ServerResponse res;
    memset(&res, 0, sizeof(ServerResponse));
    
    // 1. Check if username is already active
    pthread_mutex_lock(&ns.current_clients->lock);
    int is_active = ns_is_client_in_list(ns.current_clients, username); 
    pthread_mutex_unlock(&ns.current_clients->lock);

    if (is_active) {
        res.status = ERR_USERNAME_TAKEN;
        sprintf(res.message, "Error: Username '%s' is already in use.", username);
        SEND_SERVER_RESPONSE(sock, &res);
        close(sock);
        free(client);
        return NULL;
    }

    // 2. Add to client lists
    pthread_mutex_lock(&ns.current_clients->lock);
    ns_add_client(ns.current_clients, username, sock); 
    pthread_mutex_unlock(&ns.current_clients->lock);
    
    pthread_mutex_lock(&ns.all_clients->lock);
    if (!ns_is_client_in_list(ns.all_clients, username)) {
        ns_add_client(ns.all_clients, username, sock);
        pthread_mutex_unlock(&ns.all_clients->lock);
        // Save to persistent storage
        ns_save_all_users();
    } else {
        pthread_mutex_unlock(&ns.all_clients->lock);
    }
    
    printf("Client '%s' connected.\n", username);
    res.status = ERR_OK;
    sprintf(res.message, "Welcome, %s! You are connected to the Name Server.", username);
    SEND_SERVER_RESPONSE(sock, &res);

    // 3. Client request loop
    ClientRequest req;
    while (RECV_CLIENT_REQUEST(sock, &req) > 0) {
        
        // --- Log the operation ---
        printf("NS: Client '%s' requested operation: %s (path: '%s')\n", 
               username, op_to_string(req.op), req.path);

        switch (req.op) {
            case OP_LIST:
                handle_op_list(sock, &req);
                break;
            case OP_VIEW:
                handle_op_view(sock, &req);
                break;
            case OP_CREATE:
                handle_op_create(sock, &req);
                break;
            case OP_DELETE:
                handle_op_delete(sock, &req);
                break;
            case OP_UNDO:
                handle_op_undo(sock, &req);
                break;
            case OP_CHECKPOINT:
                handle_op_checkpoint(sock, &req);
                break;
            case OP_VIEWCHECKPOINT:
                handle_op_viewcheckpoint(sock, &req);
                break;
            case OP_REVERT:
                handle_op_revert(sock, &req);
                break;
            case OP_LISTCHECKPOINTS:
                handle_op_listcheckpoints(sock, &req);
                break;
            case OP_READ:
                handle_op_read(sock, &req);
                break;
            case OP_WRITE:
                handle_op_write(sock, &req);
                break;
            case OP_STREAM:
                handle_op_read(sock, &req); // STREAM uses same logic as READ
                break;
            case OP_EXEC:
                handle_op_exec(sock, &req);
                break;
            case OP_CREATEFOLDER:
                handle_op_createfolder(sock, &req);
                break;
            case OP_MOVE:
                handle_op_move(sock, &req);
                break;
            case OP_VIEWFOLDER:
                handle_op_viewfolder(sock, &req);
                break;
            case OP_INFO:
                handle_op_info(sock, &req);
                break;
            case OP_ADDACCESS:
                handle_op_addaccess(sock, &req);
                break;
            case OP_REMACCESS:
                handle_op_remaccess(sock, &req);
                break;
            case OP_REQACCESS:
                handle_op_reqaccess(sock, &req);
                break;
            case OP_REQLIST:
                handle_op_reqlist(sock, &req);
                break;
            case OP_APPROVE:
                handle_op_approve(sock, &req);
                break;
            case OP_REJECT:
                handle_op_reject(sock, &req);
                break;
            case OP_WRITER_DONE: {
                // Decrement active_writers counter for the SS that served this write
                FileInfo* file = ns_file_get_cached(req.path);
                if (file != NULL) {
                    // Find which SS is serving writes (has active_writers > 0)
                    for (int i = 0; i < file->ss_count; i++) {
                        pthread_mutex_lock(&file->ss_list[i]->writers_lock);
                        if (file->ss_list[i]->active_writers > 0) {
                            file->ss_list[i]->active_writers--;
                            printf("NS: Decremented active_writers for SS '%s' (now: %d)\n",
                                   file->ss_list[i]->name, file->ss_list[i]->active_writers);
                            pthread_mutex_unlock(&file->ss_list[i]->writers_lock);
                            break;
                        }
                        pthread_mutex_unlock(&file->ss_list[i]->writers_lock);
                    }
                    free_file_info_copy(file);
                }
                break;
            }
            case OP_EXIT:
                goto client_exit;
            default:
                // Send ERR_UNKNOWN_COMMAND
                break;
        }
    }

client_exit:
    // Client disconnected
    printf("Client '%s' disconnected.\n", username);
    pthread_mutex_lock(&ns.current_clients->lock);
    ns_remove_client(ns.current_clients, username); 
    pthread_mutex_unlock(&ns.current_clients->lock);
    
    close(sock);
    free(client);
    return NULL;
}


// --- Storage Server Command Socket Session Logic ---
void* handle_ss_cmd_session(void* arg) {
    SSInfo* ss = (SSInfo*)arg;
    int sock = ss->cmd_sock;
    
    // 1. Receive SS_Info_Packet (already peeked in handle_connection)
    SS_Info_Packet info_pkt;
    char info_buffer[SERIALIZED_SS_INFO_PACKET_SIZE];
    if (recv_full(sock, info_buffer, SERIALIZED_SS_INFO_PACKET_SIZE) <= 0) {
        printf("SS disconnected before sending info.\n");
        close(sock);
        return NULL;
    }
    deserialize_ss_info_packet(info_buffer, &info_pkt);
    
    // Store SS name and backup port
    strcpy(ss->name, info_pkt.name);
    ss->backup_port = info_pkt.backup_port;
    
    printf("Storage Server '%s' connected from %s:%d (empty: %d, update_port: %d, backup_port: %d)\n", 
           ss->name, ss->ip, ss->client_port, info_pkt.is_empty, info_pkt.update_port, ss->backup_port);

    // PRIORITY 1: Check if this is a reconnecting SS with an existing partner
    SSInfo* existing_partner = NULL;
    pthread_mutex_lock(&ns.ss_list_lock);
    SSInfo* current = ns.ss_list_head;
    while (current != NULL) {
        if (current != ss && current->is_active && 
            strcmp(current->partner_name, ss->name) == 0) {
            existing_partner = current;
            break;
        }
        current = current->next;
    }
    pthread_mutex_unlock(&ns.ss_list_lock);
    
    // First, handle reconnection partnership restoration (metadata only - no commands yet)
    if (existing_partner != NULL) {
        printf("NS: SS '%s' reconnecting - restoring partnership with '%s'\n", 
               ss->name, existing_partner->name);
        strcpy(ss->partner_name, existing_partner->name);
        ss->backed_up = 1;
        existing_partner->backed_up = 1;
    }

    // 2. Receive File List (if not empty)
    // IMPORTANT: Must receive file index BEFORE sending any commands on cmd_sock,
    // because SS main thread is blocked sending file index and can't process commands yet
    if (!info_pkt.is_empty) {
        if (existing_partner == NULL) {
            // Normal registration - receive and process file index
            printf("NS: Receiving file index from SS %s...\n", ss->name);
            SS_File_Sync_Packet sync_pkt;
            char sync_buffer[SERIALIZED_SS_FILE_SYNC_PACKET_SIZE];
            while (1) {
                memset(&sync_pkt, 0, sizeof(sync_pkt));
                memset(sync_buffer, 0, sizeof(sync_buffer));
                
                // Use recv_full to ensure we get the complete packet
                int recv_bytes = recv_full(sock, sync_buffer, SERIALIZED_SS_FILE_SYNC_PACKET_SIZE);
                if (recv_bytes <= 0) break;
                
                deserialize_ss_file_sync_packet(sync_buffer, &sync_pkt);
                
                // Check for end marker
                if (strcmp(sync_pkt.path, SYNC_END_MARKER) == 0) {
                    break; // Sync complete
                }
                
                // Process the received file info
                ns_parse_and_index_file(sync_pkt.path, sync_pkt.info_content, ss);
            }
            printf("NS: File index sync complete for SS %s.\n", ss->name);
        } else {
            // Reconnection - discard file index since it's stale, survivor will sync to it
            printf("NS: Discarding stale file index from reconnecting SS '%s' (will receive from survivor)...\n", ss->name);
            SS_File_Sync_Packet sync_pkt;
            char sync_buffer[SERIALIZED_SS_FILE_SYNC_PACKET_SIZE];
            while (1) {
                memset(&sync_pkt, 0, sizeof(sync_pkt));
                memset(sync_buffer, 0, sizeof(sync_buffer));
                
                int recv_bytes = recv_full(sock, sync_buffer, SERIALIZED_SS_FILE_SYNC_PACKET_SIZE);
                if (recv_bytes <= 0) break;
                
                deserialize_ss_file_sync_packet(sync_buffer, &sync_pkt);
                
                // Check for end marker
                if (strcmp(sync_pkt.path, SYNC_END_MARKER) == 0) {
                    printf("NS: Discarded stale file index from reconnecting SS '%s'\n", ss->name);
                    break;
                }
            }
        }
    }
    
    // NOW send pair commands after file index is received (so SS can process commands)
    if (existing_partner != NULL) {
        // Re-join sync: survivor → reconnecting
        printf("NS: Sending pair commands for reconnection sync...\n");
        send_pair_command(ss, existing_partner->name, existing_partner->ip, existing_partner->backup_port, 0);  // Reconnecting receives
        send_pair_command(existing_partner, ss->name, ss->ip, ss->backup_port, 1);  // Survivor sends
        
        printf("NS: Reconnecting SS '%s' will receive sync from survivor '%s'\n",
               ss->name, existing_partner->name);
        
        // Add reconnecting SS to all files that existing_partner has
        ns_add_partner_to_files(existing_partner, ss);
        printf("NS: Added reconnecting SS '%s' to all files from survivor '%s'\n",
               ss->name, existing_partner->name);
    }
    
    // Mark SS as active BEFORE pairing so other SSes can find it in find_unbacked_ss()
    ss->is_active = 1;
    
    // PRIORITY 2: If not already paired (reconnection), handle backup assignment
    if (!ss->backed_up && ss->partner_name[0] == '\0') {
        if (info_pkt.is_empty) {
            // Empty SS - try to pair with unbacked regular SS (including survivors)
            SSInfo* regular_ss = find_unbacked_ss(ss);
            if (regular_ss != NULL) {
                // Pair them
                pair_storage_servers(ss, regular_ss);
                
                // Initial sync: regular → empty
                // The regular_ss (has files) should send full sync to empty ss
                send_pair_command(ss, regular_ss->name, regular_ss->ip, regular_ss->backup_port, 0);  // Empty receives
                send_pair_command(regular_ss, ss->name, ss->ip, ss->backup_port, 1);  // Regular sends
                
                // Add empty SS to all files that regular_ss has
                ns_add_partner_to_files(regular_ss, ss);
            } else {
                // No unbacked regular SS found - empty SS becomes regular immediately
                printf("NS: Empty SS '%s' has no SS to back up, becoming regular (no backup)\n", ss->name);
                // It stays unbacked and waits for a future empty SS to back it up
            }
        } else {
            // Regular SS - just wait for an empty SS to arrive in the future
            printf("NS: Regular SS '%s' registered, waiting for empty SS to pair with\n", ss->name);
        }
    }
    
    // 4. Command socket stays alive to detect disconnection
    printf("NS: SS %s command session initialized.\n", ss->name);
    
    // Wait while active - update_session will mark inactive on disconnect
    while (ss->is_active) {
        sleep(1);
    }
    
    // Close command socket and free SS structure
    close(sock);
    pthread_mutex_destroy(&ss->cmd_lock);
    free(ss);
    
    return NULL;
}

// --- Storage Server Update Socket Session Logic ---
void* handle_ss_update_session(void* arg) {
    SSInfo* ss = (SSInfo*)arg;
    int sock = ss->update_sock;
    
    // 1. Consume the SS_Info_Packet (already processed by handle_connection)
    SS_Info_Packet info_pkt;
    char info_buffer[SERIALIZED_SS_INFO_PACKET_SIZE];
    if (recv_full(sock, info_buffer, SERIALIZED_SS_INFO_PACKET_SIZE) <= 0) {
        printf("NS: Update socket disconnected before info packet.\n");
        return NULL;
    }
    deserialize_ss_info_packet(info_buffer, &info_pkt);
    
    printf("NS: Update socket initialized for SS '%s'\n", ss->name);
    
    // 2. Main message loop - handle ASYNCHRONOUS messages from SS
    while (1) {
        SSOpCode opcode;
        
        int bytes = RECV_OPCODE(sock, &opcode);
        
        if (bytes <= 0) {
            break; // SS disconnected
        }
        
        // Update last heartbeat time on any message
        ss->last_heartbeat = time(NULL);
        
        if (opcode == SS_NS_UPDATE_INFO) {
            // Receive rest of message (no lock needed - dedicated socket)
            char path[MAX_PATH_LEN];
            recv_full(sock, path, MAX_PATH_LEN);
            
            char info_content[MAX_INFO_LEN];
            recv_full(sock, info_content, MAX_INFO_LEN);
            
            printf("NS: Received info update from SS '%s' for file '%s'\n", ss->name, path);
            printf("NS: Info content received:\n%s\n", info_content);
            
            // Update the NS file index
            pthread_mutex_lock(&ns.file_index->lock);
            FileInfo* file = ns_file_index_get(path);
            if (file != NULL) {
                printf("NS: BEFORE update - file->last_accessed=%ld, file->modified=%ld\n", 
                       file->last_accessed, file->modified);
                ns_update_file_metadata(file, info_content);
                printf("NS: AFTER update - file->last_accessed=%ld, file->modified=%ld\n", 
                       file->last_accessed, file->modified);
            } else {
                printf("NS: Warning - received info update for unknown file '%s'\n", path);
            }
            pthread_mutex_unlock(&ns.file_index->lock);
            
            // CRITICAL: Invalidate cache so clients get fresh data
            lru_cache_invalidate(ns.lru_cache, path);
            printf("NS: LRU Cache INVALIDATED '%s'\n", path);
            
        } else if (opcode == SS_NS_HEARTBEAT) {
            // Heartbeat ACK from SS
            printf("NS: Heartbeat ACK from SS '%s'\n", ss->name);
        } else if (opcode == SS_NS_PARTNER_DIED) {
            // Partner disconnected notification
            printf("NS: SS '%s' reported partner disconnect\n", ss->name);
            // Keep partner_name for reconnection detection, just clear backed_up
            ss->backed_up = 0;
        } else {
            printf("NS: Unknown opcode %d from SS '%s'\n", opcode, ss->name);
        }
    }
    
    
    printf("NS: Storage Server '%s' disconnected.\n", ss->name);
    
    // Notify partner if exists
    if (ss->partner_name[0] != '\0') {
        pthread_mutex_lock(&ns.ss_list_lock);
        SSInfo* partner = ns.ss_list_head;
        while (partner != NULL) {
            if (strcmp(partner->name, ss->partner_name) == 0 && partner->is_active) {
                // Notify partner of disconnect
                pthread_mutex_lock(&partner->cmd_lock);
                NSOpCode opcode = NS_SS_PARTNER_DISCONNECTED;
                SEND_OPCODE(partner->cmd_sock, opcode);
                pthread_mutex_unlock(&partner->cmd_lock);
                
                printf("NS: Notified SS '%s' that partner '%s' disconnected\n", 
                       partner->name, ss->name);
                
                // Keep partner_name intact for reconnection detection
                // Only clear backed_up flag
                partner->backed_up = 0;
                break;
            }
            partner = partner->next;
        }
        pthread_mutex_unlock(&ns.ss_list_lock);
    }
    
    // Clean up all files from this SS
    ns_cleanup_ss_from_index(ss);
    
    // Remove this SS from the global list
    pthread_mutex_lock(&ns.ss_list_lock);
    SSInfo* current = ns.ss_list_head;
    SSInfo* prev = NULL;
    
    while (current != NULL) {
        if (current == ss) {
            if (prev == NULL) {
                ns.ss_list_head = current->next;
            } else {
                prev->next = current->next;
            }
            break;
        }
        prev = current;
        current = current->next;
    }
    pthread_mutex_unlock(&ns.ss_list_lock);
    
    close(sock);
    
    // Signal cmd session to exit
    ss->is_active = 0;
    
    return NULL;
}


// --- Request Handlers ---

void handle_op_list(int sock, ClientRequest* req) {
    ServerResponse res;
    memset(&res, 0, sizeof(ServerResponse));
    res.status = ERR_OK;
    
    char* current_users = (char*)calloc(1, MAX_BUFFER_LEN);
    char* all_users = (char*)calloc(1, MAX_BUFFER_LEN);

    // Build current users list
    pthread_mutex_lock(&ns.current_clients->lock);
    ClientInfo* current = ns.current_clients->head;
    strcat(current_users, "Current Users:\n");
    while (current != NULL) {
        strcat(current_users, "- ");
        strcat(current_users, current->username);
        strcat(current_users, "\n");
        current = current->next;
    }
    pthread_mutex_unlock(&ns.current_clients->lock);

    // Build all users list
    pthread_mutex_lock(&ns.all_clients->lock);
    ClientInfo* all = ns.all_clients->head; 
    strcat(all_users, "All Users:\n");
    while (all != NULL) {
        strcat(all_users, "- ");
        strcat(all_users, all->username);
        strcat(all_users, "\n");
        all = all->next;
    }
    pthread_mutex_unlock(&ns.all_clients->lock);
    
    sprintf(res.message, "%s\n%s", current_users, all_users);
    SEND_SERVER_RESPONSE(sock, &res);
    
    free(current_users);
    free(all_users);
}

void handle_op_view(int sock, ClientRequest* req) {
    ServerResponse res;
    memset(&res, 0, sizeof(ServerResponse));
    res.status = ERR_OK;
    
    // Extract flags: bit 0 = show_all, bit 1 = show_long
    int show_all = req->flags & 1;
    int show_long = req->flags & 2;
    
    char message[MAX_BUFFER_LEN];
    memset(message, 0, sizeof(message));
    
    pthread_mutex_lock(&ns.file_index->lock);
    
    if (show_long) {
        // Long format with details
        sprintf(message, "---------------------------------------------------------\n");
        sprintf(message + strlen(message), "| %-20s | %-5s | %-5s | %-16s | %-10s |\n", 
                "Filename", "Words", "Chars", "Last Access", "Owner");
        sprintf(message + strlen(message), "|----------------------|-------|-------|------------------|------------|\n");
        
        // Iterate through all buckets in the file index hash table
        for (int i = 0; i < FILE_INDEX_SIZE; i++) {
            FileInfo* file = ns.file_index->table[i];
            while (file != NULL) {
                // Check if file has at least one active SS
                int has_active_ss = 0;
                for (int j = 0; j < file->ss_count; j++) {
                    if (file->ss_list[j]->is_active) {
                        has_active_ss = 1;
                        break;
                    }
                }
                
                // Check if user has access or if showing all files
                int has_access = (strcmp(file->owner, req->username) == 0) || 
                                 ns_check_permission(file, req->username, 'R');
                
                if (has_active_ss && (show_all || has_access)) {
                    // Format timestamp
                    char time_str[20] = "N/A";
                    if (file->last_accessed > 0) {
                        struct tm* tm_info = localtime(&file->last_accessed);
                        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", tm_info);
                    }
                    
                    sprintf(message + strlen(message), "| %-20s | %5d | %5d | %-16s | %-10s |\n",
                            file->path, file->word_count, file->char_count, time_str, file->owner);
                }
                
                file = file->next;
            }
        }
        
        sprintf(message + strlen(message), "---------------------------------------------------------\n");
    } else {
        // Simple format - just filenames
        int file_count = 0;
        
        // Iterate through all buckets
        for (int i = 0; i < FILE_INDEX_SIZE; i++) {
            FileInfo* file = ns.file_index->table[i];
            while (file != NULL) {
                // Check if file has at least one active SS
                int has_active_ss = 0;
                for (int j = 0; j < file->ss_count; j++) {
                    if (file->ss_list[j]->is_active) {
                        has_active_ss = 1;
                        break;
                    }
                }
                
                // Check if user has access or if showing all files
                int has_access = (strcmp(file->owner, req->username) == 0) || 
                                 ns_check_permission(file, req->username, 'R');
                
                if (has_active_ss && (show_all || has_access)) {
                    sprintf(message + strlen(message), "--> %s\n", file->path);
                    file_count++;
                }
                
                file = file->next;
            }
        }
        
        if (file_count == 0) {
            sprintf(message, "No files available.\n");
        }
    }
    
    pthread_mutex_unlock(&ns.file_index->lock);
    
    strncpy(res.message, message, MAX_BUFFER_LEN - 1);
    SEND_SERVER_RESPONSE(sock, &res);
}

void handle_op_create(int sock, ClientRequest* req) {
    ServerResponse res;
    memset(&res, 0, sizeof(ServerResponse));
    
    // 1. Check if file already exists in index
    pthread_mutex_lock(&ns.file_index->lock);
    if (ns_file_index_get(req->path) != NULL) {
        pthread_mutex_unlock(&ns.file_index->lock);
        res.status = ERR_FILE_EXISTS;
        sprintf(res.message, "Error: File '%s' already exists.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    pthread_mutex_unlock(&ns.file_index->lock);

    // 2. Find an active SS to create the file on
    pthread_mutex_lock(&ns.ss_list_lock);
    SSInfo* target_ss = ns.ss_list_head;
    while (target_ss != NULL && !target_ss->is_active) {
        target_ss = target_ss->next;
    }
    
    if (target_ss == NULL) {
        pthread_mutex_unlock(&ns.ss_list_lock);
        res.status = ERR_SS_NOT_FOUND;
        sprintf(res.message, "Error: No available Storage Servers to create file.");
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // 3. Send command to the SS with socket lock
    NSOpCode op = NS_SS_CREATE;
    pthread_mutex_lock(&target_ss->cmd_lock);
    SEND_OPCODE(target_ss->cmd_sock, op);
    send_full(target_ss->cmd_sock, req->path, MAX_PATH_LEN);
    send_full(target_ss->cmd_sock, req->username, MAX_USERNAME_LEN);
    
    // Receive response from SS
    ServerResponse ss_res;
    RECV_SERVER_RESPONSE(target_ss->cmd_sock, &ss_res);
    pthread_mutex_unlock(&target_ss->cmd_lock);
    
    pthread_mutex_unlock(&ns.ss_list_lock);
    
    // 4. If SS responded OK, add to file index and request metadata
    if (ss_res.status == ERR_OK) {
        pthread_mutex_lock(&ns.file_index->lock);
        ns_file_index_add(req->path, req->username, target_ss);
        pthread_mutex_unlock(&ns.file_index->lock);
        
        printf("NS: File '%s' created by '%s' on SS %s\n", req->path, req->username, target_ss->ip);
        
        // Request metadata from SS to populate timestamps
        // Must be done AFTER adding file to index to avoid race
        pthread_mutex_lock(&ns.ss_list_lock);
        if (target_ss->is_active) {
            NSOpCode info_op = NS_SS_GET_INFO;
            char info_content[MAX_INFO_LEN];
            
            pthread_mutex_lock(&target_ss->cmd_lock);
            SEND_OPCODE(target_ss->cmd_sock, info_op);
            send_full(target_ss->cmd_sock, req->path, MAX_PATH_LEN);
            
            ServerResponse info_res;
            RECV_SERVER_RESPONSE(target_ss->cmd_sock, &info_res);
            if (info_res.status == ERR_OK) {
                recv_full(target_ss->cmd_sock, info_content, MAX_INFO_LEN);
                
                // Update file metadata with info from SS
                pthread_mutex_lock(&ns.file_index->lock);
                FileInfo* file = ns_file_index_get(req->path);
                if (file != NULL) {
                    ns_update_file_metadata(file, info_content);
                }
                pthread_mutex_unlock(&ns.file_index->lock);
                
                // Invalidate any cached entry (shouldn't exist yet, but be safe)
                lru_cache_invalidate(ns.lru_cache, req->path);
            }
            pthread_mutex_unlock(&target_ss->cmd_lock);
        }
        pthread_mutex_unlock(&ns.ss_list_lock);
        
        res.status = ERR_OK;
        sprintf(res.message, "File '%s' created successfully.", req->path);
    } else {
        res = ss_res;
    }
    
    // 5. Send "success" to client
    SEND_SERVER_RESPONSE(sock, &res);
}

void handle_op_delete(int sock, ClientRequest* req) {
    ServerResponse res;
    memset(&res, 0, sizeof(ServerResponse));
    
    // 1. Find the file (using cache)
    FileInfo* file = ns_file_get_cached(req->path);
    
    if (file == NULL) {
        res.status = ERR_FILE_NOT_FOUND;
        sprintf(res.message, "Error: File '%s' not found.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // 2. Check if the user is the owner (only owner can delete)
    if (strcmp(file->owner, req->username) != 0) {
        free_file_info_copy(file);
        res.status = ERR_NOT_OWNER;
        sprintf(res.message, "Error: Only the owner can delete this file.");
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // 3. Copy SS list to avoid holding locks during network I/O
    int ss_count = file->ss_count;
    SSInfo* ss_list[ss_count];
    for (int i = 0; i < ss_count; i++) {
        ss_list[i] = file->ss_list[i];
    }
    
    // Done with cached file info
    free_file_info_copy(file);
    
    // 4. Send delete command to all SSs that have this file
    printf("NS: Deleting file '%s' from %d storage server(s)\n", req->path, ss_count);
    
    int deletion_blocked = 0;
    char blocked_reason[MAX_BUFFER_LEN] = {0};
    
    for (int i = 0; i < ss_count; i++) {
        if (ss_list[i]->is_active) {
            NSOpCode op = NS_SS_DELETE;
            
            pthread_mutex_lock(&ss_list[i]->cmd_lock);
            SEND_OPCODE(ss_list[i]->cmd_sock, op);
            send_full(ss_list[i]->cmd_sock, req->path, MAX_PATH_LEN);
            
            // Wait for acknowledgment
            ServerResponse ss_res;
            RECV_SERVER_RESPONSE(ss_list[i]->cmd_sock, &ss_res);
            pthread_mutex_unlock(&ss_list[i]->cmd_lock);
            
            if (ss_res.status == ERR_SENTENCE_LOCKED) {
                // File is currently being written to - cannot delete
                deletion_blocked = 1;
                snprintf(blocked_reason, sizeof(blocked_reason), 
                         "File is currently being accessed (active write session). Please try again later.");
                printf("NS: SS %s reports file '%s' is locked (active session)\n", 
                       ss_list[i]->ip, req->path);
                break; // Stop trying to delete from other SSs
            } else if (ss_res.status != ERR_OK) {
                printf("NS: SS %s failed to delete file '%s': %s\n", 
                       ss_list[i]->ip, req->path, ss_res.message);
            } else {
                printf("NS: SS %s deleted file '%s': OK\n", 
                       ss_list[i]->ip, req->path);
            }
        }
    }
    
    // If deletion was blocked, notify the client and abort
    if (deletion_blocked) {
        res.status = ERR_SENTENCE_LOCKED;
        strncpy(res.message, blocked_reason, sizeof(res.message) - 1);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // 5. Invalidate cache entry before removing from index
    lru_cache_invalidate(ns.lru_cache, req->path);
    
    // 6. Re-acquire lock and remove file from the index
    pthread_mutex_lock(&ns.file_index->lock);
    // We need to remove it from the hash table
    unsigned long hash = 5381;  // Must match hash_path() in ns_index.c
    const char* p = req->path;
    int c;
    while ((c = *p++)) {
        hash = ((hash << 5) + hash) + c;
    }
    hash = hash % FILE_INDEX_SIZE;
    
    printf("NS: Removing file from hash bucket %lu\n", hash);
    
    FileInfo* current = ns.file_index->table[hash];
    FileInfo* prev = NULL;
    int found = 0;
    
    while (current != NULL) {
        printf("NS: Checking file '%s' in hash bucket\n", current->path);
        if (strcmp(current->path, req->path) == 0) {
            // Found it - remove from linked list
            if (prev == NULL) {
                ns.file_index->table[hash] = current->next;
            } else {
                prev->next = current->next;
            }
            
            // Free the FileInfo and its data
            for (int i = 0; i < current->read_count; i++) {
                free(current->read_access[i]);
            }
            free(current->read_access);
            
            for (int i = 0; i < current->write_count; i++) {
                free(current->write_access[i]);
            }
            free(current->write_access);
            
            for (int i = 0; i < current->exec_count; i++) {
                free(current->exec_access[i]);
            }
            free(current->exec_access);
            
            free(current->ss_list);
            free(current);
            
            printf("NS: File '%s' removed from index (hash bucket %lu)\n", req->path, hash);
            found = 1;
            break;
        }
        prev = current;
        current = current->next;
    }
    
    if (!found) {
        printf("NS: WARNING: File '%s' not found in hash table for removal!\n", req->path);
    }
    
    pthread_mutex_unlock(&ns.file_index->lock);
    
    // 5. Send success response to client
    res.status = ERR_OK;
    sprintf(res.message, "File '%s' deleted successfully!", req->path);
    SEND_SERVER_RESPONSE(sock, &res);
}

void handle_op_undo(int sock, ClientRequest* req) {
    ServerResponse res;
    memset(&res, 0, sizeof(ServerResponse));
    
    // 1. Find the file (using cache)
    FileInfo* file = ns_file_get_cached(req->path);
    
    if (file == NULL) {
        res.status = ERR_FILE_NOT_FOUND;
        sprintf(res.message, "Error: File '%s' not found.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // 2. Check write permissions (only users with write access can undo)
    if (!ns_check_permission(file, req->username, 'W')) {
        free_file_info_copy(file);
        res.status = ERR_ACCESS_DENIED;
        sprintf(res.message, "Error: You don't have write access to '%s'.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // 3. Find first active SS for this file
    SSInfo* target_ss = NULL;
    for (int i = 0; i < file->ss_count; i++) {
        if (file->ss_list[i]->is_active) {
            target_ss = file->ss_list[i];
            break;
        }
    }
    
    // Done with cached file info
    free_file_info_copy(file);
    
    if (target_ss == NULL) {
        res.status = ERR_SS_NOT_FOUND;
        sprintf(res.message, "Error: No active servers found for file '%s'.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // 4. Send undo command to an active SS (it will sync to partner)
    printf("NS: Sending UNDO command for file '%s' to SS %s\n", req->path, target_ss->name);
    
    ErrorCode result_status = ERR_OK;
    
    NSOpCode op = NS_SS_UNDO;
    
    pthread_mutex_lock(&target_ss->cmd_lock);
    SEND_OPCODE(target_ss->cmd_sock, op);
    send_full(target_ss->cmd_sock, req->path, MAX_PATH_LEN);
    send_full(target_ss->cmd_sock, req->username, MAX_USERNAME_LEN);
    
    // Wait for acknowledgment
    ServerResponse ss_res;
    RECV_SERVER_RESPONSE(target_ss->cmd_sock, &ss_res);
    pthread_mutex_unlock(&target_ss->cmd_lock);
    
    result_status = ss_res.status;
    if (ss_res.status == ERR_OK) {
        printf("NS: SS %s undo successful for '%s'\n", target_ss->name, req->path);
    } else {
        printf("NS: SS %s undo failed for '%s': %s\n", 
               target_ss->name, req->path, ss_res.message);
    }
    
    // 5. Invalidate cache after undo completes (file content changed)
    if (result_status == ERR_OK) {
        lru_cache_invalidate(ns.lru_cache, req->path);
    }
    
    // 6. Send response to client
    res.status = result_status;
    if (result_status == ERR_OK) {
        sprintf(res.message, "Undo successful for '%s'.", req->path);
    } else {
        sprintf(res.message, "Error: Undo failed for '%s'.", req->path);
    }
    SEND_SERVER_RESPONSE(sock, &res);
}

void handle_op_checkpoint(int sock, ClientRequest* req) {
    ServerResponse res;
    memset(&res, 0, sizeof(ServerResponse));
    
    // Validate checkpoint tag
    if (strlen(req->arg1) == 0) {
        res.status = ERR_INVALID_PATH;
        sprintf(res.message, "Error: Checkpoint tag required.");
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // 1. Find the file (using cache)
    FileInfo* file = ns_file_get_cached(req->path);
    
    if (file == NULL) {
        res.status = ERR_FILE_NOT_FOUND;
        sprintf(res.message, "Error: File '%s' not found.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // 2. Check if user is the owner (only owner can checkpoint)
    if (strcmp(file->owner, req->username) != 0) {
        free_file_info_copy(file);
        res.status = ERR_ACCESS_DENIED;
        sprintf(res.message, "Error: Only the owner can create checkpoints for '%s'.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // 3. Find first active SS for this file
    SSInfo* target_ss = NULL;
    for (int i = 0; i < file->ss_count; i++) {
        if (file->ss_list[i]->is_active) {
            target_ss = file->ss_list[i];
            break;
        }
    }
    
    // Done with cached file info
    free_file_info_copy(file);
    
    if (target_ss == NULL) {
        res.status = ERR_SS_NOT_FOUND;
        sprintf(res.message, "Error: No active servers found for file '%s'.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // 4. Send checkpoint command to an active SS (it will sync to partner)
    printf("NS: Creating checkpoint '%s' for file '%s' on SS %s\n", 
           req->arg1, req->path, target_ss->name);
    
    ErrorCode result_status = ERR_OK;
    
    NSOpCode op = NS_SS_CHECKPOINT;
    
    pthread_mutex_lock(&target_ss->cmd_lock);
    SEND_OPCODE(target_ss->cmd_sock, op);
    send_full(target_ss->cmd_sock, req->path, MAX_PATH_LEN);
    send_full(target_ss->cmd_sock, req->arg1, MAX_PATH_LEN);
    
    // Wait for acknowledgment
    ServerResponse ss_res;
    memset(&ss_res, 0, sizeof(ServerResponse));
    int recv_bytes = RECV_SERVER_RESPONSE(target_ss->cmd_sock, &ss_res);
    pthread_mutex_unlock(&target_ss->cmd_lock);
    
    if (recv_bytes > 0 && ss_res.status == ERR_OK) {
        printf("NS: SS %s checkpoint successful for '%s'\n", target_ss->name, req->path);
        result_status = ERR_OK;
    } else {
        if (recv_bytes <= 0) {
            result_status = ERR_SS_NOT_FOUND;
            printf("NS: SS %s checkpoint failed (connection lost) for '%s'\n", target_ss->name, req->path);
        } else {
            result_status = ss_res.status;
            printf("NS: SS %s checkpoint failed for '%s': %s\n", 
                   target_ss->name, req->path, ss_res.message);
        }
    }
    
    // 5. Send response to client
    res.status = result_status;
    if (result_status == ERR_OK) {
        sprintf(res.message, "Checkpoint '%s' created for '%s'.", req->arg1, req->path);
    } else {
        sprintf(res.message, "Error: Checkpoint creation failed for '%s'.", req->path);
    }
    SEND_SERVER_RESPONSE(sock, &res);
}

void handle_op_viewcheckpoint(int sock, ClientRequest* req) {
    ServerResponse res;
    memset(&res, 0, sizeof(ServerResponse));
    
    // Validate checkpoint tag
    if (strlen(req->arg1) == 0) {
        res.status = ERR_INVALID_PATH;
        sprintf(res.message, "Error: Checkpoint tag required.");
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // 1. Find the file (using cache)
    FileInfo* file = ns_file_get_cached(req->path);
    
    if (file == NULL) {
        res.status = ERR_FILE_NOT_FOUND;
        sprintf(res.message, "Error: File '%s' not found.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // 2. Check read permissions
    if (!ns_check_permission(file, req->username, 'R')) {
        free_file_info_copy(file);
        res.status = ERR_ACCESS_DENIED;
        sprintf(res.message, "Error: You don't have read access to '%s'.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // 3. Find an active SS (use first available)
    SSInfo* target_ss = NULL;
    for (int i = 0; i < file->ss_count; i++) {
        if (file->ss_list[i]->is_active) {
            target_ss = file->ss_list[i];
            break;
        }
    }
    
    if (target_ss == NULL) {
        free_file_info_copy(file);
        res.status = ERR_SS_NOT_FOUND;
        sprintf(res.message, "Error: No storage servers available for '%s'.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // 4. Send SS connection info to client
    res.status = ERR_OK;
    strcpy(res.ss_ip, target_ss->ip);
    res.ss_port = target_ss->client_port;
    strcpy(res.message, req->arg1); // Pass checkpoint tag to client
    printf("NS: Redirecting VIEWCHECKPOINT for '%s' (tag: %s) to SS %s:%d\n", 
           req->path, req->arg1, target_ss->ip, target_ss->client_port);
    
    free_file_info_copy(file);
    SEND_SERVER_RESPONSE(sock, &res);
}

void handle_op_revert(int sock, ClientRequest* req) {
    ServerResponse res;
    memset(&res, 0, sizeof(ServerResponse));
    
    // Validate checkpoint tag
    if (strlen(req->arg1) == 0) {
        res.status = ERR_INVALID_PATH;
        sprintf(res.message, "Error: Checkpoint tag required.");
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // 1. Find the file (using cache)
    FileInfo* file = ns_file_get_cached(req->path);
    
    if (file == NULL) {
        res.status = ERR_FILE_NOT_FOUND;
        sprintf(res.message, "Error: File '%s' not found.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // 2. Check write permissions (only users with write access can revert)
    if (!ns_check_permission(file, req->username, 'W')) {
        free_file_info_copy(file);
        res.status = ERR_ACCESS_DENIED;
        sprintf(res.message, "Error: You don't have write access to '%s'.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // 3. Find first active SS for this file
    SSInfo* target_ss = NULL;
    for (int i = 0; i < file->ss_count; i++) {
        if (file->ss_list[i]->is_active) {
            target_ss = file->ss_list[i];
            break;
        }
    }
    
    // Done with cached file info
    free_file_info_copy(file);
    
    if (target_ss == NULL) {
        res.status = ERR_SS_NOT_FOUND;
        sprintf(res.message, "Error: No active servers found for file '%s'.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // 4. Send revert command to an active SS (it will sync to partner)
    printf("NS: Reverting file '%s' to checkpoint '%s' on SS %s\n", 
           req->path, req->arg1, target_ss->name);
    
    ErrorCode result_status = ERR_OK;
    char error_msg[MAX_BUFFER_LEN] = "";
    
    NSOpCode op = NS_SS_REVERT;
    
    pthread_mutex_lock(&target_ss->cmd_lock);
    SEND_OPCODE(target_ss->cmd_sock, op);
    send_full(target_ss->cmd_sock, req->path, MAX_PATH_LEN);
    send_full(target_ss->cmd_sock, req->arg1, MAX_PATH_LEN);
    send_full(target_ss->cmd_sock, req->username, MAX_USERNAME_LEN);
    
    // Wait for acknowledgment
    ServerResponse ss_res;
    memset(&ss_res, 0, sizeof(ServerResponse));
    int recv_bytes = RECV_SERVER_RESPONSE(target_ss->cmd_sock, &ss_res);
    pthread_mutex_unlock(&target_ss->cmd_lock);
    
    if (recv_bytes > 0 && ss_res.status == ERR_OK) {
        printf("NS: SS %s revert successful for '%s'\n", target_ss->name, req->path);
        result_status = ERR_OK;
    } else {
        if (recv_bytes <= 0) {
            result_status = ERR_SS_NOT_FOUND;
            strcpy(error_msg, "Communication with storage server failed.");
            printf("NS: SS %s communication failed for revert '%s'\n", 
                   target_ss->name, req->path);
        } else {
            result_status = ss_res.status;
            strcpy(error_msg, ss_res.message);
            printf("NS: SS %s revert failed for '%s': %s\n", 
                   target_ss->name, req->path, ss_res.message);
        }
    }
    
    // 5. Invalidate cache after revert completes (file content changed)
    if (result_status == ERR_OK) {
        lru_cache_invalidate(ns.lru_cache, req->path);
    }
    
    // 6. Send response to client
    res.status = result_status;
    if (result_status == ERR_OK) {
        sprintf(res.message, "Reverted '%s' to checkpoint '%s'.", req->path, req->arg1);
    } else {
        if (strlen(error_msg) > 0) {
            strcpy(res.message, error_msg);
        } else {
            sprintf(res.message, "Error: Revert failed for '%s'.", req->path);
        }
    }
    SEND_SERVER_RESPONSE(sock, &res);
}

void handle_op_listcheckpoints(int sock, ClientRequest* req) {
    ServerResponse res;
    memset(&res, 0, sizeof(ServerResponse));
    
    // 1. Find the file (using cache)
    FileInfo* file = ns_file_get_cached(req->path);
    
    if (file == NULL) {
        res.status = ERR_FILE_NOT_FOUND;
        sprintf(res.message, "Error: File '%s' not found.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // 2. Check read permissions (anyone with read access can list checkpoints)
    if (!ns_check_permission(file, req->username, 'R')) {
        free_file_info_copy(file);
        res.status = ERR_ACCESS_DENIED;
        sprintf(res.message, "Error: You don't have read access to '%s'.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // 3. Find an active SS (use first available)
    SSInfo* target_ss = NULL;
    for (int i = 0; i < file->ss_count; i++) {
        if (file->ss_list[i]->is_active) {
            target_ss = file->ss_list[i];
            break;
        }
    }
    
    if (target_ss == NULL) {
        free_file_info_copy(file);
        res.status = ERR_SS_NOT_FOUND;
        sprintf(res.message, "Error: No storage servers available for '%s'.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // 4. Send SS connection info to client
    res.status = ERR_OK;
    strcpy(res.ss_ip, target_ss->ip);
    res.ss_port = target_ss->client_port;
    printf("NS: Redirecting LISTCHECKPOINTS for '%s' to SS %s:%d\n", 
           req->path, target_ss->ip, target_ss->client_port);
    
    free_file_info_copy(file);
    SEND_SERVER_RESPONSE(sock, &res);
}

// --- EXEC Operation Implementation ---
void handle_op_exec(int sock, ClientRequest* req) {
    ServerResponse res;
    memset(&res, 0, sizeof(ServerResponse));
    
    // 1. Find the file (using cache)
    FileInfo* file = ns_file_get_cached(req->path);
    
    if (file == NULL) {
        res.status = ERR_FILE_NOT_FOUND;
        sprintf(res.message, "Error: File '%s' not found.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // 2. Check execute permissions
    if (!ns_check_permission(file, req->username, 'X')) {
        free_file_info_copy(file);
        res.status = ERR_ACCESS_DENIED;
        sprintf(res.message, "Error: You don't have execute access to '%s'.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // 3. Find an active SS to fetch the file
    SSInfo* target_ss = NULL;
    for (int i = 0; i < file->ss_count; i++) {
        if (file->ss_list[i]->is_active) {
            target_ss = file->ss_list[i];
            break;
        }
    }
    
    if (target_ss == NULL) {
        free_file_info_copy(file);
        res.status = ERR_SS_NOT_FOUND;
        sprintf(res.message, "Error: No storage servers available for '%s'.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    free_file_info_copy(file);
    
    // 4. Fetch file content from SS
    printf("NS: Fetching file '%s' from SS for EXEC\n", req->path);
    
    int ss_sock = connect_to_server(target_ss->ip, target_ss->client_port);
    if (ss_sock < 0) {
        res.status = ERR_SS_NOT_FOUND;
        sprintf(res.message, "Error: Failed to connect to storage server.");
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // Send READ request to SS
    ClientRequest ss_req = *req;
    ss_req.op = OP_READ;
    SEND_CLIENT_REQUEST(ss_sock, &ss_req);
    
    // Receive file content into a buffer
    char* script_content = malloc(MAX_COMMIT_LEN * 10);
    if (script_content == NULL) {
        close(ss_sock);
        res.status = ERR_UNKNOWN_COMMAND;
        sprintf(res.message, "Error: Memory allocation failed.");
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    memset(script_content, 0, MAX_COMMIT_LEN * 10);
    int total_bytes = 0;
    char buffer[MAX_BUFFER_LEN];
    
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        int n = recv(ss_sock, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0 || strcmp(buffer, "STOP") == 0) break;
        
        // Append to script content
        int space_left = (MAX_COMMIT_LEN * 10) - total_bytes - 1;
        int to_copy = (n < space_left) ? n : space_left;
        if (to_copy > 0) {
            memcpy(script_content + total_bytes, buffer, to_copy);
            total_bytes += to_copy;
        }
        
        if (total_bytes >= (MAX_COMMIT_LEN * 10) - 1) {
            printf("NS: Warning - script file too large, truncating\n");
            break;
        }
    }
    
    close(ss_sock);
    script_content[total_bytes] = '\0';
    
    if (total_bytes == 0) {
        free(script_content);
        res.status = ERR_FILE_NOT_FOUND;
        sprintf(res.message, "Error: File is empty or could not be read.");
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // 5. Send acknowledgment to client
    res.status = ERR_OK;
    sprintf(res.message, "Executing script '%s'...", req->path);
    SEND_SERVER_RESPONSE(sock, &res);
    
    // 6. Execute script directly (blocking, but that's OK - this client waits)
    printf("NS: Executing '%s' for user '%s' (blocking this client session)\n", 
           req->path, req->username);
    
    // Create a temporary script file
    char temp_script_path[MAX_PATH_LEN];
    snprintf(temp_script_path, sizeof(temp_script_path), "/tmp/ns_exec_%d_%ld.sh", 
             sock, (long)time(NULL));
    
    FILE* script_file = fopen(temp_script_path, "w");
    if (script_file == NULL) {
        const char* err_msg = "Error: Failed to create temporary script file.\nEXEC_STOP";
        send(sock, err_msg, strlen(err_msg), 0);
        free(script_content);
        return;
    }
    
    fprintf(script_file, "%s", script_content);
    fclose(script_file);
    free(script_content); // Done with script content
    
    // Make script executable
    chmod(temp_script_path, 0755);
    
    // Execute script and stream output
    char command[MAX_PATH_LEN + 20];
    snprintf(command, sizeof(command), "/bin/bash %s 2>&1", temp_script_path);
    
    FILE* pipe = popen(command, "r");
    if (pipe == NULL) {
        const char* err_msg = "Error: Failed to execute script.\nEXEC_STOP";
        send(sock, err_msg, strlen(err_msg), 0);
        remove(temp_script_path);
        return;
    }
    
    // Stream output line by line
    char output_buffer[MAX_BUFFER_LEN];
    while (fgets(output_buffer, sizeof(output_buffer), pipe) != NULL) {
        int sent = send(sock, output_buffer, strlen(output_buffer), 0);
        if (sent <= 0) {
            printf("NS: Client disconnected during EXEC\n");
            break;
        }
    }
    
    int exit_code = pclose(pipe);
    
    // Send completion message
    char completion_msg[MAX_BUFFER_LEN];
    snprintf(completion_msg, sizeof(completion_msg), 
             "\n--- Execution Complete (exit code: %d) ---\n", WEXITSTATUS(exit_code));
    send(sock, completion_msg, strlen(completion_msg), 0);
    send(sock, "EXEC_STOP", 10, 0);
    
    // Cleanup
    remove(temp_script_path);
    
    printf("NS: EXEC completed for '%s', client session continues\n", req->path);
    // Session continues - socket remains open for next request
}

void handle_op_read(int sock, ClientRequest* req) {
    ServerResponse res;
    memset(&res, 0, sizeof(ServerResponse));
    
    // 1. Find the file (using cache)
    FileInfo* file = ns_file_get_cached(req->path);

    if (file == NULL) {
        res.status = ERR_FILE_NOT_FOUND;
        sprintf(res.message, "Error: File '%s' not found.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }

    // 2. Check read permissions
    if (!ns_check_permission(file, req->username, 'R')) {
        free_file_info_copy(file);  // Free cached copy
        res.status = ERR_ACCESS_DENIED;
        sprintf(res.message, "Error: You don't have read access to '%s'.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }

    // 3. Find an active SS that has this file
    SSInfo* target_ss = NULL;
    for (int i = 0; i < file->ss_count; i++) {
        if (file->ss_list[i]->is_active) {
            target_ss = file->ss_list[i];
            break;
        }
    }

    if (target_ss == NULL) {
        free_file_info_copy(file);  // Free cached copy
        res.status = ERR_SS_NOT_FOUND;
        sprintf(res.message, "Error: No active servers found for file '%s'.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }

    // 4. Send redirect response to the client
    res.status = ERR_OK;
    strncpy(res.ss_ip, target_ss->ip, INET_ADDRSTRLEN);
    res.ss_port = target_ss->client_port;
    sprintf(res.message, "Redirecting to SS at %s:%d", res.ss_ip, res.ss_port);
    
    free_file_info_copy(file);  // Free cached copy
    SEND_SERVER_RESPONSE(sock, &res);
}

void handle_op_write(int sock, ClientRequest* req) {
    ServerResponse res;
    memset(&res, 0, sizeof(ServerResponse));
    
    // 1. Find the file (using cache)
    FileInfo* file = ns_file_get_cached(req->path);

    if (file == NULL) {
        res.status = ERR_FILE_NOT_FOUND;
        sprintf(res.message, "Error: File '%s' not found.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }

    // 2. Check write permissions
    if (!ns_check_permission(file, req->username, 'W')) {
        free_file_info_copy(file);
        res.status = ERR_ACCESS_DENIED;
        sprintf(res.message, "Error: You don't have write access to '%s'.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }

    // 3. Find an active SS that has this file
    SSInfo* target_ss = NULL;
    for (int i = 0; i < file->ss_count; i++) {
        if (file->ss_list[i]->is_active) {
            target_ss = file->ss_list[i];
            break;
        }
    }

    if (target_ss == NULL) {
        free_file_info_copy(file);
        res.status = ERR_SS_NOT_FOUND;
        sprintf(res.message, "Error: No active servers found for file '%s'.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }

    // 4. Increment active writers counter for this SS
    pthread_mutex_lock(&target_ss->writers_lock);
    target_ss->active_writers++;
    int writer_count = target_ss->active_writers;
    pthread_mutex_unlock(&target_ss->writers_lock);
    
    printf("NS: Routing WRITE to SS '%s' (active_writers: %d)\n", target_ss->name, writer_count);
    
    // 5. Send redirect response to the client
    res.status = ERR_OK;
    strncpy(res.ss_ip, target_ss->ip, INET_ADDRSTRLEN);
    res.ss_port = target_ss->client_port;
    sprintf(res.message, "Redirecting to SS at %s:%d for WRITE", res.ss_ip, res.ss_port);
    
    // Invalidate cache since file will be modified
    lru_cache_invalidate(ns.lru_cache, req->path);
    
    free_file_info_copy(file);
    SEND_SERVER_RESPONSE(sock, &res);
}


// --- INFO Operation Handler ---
void handle_op_info(int sock, ClientRequest* req) {
    ServerResponse res;
    memset(&res, 0, sizeof(ServerResponse));
    
    // 1. Find the file (using cache)
    FileInfo* file = ns_file_get_cached(req->path);

    if (file == NULL) {
        res.status = ERR_FILE_NOT_FOUND;
        sprintf(res.message, "Error: File '%s' not found.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }

    // 2. TODO: Check read permissions
    // For now, allow anyone to view info

    // 3. Format the info message
    res.status = ERR_OK;
    
    char created_str[64], modified_str[64], accessed_str[64];
    struct tm* tm_info;
    
    if (file->created > 0) {
        tm_info = localtime(&file->created);
        strftime(created_str, sizeof(created_str), "%Y-%m-%d %H:%M:%S", tm_info);
    } else {
        strcpy(created_str, "N/A");
    }
    
    if (file->modified > 0) {
        tm_info = localtime(&file->modified);
        strftime(modified_str, sizeof(modified_str), "%Y-%m-%d %H:%M:%S", tm_info);
    } else {
        strcpy(modified_str, "N/A");
    }
    
    if (file->last_accessed > 0) {
        tm_info = localtime(&file->last_accessed);
        strftime(accessed_str, sizeof(accessed_str), "%Y-%m-%d %H:%M:%S", tm_info);
    } else {
        strcpy(accessed_str, "N/A");
    }
    
    // Build access list strings (owner always has R/W/X access)
    char read_list[100] = "Owner";
    char write_list[100] = "Owner";
    char exec_list[100] = "Owner";
    
    // Add other users with read access (not owner)
    for (int i = 0; i < file->read_count && i < 2; i++) {
        if (strlen(read_list) + strlen(file->read_access[i]) + 3 < sizeof(read_list)) {
            strcat(read_list, ", ");
            strcat(read_list, file->read_access[i]);
        }
    }
    if (file->read_count > 2) strcat(read_list, "...");
    
    // Add other users with write access (not owner)
    for (int i = 0; i < file->write_count && i < 2; i++) {
        if (strlen(write_list) + strlen(file->write_access[i]) + 3 < sizeof(write_list)) {
            strcat(write_list, ", ");
            strcat(write_list, file->write_access[i]);
        }
    }
    if (file->write_count > 2) strcat(write_list, "...");
    
    // Add other users with exec access (not owner)
    for (int i = 0; i < file->exec_count && i < 2; i++) {
        if (strlen(exec_list) + strlen(file->exec_access[i]) + 3 < sizeof(exec_list)) {
            strcat(exec_list, ", ");
            strcat(exec_list, file->exec_access[i]);
        }
    }
    if (file->exec_count > 2) strcat(exec_list, "...");
    
    // Count active servers only
    int active_ss_count = 0;
    for (int i = 0; i < file->ss_count; i++) {
        if (file->ss_list[i]->is_active) {
            active_ss_count++;
        }
    }
    
    snprintf(res.message, sizeof(res.message),
             "Path: %s\nOwner: %s\n"
             "Created: %s\n"
             "Modified: %s\n"
             "Accessed: %s by %s\n"
             "Size: %ld | Words: %d | Chars: %d\n"
             "Read: %s\nWrite: %s\nExec: %s\n"
             "Servers: %d (active: %d)",
             file->path,
             file->owner,
             created_str,
             modified_str,
             accessed_str,
             strlen(file->last_accessed_by) > 0 ? file->last_accessed_by : "N/A",
             file->size,
             file->word_count,
             file->char_count,
             read_list,
             write_list,
             exec_list,
             file->ss_count,
             active_ss_count);
    
    free_file_info_copy(file);
    SEND_SERVER_RESPONSE(sock, &res);
}


// --- Helper Functions ---
int ns_is_client_in_list(ClientList* list, const char* username) {
    ClientInfo* current = list->head;
    while (current != NULL) {
        if (strcmp(current->username, username) == 0) {
            return 1;
        }
        current = current->next;
    }
    return 0;
}

void ns_add_client(ClientList* list, const char* username, int sock) {
    ClientInfo* new_client = (ClientInfo*)malloc(sizeof(ClientInfo));
    strcpy(new_client->username, username);
    new_client->sock = sock;
    new_client->next = list->head;
    list->head = new_client;
}

void ns_remove_client(ClientList* list, const char* username) {
    ClientInfo *current = list->head, *prev = NULL;
    while (current != NULL) {
        if (strcmp(current->username, username) == 0) {
            if (prev == NULL) {
                list->head = current->next;
            } else {
                prev->next = current->next;
            }
            return;
        }
        prev = current;
        current = current->next;
    }
}

// --- Access Control Handlers ---

void handle_op_addaccess(int sock, ClientRequest* req) {
    ServerResponse res;
    memset(&res, 0, sizeof(ServerResponse));
    
    // arg1 = username to add, arg2[0] = type ('R', 'W', or 'X')
    char* target_user = req->arg1;
    char type = req->arg2[0];
    
    pthread_mutex_lock(&ns.file_index->lock);
    FileInfo* file = ns_file_index_get(req->path);
    
    if (file == NULL) {
        pthread_mutex_unlock(&ns.file_index->lock);
        res.status = ERR_FILE_NOT_FOUND;
        sprintf(res.message, "Error: File '%s' not found.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // Only owner can add access
    if (strcmp(file->owner, req->username) != 0) {
        pthread_mutex_unlock(&ns.file_index->lock);
        res.status = ERR_NOT_OWNER;
        sprintf(res.message, "Error: Only the owner can add access.");
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    ErrorCode result = ns_add_access(file, target_user, type);
    
    // Release lock before broadcasting (network I/O)
    pthread_mutex_unlock(&ns.file_index->lock);
    
    // Broadcast info update to sync access lists to all SSs
    if (result == ERR_OK || result == ERR_FILE_EXISTS) {
        ns_broadcast_info_update(file);
        // Invalidate cache since access list changed
        lru_cache_invalidate(ns.lru_cache, req->path);
    }
    
    if (result == ERR_OK) {
        res.status = ERR_OK;
        sprintf(res.message, "Access (%c) granted to '%s' for '%s'.", type, target_user, req->path);
    } else if (result == ERR_FILE_EXISTS) {
        res.status = ERR_FILE_EXISTS;
        sprintf(res.message, "User '%s' already has %c access.", target_user, type);
    } else {
        res.status = result;
        sprintf(res.message, "Error adding access.");
    }
    
    SEND_SERVER_RESPONSE(sock, &res);
}

void handle_op_remaccess(int sock, ClientRequest* req) {
    ServerResponse res;
    memset(&res, 0, sizeof(ServerResponse));
    
    char* target_user = req->arg1;
    char type = req->arg2[0];
    
    pthread_mutex_lock(&ns.file_index->lock);
    FileInfo* file = ns_file_index_get(req->path);
    
    if (file == NULL) {
        pthread_mutex_unlock(&ns.file_index->lock);
        res.status = ERR_FILE_NOT_FOUND;
        sprintf(res.message, "Error: File '%s' not found.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // Only owner can remove access
    if (strcmp(file->owner, req->username) != 0) {
        pthread_mutex_unlock(&ns.file_index->lock);
        res.status = ERR_NOT_OWNER;
        sprintf(res.message, "Error: Only the owner can remove access.");
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    ErrorCode result = ns_remove_access(file, target_user, type);
    
    // Release lock before broadcasting (network I/O)
    pthread_mutex_unlock(&ns.file_index->lock);
    
    // Broadcast info update to sync access lists to all SSs
    if (result == ERR_OK) {
        ns_broadcast_info_update(file);
        // Invalidate cache since access list changed
        lru_cache_invalidate(ns.lru_cache, req->path);
    }
    
    if (result == ERR_OK) {
        res.status = ERR_OK;
        sprintf(res.message, "Access (%c) removed from '%s' for '%s'.", type, target_user, req->path);
    } else if (result == ERR_FILE_NOT_FOUND) {
        res.status = ERR_FILE_NOT_FOUND;
        sprintf(res.message, "User '%s' didn't have %c access.", target_user, type);
    } else {
        res.status = result;
        sprintf(res.message, "Error removing access.");
    }
    
    SEND_SERVER_RESPONSE(sock, &res);
}

void handle_op_reqaccess(int sock, ClientRequest* req) {
    ServerResponse res;
    memset(&res, 0, sizeof(ServerResponse));
    
    char type = req->arg2[0];
    type = toupper(type);
    
    pthread_mutex_lock(&ns.file_index->lock);
    FileInfo* file = ns_file_index_get(req->path);
    
    if (file == NULL) {
        pthread_mutex_unlock(&ns.file_index->lock);
        res.status = ERR_FILE_NOT_FOUND;
        sprintf(res.message, "Error: File '%s' not found.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // Check if user already has access
    if (ns_check_permission(file, req->username, type)) {
        pthread_mutex_unlock(&ns.file_index->lock);
        res.status = ERR_FILE_EXISTS;
        sprintf(res.message, "You already have %c access to '%s'.", type, req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    pthread_mutex_unlock(&ns.file_index->lock);
    
    // Add request to queue
    int request_id = ns_add_access_request(req->username, req->path, type);
    
    // Save to persistent storage
    ns_save_access_requests();
    
    res.status = ERR_OK;
    sprintf(res.message, "Access request submitted (ID: %d). Owner will be notified.", request_id);
    SEND_SERVER_RESPONSE(sock, &res);
}

void handle_op_reqlist(int sock, ClientRequest* req) {
    ServerResponse res;
    memset(&res, 0, sizeof(ServerResponse));
    
    char message[MAX_BUFFER_LEN] = "Pending Access Requests:\n";
    int count = 0;
    
    pthread_mutex_lock(&ns.access_requests->lock);
    AccessRequest* current = ns.access_requests->head;
    
    while (current != NULL) {
        // Check if this request is for a file owned by the requesting user
        pthread_mutex_lock(&ns.file_index->lock);
        FileInfo* file = ns_file_index_get(current->path);
        pthread_mutex_unlock(&ns.file_index->lock);
        
        if (file != NULL && strcmp(file->owner, req->username) == 0) {
            char line[512];
            snprintf(line, sizeof(line), "  [%d] %s requests %c access to %s\n",
                     current->id, current->username, current->type, current->path);
            if (strlen(message) + strlen(line) < MAX_BUFFER_LEN - 1) {
                strcat(message, line);
                count++;
            }
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&ns.access_requests->lock);
    
    if (count == 0) {
        strcpy(message, "No pending access requests.");
    }
    
    res.status = ERR_OK;
    strncpy(res.message, message, MAX_BUFFER_LEN - 1);
    SEND_SERVER_RESPONSE(sock, &res);
}

void handle_op_approve(int sock, ClientRequest* req) {
    ServerResponse res;
    memset(&res, 0, sizeof(ServerResponse));
    
    int request_id = req->index; // Reusing index field for request ID
    
    AccessRequest* access_req = ns_get_access_request(request_id);
    
    if (access_req == NULL) {
        res.status = ERR_FILE_NOT_FOUND;
        sprintf(res.message, "Error: Request ID %d not found.", request_id);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // Check if requester is the owner of the file
    pthread_mutex_lock(&ns.file_index->lock);
    FileInfo* file = ns_file_index_get(access_req->path);
    
    if (file == NULL) {
        pthread_mutex_unlock(&ns.file_index->lock);
        res.status = ERR_FILE_NOT_FOUND;
        sprintf(res.message, "Error: File '%s' no longer exists.", access_req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    if (strcmp(file->owner, req->username) != 0) {
        pthread_mutex_unlock(&ns.file_index->lock);
        res.status = ERR_NOT_OWNER;
        sprintf(res.message, "Error: Only the owner can approve requests.");
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // Grant the access
    ErrorCode result = ns_add_access(file, access_req->username, access_req->type);
    
    // Release lock before broadcasting (network I/O)
    pthread_mutex_unlock(&ns.file_index->lock);
    
    // Broadcast info update to sync access lists to all SSs
    ns_broadcast_info_update(file);
    
    // Invalidate cache since access list changed
    lru_cache_invalidate(ns.lru_cache, access_req->path);
    
    // Save data before removing request (it will be freed)
    char saved_username[MAX_USERNAME_LEN];
    char saved_path[MAX_PATH_LEN];
    char saved_type = access_req->type;
    strncpy(saved_username, access_req->username, MAX_USERNAME_LEN);
    strncpy(saved_path, access_req->path, MAX_PATH_LEN);
    
    // Remove the request
    ns_remove_access_request(request_id);
    
    // Save to persistent storage
    ns_save_access_requests();
    
    if (result == ERR_OK || result == ERR_FILE_EXISTS) {
        res.status = ERR_OK;
        sprintf(res.message, "Access granted to '%s' for '%s' (%c).",
                saved_username, saved_path, saved_type);
    } else {
        res.status = result;
        sprintf(res.message, "Error granting access.");
    }
    
    SEND_SERVER_RESPONSE(sock, &res);
}

void handle_op_reject(int sock, ClientRequest* req) {
    ServerResponse res;
    memset(&res, 0, sizeof(ServerResponse));
    
    int request_id = req->index;
    
    AccessRequest* access_req = ns_get_access_request(request_id);
    
    if (access_req == NULL) {
        res.status = ERR_FILE_NOT_FOUND;
        sprintf(res.message, "Error: Request ID %d not found.", request_id);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // Check if requester is the owner of the file
    pthread_mutex_lock(&ns.file_index->lock);
    FileInfo* file = ns_file_index_get(access_req->path);
    
    if (file == NULL) {
        pthread_mutex_unlock(&ns.file_index->lock);
        res.status = ERR_FILE_NOT_FOUND;
        sprintf(res.message, "Error: File '%s' no longer exists.", access_req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    if (strcmp(file->owner, req->username) != 0) {
        pthread_mutex_unlock(&ns.file_index->lock);
        res.status = ERR_NOT_OWNER;
        sprintf(res.message, "Error: Only the owner can reject requests.");
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    pthread_mutex_unlock(&ns.file_index->lock);
    
    // Remove the request
    char username[MAX_USERNAME_LEN], path[MAX_PATH_LEN];
    char type;
    strncpy(username, access_req->username, MAX_USERNAME_LEN);
    strncpy(path, access_req->path, MAX_PATH_LEN);
    type = access_req->type;
    
    ns_remove_access_request(request_id);
    
    // Save to persistent storage
    ns_save_access_requests();
    
    res.status = ERR_OK;
    sprintf(res.message, "Request from '%s' for '%s' (%c) rejected.", username, path, type);
    SEND_SERVER_RESPONSE(sock, &res);
}

// --- CREATEFOLDER Operation Handler ---
void handle_op_createfolder(int sock, ClientRequest* req) {
    ServerResponse res;
    memset(&res, 0, sizeof(ServerResponse));
    
    // Find an active SS to create the folder on
    pthread_mutex_lock(&ns.ss_list_lock);
    SSInfo* target_ss = ns.ss_list_head;
    while (target_ss != NULL && !target_ss->is_active) {
        target_ss = target_ss->next;
    }
    
    if (target_ss == NULL) {
        pthread_mutex_unlock(&ns.ss_list_lock);
        res.status = ERR_SS_NOT_FOUND;
        sprintf(res.message, "Error: No available Storage Servers to create folder.");
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // Send command to the SS
    NSOpCode op = NS_SS_CREATEFOLDER;
    pthread_mutex_lock(&target_ss->cmd_lock);
    SEND_OPCODE(target_ss->cmd_sock, op);
    send_full(target_ss->cmd_sock, req->path, MAX_PATH_LEN);
    
    // Receive response from SS
    ServerResponse ss_res;
    RECV_SERVER_RESPONSE(target_ss->cmd_sock, &ss_res);
    pthread_mutex_unlock(&target_ss->cmd_lock);
    
    pthread_mutex_unlock(&ns.ss_list_lock);
    
    // Forward response to client
    if (ss_res.status == ERR_OK) {
        printf("NS: Folder '%s' created on SS %s\n", req->path, target_ss->ip);
        
        // Replicate to backup SSs asynchronously
        pthread_mutex_lock(&ns.ss_list_lock);
        SSInfo* current = ns.ss_list_head;
        while (current != NULL) {
            if (current->is_active && current != target_ss) {
                NSOpCode repl_op = NS_SS_CREATEFOLDER;
                pthread_mutex_lock(&current->cmd_lock);
                SEND_OPCODE(current->cmd_sock, repl_op);
                send_full(current->cmd_sock, req->path, MAX_PATH_LEN);
                
                // Don't wait for response - async replication
                ServerResponse dummy_res;
                RECV_SERVER_RESPONSE(current->cmd_sock, &dummy_res);
                pthread_mutex_unlock(&current->cmd_lock);
            }
            current = current->next;
        }
        pthread_mutex_unlock(&ns.ss_list_lock);
    }
    
    SEND_SERVER_RESPONSE(sock, &ss_res);
}

// --- MOVE Operation Handler ---
void handle_op_move(int sock, ClientRequest* req) {
    ServerResponse res;
    memset(&res, 0, sizeof(ServerResponse));
    
    // 1. Find the file in index
    pthread_mutex_lock(&ns.file_index->lock);
    FileInfo* file = ns_file_index_get(req->path);
    
    if (file == NULL) {
        pthread_mutex_unlock(&ns.file_index->lock);
        res.status = ERR_FILE_NOT_FOUND;
        sprintf(res.message, "Error: File '%s' not found.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // 2. Check permissions (owner or write access)
    if (strcmp(file->owner, req->username) != 0 && 
        !ns_has_write_access(file, req->username)) {
        pthread_mutex_unlock(&ns.file_index->lock);
        res.status = ERR_ACCESS_DENIED;
        sprintf(res.message, "Error: No write permission for '%s'.", req->path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // 3. Construct new path (arg1 contains target folder/new name)
    char new_path[MAX_PATH_LEN];
    snprintf(new_path, sizeof(new_path), "%s", req->arg1);
    
    // 4. Check if destination already exists
    if (ns_file_index_get(new_path) != NULL) {
        pthread_mutex_unlock(&ns.file_index->lock);
        res.status = ERR_FILE_EXISTS;
        sprintf(res.message, "Error: Destination '%s' already exists.", new_path);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    
    // 5. Get list of SSs that have this file
    SSInfo** ss_list = (SSInfo**)malloc(sizeof(SSInfo*) * file->ss_count);
    int ss_count = file->ss_count;
    for (int i = 0; i < ss_count; i++) {
        ss_list[i] = file->ss_list[i];
    }
    pthread_mutex_unlock(&ns.file_index->lock);
    
    // 6. Send MOVE command to all SSs that have this file
    int success = 1;
    pthread_mutex_lock(&ns.ss_list_lock);
    for (int i = 0; i < ss_count; i++) {
        SSInfo* ss = ss_list[i];
        if (ss->is_active) {
            NSOpCode op = NS_SS_MOVE;
            pthread_mutex_lock(&ss->cmd_lock);
            SEND_OPCODE(ss->cmd_sock, op);
            send_full(ss->cmd_sock, req->path, MAX_PATH_LEN);  // Old path
            send_full(ss->cmd_sock, new_path, MAX_PATH_LEN);   // New path
            
            ServerResponse ss_res;
            RECV_SERVER_RESPONSE(ss->cmd_sock, &ss_res);
            pthread_mutex_unlock(&ss->cmd_lock);
            
            if (ss_res.status != ERR_OK) {
                success = 0;
                memcpy(&res, &ss_res, sizeof(ServerResponse));
            }
        }
    }
    pthread_mutex_unlock(&ns.ss_list_lock);
    
    free(ss_list);
    
    // 7. Update file index if successful
    if (success) {
        pthread_mutex_lock(&ns.file_index->lock);
        
        // Remove old entry
        file = ns_file_index_get(req->path);
        if (file != NULL) {
            // Save important data
            char owner[MAX_USERNAME_LEN];
            strcpy(owner, file->owner);
            SSInfo** stored_ss_list = file->ss_list;
            int stored_ss_count = file->ss_count;
            
            // Remove from old path
            ns_file_index_remove(req->path);
            
            // Add with new path
            ns_file_index_add_with_ss(new_path, owner, stored_ss_list, stored_ss_count);
        }
        
        pthread_mutex_unlock(&ns.file_index->lock);
        
        // Invalidate cache for both old and new paths
        lru_cache_invalidate(ns.lru_cache, req->path);
        lru_cache_invalidate(ns.lru_cache, new_path);
        
        res.status = ERR_OK;
        sprintf(res.message, "File moved from '%s' to '%s'.", req->path, new_path);
        printf("NS: File '%s' moved to '%s' by '%s'\n", req->path, new_path, req->username);
    }
    
    SEND_SERVER_RESPONSE(sock, &res);
}

void handle_op_viewfolder(int sock, ClientRequest* req) {
    ServerResponse res;
    memset(&res, 0, sizeof(ServerResponse));
    
    // List all files that start with the folder path
    pthread_mutex_lock(&ns.file_index->lock);
    
    char folder_prefix[MAX_PATH_LEN + 2];  // +2 for slash and null terminator
    int prefix_len;
    if (strlen(req->path) == 0 || strcmp(req->path, ".") == 0) {
        // Root folder - list all files without prefix
        folder_prefix[0] = '\0';
        prefix_len = 0;
    } else {
        snprintf(folder_prefix, sizeof(folder_prefix) - 1, "%s/", req->path);
        prefix_len = strlen(folder_prefix);
    }
    
    // Build list of files in this folder (use dynamic allocation to avoid truncation)
    char* file_list = malloc(MAX_BUFFER_LEN * 8);
    if (file_list == NULL) {
        pthread_mutex_unlock(&ns.file_index->lock);
        res.status = ERR_FILE_NOT_FOUND;
        sprintf(res.message, "Error: Memory allocation failed.");
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    file_list[0] = '\0';
    int count = 0;
    
    for (int i = 0; i < FILE_INDEX_SIZE; i++) {
        FileInfo* file = ns.file_index->table[i];
        while (file != NULL) {
            // Check if file has at least one active SS
            int has_active_ss = 0;
            for (int j = 0; j < file->ss_count; j++) {
                if (file->ss_list[j]->is_active) {
                    has_active_ss = 1;
                    break;
                }
            }
            
            // Check if file path starts with folder prefix
            int matches = (prefix_len == 0) || (strncmp(file->path, folder_prefix, prefix_len) == 0);
            
            if (has_active_ss && matches) {
                // Check if user has access
                if (strcmp(file->owner, req->username) == 0 ||
                    ns_has_read_access(file, req->username) ||
                    ns_has_write_access(file, req->username)) {
                    
                    // Extract just the filename (relative to folder)
                    const char* relative_name = (prefix_len == 0) ? file->path : file->path + prefix_len;
                    
                    // Only show files directly in this folder (not subdirectories)
                    if (strchr(relative_name, '/') == NULL) {
                        if (count > 0) strcat(file_list, "\n");
                        strncat(file_list, relative_name, MAX_PATH_LEN);
                        count++;
                    }
                }
            }
            file = file->next;
        }
    }
    
    pthread_mutex_unlock(&ns.file_index->lock);
    
    res.status = ERR_OK;
    if (count == 0) {
        snprintf(res.message, sizeof(res.message), "Folder '%s' is empty or does not exist.", req->path);
    } else {
        snprintf(res.message, sizeof(res.message), "Files in '%s':", req->path);
        // Append file list in a separate send if needed, but for now truncate to fit
        size_t msg_len = strlen(res.message);
        size_t remaining = sizeof(res.message) - msg_len - 2;
        if (strlen(file_list) < remaining) {
            strcat(res.message, "\n");
            strcat(res.message, file_list);
        } else {
            strcat(res.message, "\n(List truncated - too many files)");
        }
    }
    
    free(file_list);
    SEND_SERVER_RESPONSE(sock, &res);
}
