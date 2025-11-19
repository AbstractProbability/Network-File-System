#include "ss.h"
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>

// --- Externs ---
extern SS_Config g_config;

// These are defined in ss.c and used for startup synchronization
extern pthread_mutex_t port_init_lock;
extern pthread_cond_t port_init_cond;
extern int port_is_ready;


// --- Client-Facing Thread (Type-3) ---
void* client_listener_thread(void* arg) {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Client listener socket failed");
        pthread_exit(NULL);
    }
    
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(0); // Bind to port 0

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Client listener bind failed");
        pthread_exit(NULL);
    }

    // --- Get the dynamically assigned port ---
    if (getsockname(server_fd, (struct sockaddr *)&address, &addrlen) < 0) {
        perror("getsockname failed");
        pthread_exit(NULL);
    }
    
    g_config.client_port = ntohs(address.sin_port); // Store the port
    
    // --- Signal the main thread that the port is ready ---
    pthread_mutex_lock(&port_init_lock);
    port_is_ready = 1;
    pthread_cond_signal(&port_init_cond);
    pthread_mutex_unlock(&port_init_lock);

    if (listen(server_fd, 10) < 0) {
        perror("Client listener listen failed");
        pthread_exit(NULL);
    }

    printf("SS: Client listener started on port %d\n", g_config.client_port);

    while (1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Client accept failed");
            continue;
        }

        pthread_t client_thread;
        int* new_sock_ptr = malloc(sizeof(int));
        *new_sock_ptr = new_socket;
        
        if (pthread_create(&client_thread, NULL, handle_client_connection, (void*)new_sock_ptr) < 0) {
            perror("SS: could not create client thread");
            close(new_socket);
            free(new_sock_ptr);
        }
        pthread_detach(client_thread);
    }
}

// Handles a single client for a READ, WRITE, or STREAM operation
void* handle_client_connection(void* arg) {
    int sock = *(int*)arg;
    free(arg);
    
    ClientRequest req;
    
    if (recv(sock, &req, sizeof(ClientRequest), 0) <= 0) {
        printf("SS: Client disconnected before sending request.\n");
        close(sock);
        return NULL;
    }

    printf("SS: Client connected for OP %d on %s\n", req.op, req.path);

    switch (req.op) {
        case OP_READ:
            handle_op_read(sock, &req);
            break;
        case OP_WRITE:
            handle_op_write(sock, &req); // This function now handles the full session
            break;
        case OP_STREAM:
            handle_op_stream(sock, &req);
            break;
        case OP_VIEWCHECKPOINT: {
            // Read checkpoint file and send to client
            char checkpoint_path[MAX_PATH_LEN * 4];
            char checkpoint_dir[MAX_PATH_LEN * 2];
            get_full_path("checkpoint_dir", "", checkpoint_dir);
            snprintf(checkpoint_path, sizeof(checkpoint_path), "%s/%s.%s", 
                     checkpoint_dir, req.path, req.arg1);
            
            FILE* f = fopen(checkpoint_path, "r");
            if (f == NULL) {
                const char* err = "Error: Checkpoint not found.\nSTOP";
                send(sock, err, strlen(err) + 1, 0);
                break;
            }
            
            char buffer[MAX_BUFFER_LEN];
            while (fgets(buffer, MAX_BUFFER_LEN, f)) {
                send(sock, buffer, strlen(buffer), 0);
            }
            fclose(f);
            send(sock, "STOP", 5, 0);
            break;
        }
        case OP_LISTCHECKPOINTS:
            handle_op_listcheckpoints(sock, req.path);
            break;
        // --- NEW: Handle replication request from another SS ---
        case NS_SS_REPLICATE_FILE: {
            printf("SS: Receiving replication for '%s'\n", req.path);
            char file_path[MAX_PATH_LEN * 2];
            get_full_path("file_dir", req.path, file_path);
            
            FILE* f = fopen(file_path, "w");
            if (f == NULL) {
                perror("Replication: failed to open file for writing");
                close(sock);
                return NULL;
            }
            
            char buffer[MAX_BUFFER_LEN];
            int bytes_read;
            while ((bytes_read = recv(sock, buffer, MAX_BUFFER_LEN, 0)) > 0) {
                fwrite(buffer, 1, bytes_read, f);
            }
            fclose(f);
            printf("SS: Replication for '%s' complete.\n", req.path);
            break;
        }
        // --- END NEW ---
        default:
            printf("SS: Client sent invalid Type-3 operation %d\n", req.op);
    }
    
    printf("SS: Client operation complete on socket %d.\n", sock);
    close(sock);
    return NULL;
}



// --- NS-Facing Thread (Type-2) ---
void* handle_ns_commands(void* arg) {
    NSOpCode op;
    ServerResponse ack;
    char path_buf[MAX_PATH_LEN];
    char owner_buf[MAX_USERNAME_LEN];

    while (recv(g_config.cmd_sock, &op, sizeof(NSOpCode), 0) > 0) {
        memset(&ack, 0, sizeof(ServerResponse));
        
        switch (op) {
            case NS_SS_HEARTBEAT:
                printf("SS: Received heartbeat from NS.\n");
                // Send back SS_NS_HEARTBEAT opcode on update socket
                SSOpCode hb_response = SS_NS_HEARTBEAT;
                send(g_config.update_sock, &hb_response, sizeof(SSOpCode), 0);
                break;
            
            case NS_SS_PAIR_AND_SYNC: {
                // Receive partner info
                char partner_name[MAX_PATH_LEN];
                char partner_ip[INET_ADDRSTRLEN];
                int partner_port;
                int should_send_sync;
                
                recv(g_config.cmd_sock, partner_name, MAX_PATH_LEN, 0);
                recv(g_config.cmd_sock, partner_ip, INET_ADDRSTRLEN, 0);
                recv(g_config.cmd_sock, &partner_port, sizeof(int), 0);
                recv(g_config.cmd_sock, &should_send_sync, sizeof(int), 0);
                
                printf("SS: Received pair command - partner '%s' at %s:%d, should_send_sync=%d\n", 
                       partner_name, partner_ip, partner_port, should_send_sync);
                
                // Store partner info
                pthread_mutex_lock(&g_backup_state.sync_lock);
                strcpy(g_backup_state.partner_name, partner_name);
                strcpy(g_backup_state.partner_ip, partner_ip);
                g_backup_state.partner_backup_port = partner_port;
                g_backup_state.should_send_full_sync = should_send_sync;
                pthread_mutex_unlock(&g_backup_state.sync_lock);
                
                // Only initiate connection if our name is lexicographically smaller
                // This prevents both SSes from trying to connect simultaneously
                if (strcmp(g_backup_state.name, partner_name) < 0) {
                    printf("SS: I am initiator (name '%s' < '%s'), connecting to partner\n",
                           g_backup_state.name, partner_name);
                    pthread_t sync_tid;
                    pthread_create(&sync_tid, NULL, connect_and_sync_to_partner, NULL);
                    pthread_detach(sync_tid);
                } else {
                    printf("SS: I am receiver (name '%s' > '%s'), waiting for partner to connect\n",
                           g_backup_state.name, partner_name);
                }
                break;
            }
            
            case NS_SS_PARTNER_DISCONNECTED:
                printf("SS: Partner disconnected notification from NS\n");
                pthread_mutex_lock(&g_backup_state.sync_lock);
                if (g_backup_state.partner_sync_sock >= 0) {
                    close(g_backup_state.partner_sync_sock);
                    g_backup_state.partner_sync_sock = -1;
                }
                // Keep partner_name intact - we may reconnect later
                pthread_mutex_unlock(&g_backup_state.sync_lock);
                break;
                
            case NS_SS_CREATE:
                recv(g_config.cmd_sock, path_buf, MAX_PATH_LEN, 0);
                recv(g_config.cmd_sock, owner_buf, MAX_USERNAME_LEN, 0);
                printf("SS: Received CREATE for %s by %s\n", path_buf, owner_buf);
                
                ack.status = handle_op_create(path_buf, owner_buf);
                send(g_config.cmd_sock, &ack, sizeof(ServerResponse), 0);
                break;

            case NS_SS_DELETE:
                recv(g_config.cmd_sock, path_buf, MAX_PATH_LEN, 0);
                printf("SS: Received DELETE for %s\n", path_buf);

                // --- THIS IS THE FIX ---
                ack.status = handle_op_delete(path_buf);
                // --- END OF FIX ---
                
                send(g_config.cmd_sock, &ack, sizeof(ServerResponse), 0);
                break;
                
            case NS_SS_UPDATE_INFO:
                recv(g_config.cmd_sock, path_buf, MAX_PATH_LEN, 0);
                char info_content[MAX_INFO_LEN];
                recv(g_config.cmd_sock, info_content, MAX_INFO_LEN, 0);
                printf("SS: Received UPDATE_INFO for %s\n", path_buf);
                
                char info_path[MAX_PATH_LEN * 2];
                get_full_path("info_dir", path_buf, info_path);
                
                FILE* info_file = fopen(info_path, "w");
                if (info_file != NULL) {
                    fprintf(info_file, "%s", info_content);
                    fclose(info_file);
                    ack.status = ERR_OK;
                    printf("SS: Info file updated for '%s'\n", path_buf);
                } else {
                    ack.status = ERR_INVALID_PATH;
                    perror("SS: Failed to update info file");
                }
                
                send(g_config.cmd_sock, &ack, sizeof(ServerResponse), 0);
                break;
                
            case NS_SS_GET_INFO:
                recv(g_config.cmd_sock, path_buf, MAX_PATH_LEN, 0);
                printf("SS: Received GET_INFO for %s\n", path_buf);
                
                char info_path_get[MAX_PATH_LEN * 2];
                get_full_path("info_dir", path_buf, info_path_get);
                
                FILE* info_file_get = fopen(info_path_get, "r");
                char info_content_get[MAX_INFO_LEN];
                memset(info_content_get, 0, sizeof(info_content_get));
                
                if (info_file_get != NULL) {
                    fread(info_content_get, 1, MAX_INFO_LEN - 1, info_file_get);
                    fclose(info_file_get);
                    ack.status = ERR_OK;
                    send(g_config.cmd_sock, &ack, sizeof(ServerResponse), 0);
                    send(g_config.cmd_sock, info_content_get, MAX_INFO_LEN, 0);
                    printf("SS: Sent info for '%s' to NS\n", path_buf);
                } else {
                    ack.status = ERR_FILE_NOT_FOUND;
                    send(g_config.cmd_sock, &ack, sizeof(ServerResponse), 0);
                    perror("SS: Failed to read info file");
                }
                break;
                
            case NS_SS_UNDO:
                recv(g_config.cmd_sock, path_buf, MAX_PATH_LEN, 0);
                recv(g_config.cmd_sock, owner_buf, MAX_USERNAME_LEN, 0);
                printf("SS: Received UNDO for %s by %s\n", path_buf, owner_buf);
                
                ack.status = handle_op_undo(path_buf, owner_buf);
                send(g_config.cmd_sock, &ack, sizeof(ServerResponse), 0);
                break;
            
            case NS_SS_CHECKPOINT: {
                char checkpoint_tag[MAX_PATH_LEN];
                recv(g_config.cmd_sock, path_buf, MAX_PATH_LEN, 0);
                recv(g_config.cmd_sock, checkpoint_tag, MAX_PATH_LEN, 0);
                printf("SS: Received CHECKPOINT for %s with tag '%s'\n", path_buf, checkpoint_tag);
                
                ack.status = handle_op_checkpoint(path_buf, checkpoint_tag);
                if (ack.status == ERR_OK) {
                    sprintf(ack.message, "Checkpoint '%s' created successfully.", checkpoint_tag);
                } else {
                    sprintf(ack.message, "Failed to create checkpoint.");
                }
                send(g_config.cmd_sock, &ack, sizeof(ServerResponse), 0);
                break;
            }
            
            case NS_SS_REVERT: {
                char checkpoint_tag[MAX_PATH_LEN];
                recv(g_config.cmd_sock, path_buf, MAX_PATH_LEN, 0);
                recv(g_config.cmd_sock, checkpoint_tag, MAX_PATH_LEN, 0);
                recv(g_config.cmd_sock, owner_buf, MAX_USERNAME_LEN, 0);
                printf("SS: Received REVERT for %s to checkpoint '%s' by %s\n", 
                       path_buf, checkpoint_tag, owner_buf);
                
                ack.status = handle_op_revert(path_buf, checkpoint_tag, owner_buf);
                if (ack.status == ERR_OK) {
                    sprintf(ack.message, "Reverted to checkpoint '%s' successfully.", checkpoint_tag);
                } else if (ack.status == ERR_FILE_NOT_FOUND) {
                    sprintf(ack.message, "Checkpoint '%s' not found.", checkpoint_tag);
                } else if (ack.status == ERR_SENTENCE_LOCKED) {
                    sprintf(ack.message, "File has active write session. Cannot revert.");
                } else {
                    sprintf(ack.message, "Failed to revert to checkpoint.");
                }
                send(g_config.cmd_sock, &ack, sizeof(ServerResponse), 0);
                break;
            }
            
            case NS_SS_CREATEFOLDER: {
                recv(g_config.cmd_sock, path_buf, MAX_PATH_LEN, 0);
                printf("SS: Received CREATEFOLDER for '%s'\n", path_buf);
                
                handle_op_createfolder(path_buf, g_config.cmd_sock);
                break;
            }
            
            case NS_SS_MOVE: {
                char new_path[MAX_PATH_LEN];
                recv(g_config.cmd_sock, path_buf, MAX_PATH_LEN, 0);
                recv(g_config.cmd_sock, new_path, MAX_PATH_LEN, 0);
                printf("SS: Received MOVE from '%s' to '%s'\n", path_buf, new_path);
                
                handle_op_move(path_buf, new_path, g_config.cmd_sock);
                break;
            }
                
            default:
                printf("SS: Received unknown command from NS: %d\n", op);
        }
    }

    printf("SS: Connection to Name Server lost.\n");
    return NULL;
}