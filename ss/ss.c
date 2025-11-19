#include "ss.h"
#include <errno.h>
#include <dirent.h> 
#include <sys/stat.h> 

// Define the global config
SS_Config g_config;
BackupState g_backup_state;

// --- NEW: Global list for open files ---
OpenFile* g_open_files_list = NULL;
pthread_mutex_t g_open_files_mutex = PTHREAD_MUTEX_INITIALIZER;
// --- END NEW ---

// --- Synchronization for port assignment ---
pthread_mutex_t port_init_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t port_init_cond = PTHREAD_COND_INITIALIZER;
int port_is_ready = 0;

// --- NEW: Helper function for replication ---
int connect_to_server(const char* ip, int port) {
    int sock;
    struct sockaddr_in addr;
    
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return -1;
    }
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        perror("Invalid address");
        return -1;
    }
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Connection Failed");
        return -1;
    }
    return sock;
}
// --- END NEW ---

// Extract SS name from root directory path
void extract_ss_name(const char* root_path, char* name_out) {
    const char* last_slash = strrchr(root_path, '/');
    if (last_slash) {
        strcpy(name_out, last_slash + 1);
    } else {
        strcpy(name_out, root_path);
    }
}

// Helper to create directories if they don't exist
void ss_init_dirs() {
    char path[MAX_PATH_LEN * 2];
    mkdir(g_config.root_dir, 0777);
    
    sprintf(path, "%s/file_dir", g_config.root_dir);
    mkdir(path, 0777);
    sprintf(path, "%s/info_dir", g_config.root_dir);
    mkdir(path, 0777);
    sprintf(path, "%s/undo_dir", g_config.root_dir);
    mkdir(path, 0777);
    sprintf(path, "%s/checkpoint_dir", g_config.root_dir);
    mkdir(path, 0777);

    printf("SS: Verified directory structure at %s\n", g_config.root_dir);
}

// Checks if file_dir is empty
int is_dir_empty(const char* path) {
    DIR* dir = opendir(path);
    if (dir == NULL) {
        perror("opendir in is_dir_empty");
        return 1; // Assume empty if we can't open it
    }

    struct dirent* entry;
    int is_empty = 1; // Assume empty until proven otherwise

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            is_empty = 0; // Found a file/dir, so it's not empty
            break;
        }
    }

    closedir(dir);
    return is_empty;
}


// --- Function to read info file content ---
void read_info_file(const char* info_path, char* out_buffer) {
    FILE* f = fopen(info_path, "r");
    if (f == NULL) {
        strcpy(out_buffer, "owner: unknown\n"); // Default if info file missing
        return;
    }
    
    size_t len = fread(out_buffer, 1, MAX_INFO_LEN - 1, f);
    out_buffer[len] = '\0';
    fclose(f);
}

// --- Recursive function to scan directories and send files ---
void scan_and_send(int ns_sock, const char* base_dir, const char* rel_path) {
    char full_path[MAX_PATH_LEN * 3];
    sprintf(full_path, "%s/%s", base_dir, rel_path);

    DIR* dir = opendir(full_path);
    if (dir == NULL) {
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        char new_rel_path[MAX_PATH_LEN]; 
        int len;
        if (strlen(rel_path) == 0) {
            len = snprintf(new_rel_path, MAX_PATH_LEN, "%s", entry->d_name);
        } else {
            len = snprintf(new_rel_path, MAX_PATH_LEN, "%s/%s", rel_path, entry->d_name);
        }

        if (len >= MAX_PATH_LEN) {
            fprintf(stderr, "SS: Path is too long, skipping: %s/%s\n", rel_path, entry->d_name);
            continue; 
        }

        char entry_full_path[MAX_PATH_LEN * 3]; 
        sprintf(entry_full_path, "%s/%s", base_dir, new_rel_path);

        struct stat st;
        if (stat(entry_full_path, &st) == -1) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            scan_and_send(ns_sock, base_dir, new_rel_path);
        } else {
            SS_File_Sync_Packet sync_pkt;
            strcpy(sync_pkt.path, new_rel_path);
            
            char info_path[MAX_PATH_LEN * 2];
            get_full_path("info_dir", new_rel_path, info_path);
            read_info_file(info_path, sync_pkt.info_content);
            
            printf("SS: Syncing file: %s\n", sync_pkt.path);
            send(ns_sock, &sync_pkt, sizeof(SS_File_Sync_Packet), 0);
        }
    }
    closedir(dir);
}

// --- Main function to start the file sync ---
void ss_send_file_index(int ns_sock) {
    char file_dir_path[MAX_PATH_LEN * 2];
    sprintf(file_dir_path, "%s/file_dir", g_config.root_dir);
    
    scan_and_send(ns_sock, file_dir_path, "");

    SS_File_Sync_Packet end_pkt;
    strcpy(end_pkt.path, SYNC_END_MARKER);
    send(ns_sock, &end_pkt, sizeof(SS_File_Sync_Packet), 0);
    
    printf("SS: File sync complete.\n");
}


// Connects to NS, registers, and sends file list
void ss_register_with_ns() {
    struct sockaddr_in ns_addr;
    
    printf("SS: Connecting to NS at %s:%d...\n", g_config.ip, g_config.ns_port);
    
    // 1. Create command socket (for receiving NS commands)
    if ((g_config.cmd_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("NS command socket creation error");
        exit(EXIT_FAILURE);
    }

    ns_addr.sin_family = AF_INET;
    ns_addr.sin_port = htons(g_config.ns_port);
    inet_pton(AF_INET, g_config.ip, &ns_addr.sin_addr);

    if (connect(g_config.cmd_sock, (struct sockaddr *)&ns_addr, sizeof(ns_addr)) < 0) {
        perror("NS command connection Failed");
        exit(EXIT_FAILURE);
    }
    printf("SS: Command socket connected to Name Server.\n");

    // 2. Create update socket (for sending async updates to NS)
    if ((g_config.update_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("NS update socket creation error");
        exit(EXIT_FAILURE);
    }

    if (connect(g_config.update_sock, (struct sockaddr *)&ns_addr, sizeof(ns_addr)) < 0) {
        perror("NS update connection Failed");
        exit(EXIT_FAILURE);
    }
    printf("SS: Update socket connected to Name Server.\n");

    // 3. Send Initial Packet on command socket
    InitialPacket init_pkt;
    init_pkt.type = CONN_SS;
    send(g_config.cmd_sock, &init_pkt, sizeof(InitialPacket), 0);

    // 4. Send Initial Packet on update socket
    send(g_config.update_sock, &init_pkt, sizeof(InitialPacket), 0);

    // 5. Prepare SS Info Packet
    SS_Info_Packet info_pkt;
    
    // NS will detect our IP from the socket peer address
    // We still send it for backwards compatibility/logging
    strcpy(info_pkt.ip, "0.0.0.0");  // Placeholder, NS ignores this
    
    info_pkt.port_for_clients = g_config.client_port; 
    info_pkt.backup_port = g_backup_state.backup_port;
    strcpy(info_pkt.name, g_backup_state.name);
    
    char file_dir_path[MAX_PATH_LEN * 2]; 
    sprintf(file_dir_path, "%s/file_dir", g_config.root_dir);
    info_pkt.is_empty = is_dir_empty(file_dir_path);
    
    // Get the local port of the update socket
    struct sockaddr_in local_addr;
    socklen_t addr_len = sizeof(local_addr);
    getsockname(g_config.update_sock, (struct sockaddr *)&local_addr, &addr_len);
    info_pkt.update_port = ntohs(local_addr.sin_port);
    
    // 6. Send SS Info Packet on BOTH sockets (for identification)
    send(g_config.cmd_sock, &info_pkt, sizeof(SS_Info_Packet), 0);
    send(g_config.update_sock, &info_pkt, sizeof(SS_Info_Packet), 0);
    printf("SS: Sent info packet to NS (name: %s, client_port: %d, backup_port: %d, update_port: %d, empty: %d)\n", 
           info_pkt.name, info_pkt.port_for_clients, info_pkt.backup_port, info_pkt.update_port, info_pkt.is_empty);

    // 7. Send File List on command socket
    if (!info_pkt.is_empty) {
        ss_send_file_index(g_config.cmd_sock);
    }
    
    printf("SS: Registered with NS. Listening for commands.\n");
}

int main(int argc, char* argv[]) {
    if (argc != 2 && argc != 4) {
        fprintf(stderr, "Usage: %s <ss_root_dir> [<ns_ip> <ns_port>]\n", argv[0]);
        fprintf(stderr, "\nArguments:\n");
        fprintf(stderr, "  ss_root_dir  : Root directory for this storage server\n");
        fprintf(stderr, "  ns_ip        : IP address of Name Server (default: 127.0.0.1)\n");
        fprintf(stderr, "  ns_port      : Port of Name Server (default: 8080)\n");
        fprintf(stderr, "\nExamples:\n");
        fprintf(stderr, "  %s tmp/ss1_root                      # NS at localhost:8080\n", argv[0]);
        fprintf(stderr, "  %s tmp/ss2_root 192.168.1.100 8080   # NS at 192.168.1.100:8080\n", argv[0]);
        fprintf(stderr, "\nNote: Client and backup ports are automatically assigned (ephemeral).\n");
        exit(1);
    }

    strncpy(g_config.root_dir, argv[1], MAX_PATH_LEN);
    
    // Parse NS connection info
    if (argc == 2) {
        // Just root_dir - use localhost defaults
        strcpy(g_config.ip, "127.0.0.1");
        g_config.ns_port = NS_LISTEN_PORT;
    } else if (argc == 4) {
        // Full specification: root_dir ns_ip ns_port
        strncpy(g_config.ip, argv[2], INET_ADDRSTRLEN - 1);
        g_config.ip[INET_ADDRSTRLEN - 1] = '\0';
        g_config.ns_port = atoi(argv[3]);
        if (g_config.ns_port <= 0 || g_config.ns_port > 65535) {
            fprintf(stderr, "Error: Invalid NS port %s\n", argv[3]);
            exit(1);
        }
    }

    // Initialize backup state
    extract_ss_name(g_config.root_dir, g_backup_state.name);
    g_backup_state.partner_name[0] = '\0';
    g_backup_state.partner_sync_sock = -1;
    pthread_mutex_init(&g_backup_state.sync_lock, NULL);

    // 1. Create SS directories
    ss_init_dirs();

    // 2. Start backup listener (ephemeral port)
    struct sockaddr_in backup_addr;
    if ((g_backup_state.backup_listen_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Backup listen socket creation error");
        exit(EXIT_FAILURE);
    }
    
    backup_addr.sin_family = AF_INET;
    backup_addr.sin_addr.s_addr = INADDR_ANY;
    backup_addr.sin_port = 0; // Ephemeral port
    
    if (bind(g_backup_state.backup_listen_sock, (struct sockaddr *)&backup_addr, sizeof(backup_addr)) < 0) {
        perror("Backup listen socket bind failed");
        exit(EXIT_FAILURE);
    }
    
    if (listen(g_backup_state.backup_listen_sock, 1) < 0) {
        perror("Backup listen failed");
        exit(EXIT_FAILURE);
    }
    
    // Get assigned backup port
    struct sockaddr_in assigned_addr;
    socklen_t addr_len = sizeof(assigned_addr);
    getsockname(g_backup_state.backup_listen_sock, (struct sockaddr *)&assigned_addr, &addr_len);
    g_backup_state.backup_port = ntohs(assigned_addr.sin_port);
    printf("SS: Backup listener started on port %d\n", g_backup_state.backup_port);
    
    pthread_t backup_tid;
    if (pthread_create(&backup_tid, NULL, backup_listener_thread, NULL) != 0) {
        perror("Failed to create backup listener thread");
        exit(EXIT_FAILURE);
    }
    pthread_detach(backup_tid);

    // 3. Start thread to listen for clients (for Type-3 ops)
    pthread_t client_tid;
    if (pthread_create(&client_tid, NULL, client_listener_thread, NULL) != 0) {
        perror("Failed to create client listener thread");
        exit(EXIT_FAILURE);
    }
    pthread_detach(client_tid);

    // --- Wait for the client listener thread to get its port ---
    pthread_mutex_lock(&port_init_lock);
    while (port_is_ready == 0) {
        pthread_cond_wait(&port_init_cond, &port_init_lock);
    }
    pthread_mutex_unlock(&port_init_lock);

    // 4. Connect and register with Name Server
    ss_register_with_ns();

    // 5. Main thread now listens for commands from NS
    handle_ns_commands(NULL);

    close(g_config.cmd_sock);
    close(g_config.update_sock);
    return 0;
}

// --- Backup Listener Thread ---
void* backup_listener_thread(void* arg) {
    (void)arg;
    printf("SS: Backup listener thread started\n");
    
    while (1) {
        int partner_sock = accept(g_backup_state.backup_listen_sock, NULL, NULL);
        if (partner_sock < 0) {
            perror("Accept partner connection failed");
            continue;
        }
        
        // Receive partner identification
        char partner_name[MAX_PATH_LEN];
        if (recv(partner_sock, partner_name, MAX_PATH_LEN, 0) <= 0) {
            close(partner_sock);
            continue;
        }
        
        // Verify this is expected partner
        pthread_mutex_lock(&g_backup_state.sync_lock);
        if (strlen(g_backup_state.partner_name) > 0 && 
            strcmp(partner_name, g_backup_state.partner_name) == 0) {
            printf("SS: Partner %s connected for sync\n", partner_name);
            g_backup_state.partner_sync_sock = partner_sock;
            int should_send = g_backup_state.should_send_full_sync;
            pthread_mutex_unlock(&g_backup_state.sync_lock);
            
            // Start thread to handle sync messages from partner
            pthread_t sync_handler_tid;
            pthread_create(&sync_handler_tid, NULL, handle_partner_sync, NULL);
            pthread_detach(sync_handler_tid);
            
            // Send full sync if NS told us to (even if we're the receiver of connection)
            if (should_send) {
                printf("SS: Sending full sync to partner (instructed by NS)\n");
                sync_all_files_to_partner();
            }
        } else {
            pthread_mutex_unlock(&g_backup_state.sync_lock);
            printf("SS: Unexpected partner connection from %s, rejecting\n", partner_name);
            close(partner_sock);
        }
    }
    return NULL;
}

// --- Handle Partner Sync Messages ---
void* handle_partner_sync(void* arg) {
    (void)arg;
    printf("SS: Partner sync handler started\n");
    
    while (1) {
        SSSyncOpCode op;
        
        pthread_mutex_lock(&g_backup_state.sync_lock);
        int sock = g_backup_state.partner_sync_sock;
        pthread_mutex_unlock(&g_backup_state.sync_lock);
        
        if (sock == -1) break;
        
        int bytes = recv(sock, &op, sizeof(op), 0);
        if (bytes <= 0) {
            // Partner disconnected
            printf("SS: Partner disconnected during sync\n");
            pthread_mutex_lock(&g_backup_state.sync_lock);
            close(g_backup_state.partner_sync_sock);
            g_backup_state.partner_sync_sock = -1;
            pthread_mutex_unlock(&g_backup_state.sync_lock);
            
            // Notify NS
            SSOpCode notify = SS_NS_PARTNER_DIED;
            send(g_config.update_sock, &notify, sizeof(notify), 0);
            // NS already knows partner_name, no need to send it
            break;
        }
        
        // Handle sync operations
        switch (op) {
            case SS_SYNC_CREATEFOLDER: {
                char path[MAX_PATH_LEN];
                recv(sock, path, MAX_PATH_LEN, 0);
                
                // Create folder in file_dir, info_dir, undo_dir with parent directories
                const char* dir_types[] = {"file_dir", "info_dir", "undo_dir"};
                for (int i = 0; i < 3; i++) {
                    char full_path[MAX_PATH_LEN * 3];
                    snprintf(full_path, sizeof(full_path), "%s/%s/%s", g_config.root_dir, dir_types[i], path);
                    
                    // Create parent directories recursively
                    char temp_path[MAX_PATH_LEN * 3];
                    snprintf(temp_path, sizeof(temp_path), "%s", full_path);
                    for (char* p = temp_path + 1; *p; p++) {
                        if (*p == '/') {
                            *p = '\0';
                            mkdir(temp_path, 0755);
                            *p = '/';
                        }
                    }
                    if (mkdir(full_path, 0755) != 0 && errno != EEXIST) {
                        perror("SS: Failed to create directory");
                        printf("SS: Could not create directory: %s\n", full_path);
                    }
                }
                
                printf("SS: [SYNC] Received CREATEFOLDER: %s\n", path);
                break;
            }
            
            case SS_SYNC_FILE_DATA: {
                SS_Sync_File_Packet file_pkt;
                recv(sock, &file_pkt, sizeof(file_pkt), 0);
                
                // Ensure parent directories exist before creating file
                char file_full_path[MAX_PATH_LEN * 3];
                snprintf(file_full_path, sizeof(file_full_path), "%s/file_dir/%s", g_config.root_dir, file_pkt.path);
                
                // Create parent directories if needed
                char* last_slash = strrchr(file_full_path, '/');
                if (last_slash != NULL) {
                    *last_slash = '\0';
                    // Create directory recursively (mkdir -p equivalent)
                    char temp_path[MAX_PATH_LEN * 3];
                    snprintf(temp_path, sizeof(temp_path), "%s", file_full_path);
                    for (char* p = temp_path + 1; *p; p++) {
                        if (*p == '/') {
                            *p = '\0';
                            mkdir(temp_path, 0755);
                            *p = '/';
                        }
                    }
                    mkdir(temp_path, 0755);
                    *last_slash = '/';
                }
                
                // Receive and write file content
                FILE* f = fopen(file_full_path, "wb");
                if (f) {
                    size_t remaining = file_pkt.file_size;
                    char buffer[4096];
                    while (remaining > 0) {
                        size_t to_read = (remaining < sizeof(buffer)) ? remaining : sizeof(buffer);
                        size_t bytes = recv(sock, buffer, to_read, 0);
                        if (bytes <= 0) break;
                        fwrite(buffer, 1, bytes, f);
                        remaining -= bytes;
                    }
                    fclose(f);
                } else {
                    perror("SS: Failed to create file");
                    printf("SS: Could not create file: %s\n", file_full_path);
                    // Still need to consume the data from socket
                    size_t remaining = file_pkt.file_size;
                    char buffer[4096];
                    while (remaining > 0) {
                        size_t to_read = (remaining < sizeof(buffer)) ? remaining : sizeof(buffer);
                        size_t bytes = recv(sock, buffer, to_read, 0);
                        if (bytes <= 0) break;
                        remaining -= bytes;
                    }
                }
                
                // Write info file (create parent dirs for info_dir too)
                char info_path[MAX_PATH_LEN * 3];
                snprintf(info_path, sizeof(info_path), "%s/info_dir/%s", g_config.root_dir, file_pkt.path);
                
                last_slash = strrchr(info_path, '/');
                if (last_slash != NULL) {
                    *last_slash = '\0';
                    char temp_path[MAX_PATH_LEN * 3];
                    snprintf(temp_path, sizeof(temp_path), "%s", info_path);
                    for (char* p = temp_path + 1; *p; p++) {
                        if (*p == '/') {
                            *p = '\0';
                            mkdir(temp_path, 0755);
                            *p = '/';
                        }
                    }
                    mkdir(temp_path, 0755);
                    *last_slash = '/';
                }
                
                FILE* info_f = fopen(info_path, "w");
                if (info_f) {
                    fprintf(info_f, "%s", file_pkt.info_content);
                    fclose(info_f);
                } else {
                    perror("SS: Failed to create info file");
                }
                
                // Create empty undo file (create parent dirs for undo_dir too)
                char undo_path[MAX_PATH_LEN * 3];
                snprintf(undo_path, sizeof(undo_path), "%s/undo_dir/%s", g_config.root_dir, file_pkt.path);
                
                last_slash = strrchr(undo_path, '/');
                if (last_slash != NULL) {
                    *last_slash = '\0';
                    char temp_path[MAX_PATH_LEN * 3];
                    snprintf(temp_path, sizeof(temp_path), "%s", undo_path);
                    for (char* p = temp_path + 1; *p; p++) {
                        if (*p == '/') {
                            *p = '\0';
                            mkdir(temp_path, 0755);
                            *p = '/';
                        }
                    }
                    mkdir(temp_path, 0755);
                    *last_slash = '/';
                }
                
                FILE* undo_f = fopen(undo_path, "wb");
                if (undo_f) {
                    if (file_pkt.undo_size > 0) {
                        fwrite(file_pkt.undo_content, 1, file_pkt.undo_size, undo_f);
                    }
                    fclose(undo_f);
                } else {
                    perror("SS: Failed to create undo file");
                }
                
                printf("SS: [SYNC] Received file from partner: %s (%ld bytes)\n", file_pkt.path, file_pkt.file_size);
                break;
            }
            
            case SS_SYNC_COMPLETE:
                printf("SS: Full sync complete from partner\n");
                break;
            
            case SS_SYNC_CREATE: {
                char path[MAX_PATH_LEN];
                char owner[MAX_PATH_LEN];
                recv(sock, path, MAX_PATH_LEN, 0);
                recv(sock, owner, MAX_PATH_LEN, 0);
                
                // Call handle_op_create to create the file
                handle_op_create(path, owner);
                printf("SS: [CHANGE-SYNC] Received CREATE: %s (owner: %s)\n", path, owner);
                break;
            }
            
            case SS_SYNC_DELETE: {
                char path[MAX_PATH_LEN];
                recv(sock, path, MAX_PATH_LEN, 0);
                
                // Call handle_op_delete to delete the file
                handle_op_delete(path);
                printf("SS: [CHANGE-SYNC] Received DELETE: %s\n", path);
                break;
            }
            
            case SS_SYNC_MOVE: {
                char old_path[MAX_PATH_LEN];
                char new_path[MAX_PATH_LEN];
                recv(sock, old_path, MAX_PATH_LEN, 0);
                recv(sock, new_path, MAX_PATH_LEN, 0);
                
                // Directly move files without sending response (no client socket)
                char old_paths[4][MAX_PATH_LEN * 3];
                char new_paths[4][MAX_PATH_LEN * 3];
                const char* dir_types[] = {"file_dir", "info_dir", "undo_dir", "checkpoint_dir"};
                
                for (int i = 0; i < 3; i++) {
                    snprintf(old_paths[i], sizeof(old_paths[i]), "%s/%s/%s", g_config.root_dir, dir_types[i], old_path);
                    snprintf(new_paths[i], sizeof(new_paths[i]), "%s/%s/%s", g_config.root_dir, dir_types[i], new_path);
                    
                    if (access(old_paths[i], F_OK) == 0) {
                        rename(old_paths[i], new_paths[i]);
                    }
                }
                
                printf("SS: [CHANGE-SYNC] Received MOVE: %s -> %s\n", old_path, new_path);
                break;
            }
            
            case SS_SYNC_CHECKPOINT: {
                char path[MAX_PATH_LEN];
                char tag[MAX_PATH_LEN];
                recv(sock, path, MAX_PATH_LEN, 0);
                recv(sock, tag, MAX_PATH_LEN, 0);
                
                // Call handle_op_checkpoint to create checkpoint
                handle_op_checkpoint(path, tag);
                printf("SS: [CHANGE-SYNC] Received CHECKPOINT: %s (tag: %s)\n", path, tag);
                break;
            }
                
            default:
                printf("SS: Unknown sync opcode %d\n", op);
                break;
        }
    }
    return NULL;
}

// --- Connect and Sync to Partner ---
void* connect_and_sync_to_partner(void* arg) {
    printf("SS: Connecting to partner %s at %s:%d\n", 
           g_backup_state.partner_name, g_backup_state.partner_ip, g_backup_state.partner_backup_port);
    
    // Retry connection with exponential backoff (partner might not have processed pair command yet)
    int sock = -1;
    int retry_delay_ms = 100; // Start with 100ms
    int max_retries = 5;
    
    for (int attempt = 0; attempt < max_retries; attempt++) {
        if (attempt > 0) {
            printf("SS: Retry %d/%d connecting to partner (delay %dms)...\n", 
                   attempt + 1, max_retries, retry_delay_ms);
            usleep(retry_delay_ms * 1000); // Convert to microseconds
            retry_delay_ms *= 2; // Exponential backoff
        }
        
        sock = connect_to_server(g_backup_state.partner_ip, g_backup_state.partner_backup_port);
        if (sock >= 0) {
            // Send my name for identification
            send(sock, g_backup_state.name, MAX_PATH_LEN, 0);
            
            // Check if connection was accepted (partner will close if unexpected)
            // Try a small recv with timeout to detect immediate rejection
            fd_set readfds;
            struct timeval tv;
            FD_ZERO(&readfds);
            FD_SET(sock, &readfds);
            tv.tv_sec = 0;
            tv.tv_usec = 100000; // 100ms timeout
            
            int ready = select(sock + 1, &readfds, NULL, NULL, &tv);
            if (ready > 0) {
                // Partner sent something or closed connection - likely rejected
                char test_buf[1];
                int n = recv(sock, test_buf, 1, MSG_PEEK | MSG_DONTWAIT);
                if (n == 0) {
                    // Connection closed by partner (rejected)
                    printf("SS: Partner rejected connection, will retry\n");
                    close(sock);
                    sock = -1;
                    continue;
                }
            }
            
            // Connection seems good
            printf("SS: Successfully connected to partner\n");
            break;
        }
    }
    
    if (sock < 0) {
        fprintf(stderr, "SS: Failed to connect to partner after %d attempts\n", max_retries);
        return NULL;
    }
    
    // Store socket
    pthread_mutex_lock(&g_backup_state.sync_lock);
    g_backup_state.partner_sync_sock = sock;
    int should_send = g_backup_state.should_send_full_sync;
    pthread_mutex_unlock(&g_backup_state.sync_lock);
    
    printf("SS: Connected to partner, starting sync handler\n");
    
    // Start handler for incoming sync messages
    pthread_t sync_handler_tid;
    pthread_create(&sync_handler_tid, NULL, handle_partner_sync, NULL);
    pthread_detach(sync_handler_tid);
    
    // Send full sync if NS told us to
    if (should_send) {
        printf("SS: Sending full sync to partner (instructed by NS)\n");
        sync_all_files_to_partner();
    } else {
        printf("SS: Not sending full sync (will receive from partner)\n");
    }
    
    return NULL;
}

// --- Helper: Recursively sync directory to partner ---
void sync_directory_recursive(const char* rel_path, int sock) {
    char file_dir_full[MAX_PATH_LEN * 2];
    sprintf(file_dir_full, "%s/file_dir/%s", g_config.root_dir, rel_path);
    
    DIR* dir = opendir(file_dir_full);
    if (!dir) return;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // Build relative path
        char new_rel_path[MAX_PATH_LEN * 2];
        if (strlen(rel_path) == 0) {
            snprintf(new_rel_path, sizeof(new_rel_path), "%s", entry->d_name);
        } else {
            snprintf(new_rel_path, sizeof(new_rel_path), "%s/%s", rel_path, entry->d_name);
        }
        
        // Build full path
        char entry_full[1024];
        snprintf(entry_full, sizeof(entry_full), "%s/file_dir/%s", g_config.root_dir, new_rel_path);
        
        struct stat st;
        if (stat(entry_full, &st) == -1) continue;
        
        if (S_ISDIR(st.st_mode)) {
            // Send CREATEFOLDER command
            SSSyncOpCode opcode = SS_SYNC_CREATEFOLDER;
            send(sock, &opcode, sizeof(opcode), 0);
            send(sock, new_rel_path, MAX_PATH_LEN, 0);
            
            printf("SS: Syncing folder: %s\n", new_rel_path);
            
            // Recurse into directory
            sync_directory_recursive(new_rel_path, sock);
        } else {
            // Send file
            SSSyncOpCode opcode = SS_SYNC_FILE_DATA;
            send(sock, &opcode, sizeof(opcode), 0);
            
            SS_Sync_File_Packet file_pkt;
            memset(&file_pkt, 0, sizeof(file_pkt));
            strncpy(file_pkt.path, new_rel_path, MAX_PATH_LEN - 1);
            file_pkt.file_size = st.st_size;
            
            // Read info file content
            char info_path[1024];
            snprintf(info_path, sizeof(info_path), "%s/info_dir/%s", g_config.root_dir, new_rel_path);
            FILE* info_file = fopen(info_path, "r");
            if (info_file) {
                fread(file_pkt.info_content, 1, MAX_INFO_LEN - 1, info_file);
                fclose(info_file);
            } else {
                strcpy(file_pkt.info_content, "owner: unknown\n");
            }
            
            // Send file packet
            send(sock, &file_pkt, sizeof(file_pkt), 0);
            
            // Send actual file content
            FILE* f = fopen(entry_full, "rb");
            if (f) {
                char buffer[4096];
                size_t bytes_read;
                while ((bytes_read = fread(buffer, 1, sizeof(buffer), f)) > 0) {
                    send(sock, buffer, bytes_read, 0);
                }
                fclose(f);
            }
            
            printf("SS: Synced file: %s (%ld bytes)\n", new_rel_path, st.st_size);
        }
    }
    closedir(dir);
}

// --- Sync All Files to Partner ---
void sync_all_files_to_partner() {
    printf("SS: Starting full sync to partner\n");
    
    pthread_mutex_lock(&g_backup_state.sync_lock);
    int sock = g_backup_state.partner_sync_sock;
    pthread_mutex_unlock(&g_backup_state.sync_lock);
    
    if (sock == -1) {
        printf("SS: No partner socket, skipping sync\n");
        return;
    }
    
    // Recursively sync all files starting from root
    sync_directory_recursive("", sock);
    
    // Send completion marker
    pthread_mutex_lock(&g_backup_state.sync_lock);
    if (g_backup_state.partner_sync_sock != -1) {
        SSSyncOpCode complete = SS_SYNC_COMPLETE;
        send(g_backup_state.partner_sync_sock, &complete, sizeof(complete), 0);
    }
    pthread_mutex_unlock(&g_backup_state.sync_lock);
    
    printf("SS: Full sync to partner complete\n");
}

// --- Sync Single Operation to Partner ---
void sync_operation_to_partner(SSSyncOpCode opcode, const char* path, const char* arg2) {
    pthread_mutex_lock(&g_backup_state.sync_lock);
    int sock = g_backup_state.partner_sync_sock;
    pthread_mutex_unlock(&g_backup_state.sync_lock);
    
    if (sock == -1) {
        return; // No partner connected
    }
    
    // Send opcode
    send(sock, &opcode, sizeof(opcode), 0);
    
    // Send path
    send(sock, path, MAX_PATH_LEN, 0);
    
    // Send second argument if needed
    if (arg2) {
        send(sock, arg2, MAX_PATH_LEN, 0);
    }
}

// --- Sync Single File to Partner ---
void sync_file_to_partner(const char* path) {
    pthread_mutex_lock(&g_backup_state.sync_lock);
    int sock = g_backup_state.partner_sync_sock;
    pthread_mutex_unlock(&g_backup_state.sync_lock);
    
    if (sock == -1) {
        return; // No partner connected
    }
    
    // Send FILE_DATA opcode
    SSSyncOpCode opcode = SS_SYNC_FILE_DATA;
    send(sock, &opcode, sizeof(opcode), 0);
    
    // Build file packet
    SS_Sync_File_Packet file_pkt;
    memset(&file_pkt, 0, sizeof(file_pkt));
    strncpy(file_pkt.path, path, MAX_PATH_LEN - 1);
    
    // Get file size
    char file_full_path[MAX_PATH_LEN * 3];
    snprintf(file_full_path, sizeof(file_full_path), "%s/file_dir/%s", g_config.root_dir, path);
    struct stat st;
    if (stat(file_full_path, &st) == 0) {
        file_pkt.file_size = st.st_size;
    }
    
    // Read info file
    char info_path[MAX_PATH_LEN * 3];
    snprintf(info_path, sizeof(info_path), "%s/info_dir/%s", g_config.root_dir, path);
    FILE* info_file = fopen(info_path, "r");
    if (info_file) {
        fread(file_pkt.info_content, 1, MAX_INFO_LEN - 1, info_file);
        fclose(info_file);
    } else {
        strcpy(file_pkt.info_content, "owner: unknown\n");
    }
    
    // Read undo file
    char undo_path[MAX_PATH_LEN * 3];
    snprintf(undo_path, sizeof(undo_path), "%s/undo_dir/%s", g_config.root_dir, path);
    FILE* undo_file = fopen(undo_path, "rb");
    if (undo_file) {
        file_pkt.undo_size = fread(file_pkt.undo_content, 1, MAX_COMMIT_LEN - 1, undo_file);
        fclose(undo_file);
    } else {
        file_pkt.undo_size = 0;
    }
    
    // Send file packet
    send(sock, &file_pkt, sizeof(file_pkt), 0);
    
    // Send file content
    FILE* f = fopen(file_full_path, "rb");
    if (f) {
        char buffer[4096];
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), f)) > 0) {
            send(sock, buffer, bytes_read, 0);
        }
        fclose(f);
    }
}