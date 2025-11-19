#include "../include/common.h"
#include "../include/serialize.h"
#include "ns_data.h"
#include <sys/stat.h>

// Global Name Server state
NameServer ns; 

// --- Forward Declarations ---
void* handle_connection(void* socket_desc);
void* handle_client_session(void* arg);
void* handle_ss_cmd_session(void* arg);
void* handle_ss_update_session(void* arg);
void* heartbeat_thread(void* arg);

// Backup helper functions
SSInfo* find_unbacked_ss();
void pair_storage_servers(SSInfo* empty_ss, SSInfo* regular_ss);
void send_pair_command(SSInfo* ss, const char* partner_name, const char* partner_ip, int partner_port, int should_send_sync);

// --- Backup Helper Implementations ---

// Find a storage server that needs backup (regular SS, not backed up)
SSInfo* find_unbacked_ss(SSInfo* exclude) {
    pthread_mutex_lock(&ns.ss_list_lock);
    SSInfo* current = ns.ss_list_head;
    while (current != NULL) {
        // Find any active SS that needs backing, including survivors whose partner went down
        // Exclude the currently registering SS to avoid pairing with itself
        if (current != exclude && current->is_active && !current->backed_up) {
            pthread_mutex_unlock(&ns.ss_list_lock);
            return current;
        }
        current = current->next;
    }
    pthread_mutex_unlock(&ns.ss_list_lock);
    return NULL;
}

// Set up bidirectional partnership between two storage servers
void pair_storage_servers(SSInfo* empty_ss, SSInfo* regular_ss) {
    // Mark both as backed up
    empty_ss->backed_up = 1;
    regular_ss->backed_up = 1;
    
    // Set partner names
    strcpy(empty_ss->partner_name, regular_ss->name);
    strcpy(regular_ss->partner_name, empty_ss->name);
    
    printf("NS: Paired SS '%s' with SS '%s' for backup\n", regular_ss->name, empty_ss->name);
}

// Send pairing command to a storage server and wait for acknowledgment
void send_pair_command(SSInfo* ss, const char* partner_name, const char* partner_ip, int partner_port, int should_send_sync) {
    pthread_mutex_lock(&ss->cmd_lock);
    
    // Send opcode
    NSOpCode opcode = NS_SS_PAIR_AND_SYNC;
    SEND_OPCODE(ss->cmd_sock, opcode);
    
    // Send partner info
    send_full(ss->cmd_sock, partner_name, MAX_PATH_LEN);
    send_full(ss->cmd_sock, partner_ip, INET_ADDRSTRLEN);
    SEND_INT(ss->cmd_sock, partner_port);
    SEND_INT(ss->cmd_sock, should_send_sync);
    
    // Wait for acknowledgment to ensure SS has processed the pair command
    ServerResponse ack;
    RECV_SERVER_RESPONSE(ss->cmd_sock, &ack);
    
    pthread_mutex_unlock(&ss->cmd_lock);
    
    printf("NS: Sent pair command to SS '%s' -> partner '%s' (%s:%d), should_send_sync=%d (ack: %s)\n",
           ss->name, partner_name, partner_ip, partner_port, should_send_sync,
           ack.status == ERR_OK ? "OK" : "FAILED");
}

// Helper to initialize the NS state
void ns_init(NameServer* ns) {
    ns->current_clients = (ClientList*)calloc(1, sizeof(ClientList));
    ns->all_clients = (ClientList*)calloc(1, sizeof(ClientList));
    ns->file_index = (FileIndex*)calloc(1, sizeof(FileIndex));
    ns->lru_cache = lru_cache_init();  // Initialize LRU cache
    ns->access_requests = (AccessRequestList*)calloc(1, sizeof(AccessRequestList));
    ns->ss_list_head = NULL;
    
    pthread_mutex_init(&ns->current_clients->lock, NULL);
    pthread_mutex_init(&ns->all_clients->lock, NULL);
    pthread_mutex_init(&ns->file_index->lock, NULL);
    pthread_mutex_init(&ns->access_requests->lock, NULL);
    pthread_mutex_init(&ns->ss_list_lock, NULL);
    
    ns->access_requests->next_id = 1;
    
    // Create nameserver_data directory if it doesn't exist
    mkdir("nameserver_data", 0755);
    
    // Load persistent data
    ns_load_all_users();
    ns_load_access_requests();
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    ns_init(&ns);
    printf("Name Server initializing...\n");

    // --- Create server socket ---
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(NS_LISTEN_PORT);

    // --- Bind ---
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // --- Listen ---
    if (listen(server_fd, 10) < 0) { // Listen for up to 10 pending
        perror("listen");
        exit(EXIT_FAILURE);
    }

    // --- Start Heartbeat Thread ---
    pthread_t hb_thread_id;
    if (pthread_create(&hb_thread_id, NULL, heartbeat_thread, NULL) != 0) {
        perror("Failed to create heartbeat thread");
        exit(EXIT_FAILURE);
    }
    pthread_detach(hb_thread_id);

    printf("Name Server listening on port %d\n", NS_LISTEN_PORT);

    // --- Accept connections loop ---
    while (1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            continue; 
        }

        pthread_t handler_thread;
        int* new_sock_ptr = malloc(sizeof(int));
        *new_sock_ptr = new_socket;

        if (pthread_create(&handler_thread, NULL, handle_connection, (void*)new_sock_ptr) < 0) {
            perror("could not create thread");
            close(new_socket);
            free(new_sock_ptr);
        }
        
        pthread_detach(handler_thread); 
    }

    return 0; // Unreachable
}

// --- Initial Connection Triage ---
void* handle_connection(void* socket_desc) {
    int sock = *(int*)socket_desc;
    free(socket_desc);
    
    InitialPacket init_pkt;
    
    // Read the first packet to identify connection type
    if (RECV_INITIAL_PACKET(sock, &init_pkt) <= 0) {
        printf("Connection closed before identification\n");
        close(sock);
        return NULL;
    }

    if (init_pkt.type == CONN_CLIENT) {
        // --- This is a Client ---
        ClientInfo* client = (ClientInfo*)malloc(sizeof(ClientInfo));
        strcpy(client->username, init_pkt.username);
        client->sock = sock;
        handle_client_session((void*)client);
    } 
    else if (init_pkt.type == CONN_SS) {
        // --- This is a Storage Server ---
        // Peek at the info packet to identify which SS (without consuming it)
        SS_Info_Packet info_pkt;
        char info_buffer[SERIALIZED_SS_INFO_PACKET_SIZE];
        int peek_ret = recv(sock, info_buffer, SERIALIZED_SS_INFO_PACKET_SIZE, MSG_PEEK);
        if (peek_ret <= 0) {
            printf("SS disconnected before sending info.\n");
            close(sock);
            return NULL;
        }
        deserialize_ss_info_packet(info_buffer, &info_pkt);
        
        // Get SS's actual IP from the socket peer (more reliable than SS self-reporting)
        char ss_ip[INET_ADDRSTRLEN];
        struct sockaddr_in peer_addr;
        socklen_t peer_len = sizeof(peer_addr);
        if (getpeername(sock, (struct sockaddr*)&peer_addr, &peer_len) == 0) {
            inet_ntop(AF_INET, &peer_addr.sin_addr, ss_ip, INET_ADDRSTRLEN);
        } else {
            // Fallback to SS-reported IP if getpeername fails
            strcpy(ss_ip, info_pkt.ip);
        }
        
        // Search for an existing SSInfo with matching IP and client_port
        // (to distinguish first vs second socket from same SS)
        pthread_mutex_lock(&ns.ss_list_lock);
        
        SSInfo* found = NULL;
        SSInfo* current = ns.ss_list_head;
        while (current != NULL) {
            if (strcmp(current->ip, ss_ip) == 0 && 
                current->client_port == info_pkt.port_for_clients &&
                current->update_sock == -1) {
                found = current;
                break;
            }
            current = current->next;
        }
        
        if (found == NULL) {
            // This is the first socket (command socket) from this SS
            SSInfo* ss = (SSInfo*)malloc(sizeof(SSInfo));
            ss->cmd_sock = sock;
            ss->update_sock = -1;  // Not yet connected
            strcpy(ss->ip, ss_ip);  // Use detected IP, not SS-reported
            ss->client_port = info_pkt.port_for_clients;
            ss->is_active = 0;  // Will be set to 1 after sync completes
            ss->last_heartbeat = time(NULL);
            ss->backed_up = 0;
            ss->partner_name[0] = '\0';
            ss->active_writers = 0;
            pthread_mutex_init(&ss->cmd_lock, NULL);
            pthread_mutex_init(&ss->writers_lock, NULL);
            
            // Add to list immediately so second socket can find it
            ss->next = ns.ss_list_head;
            ns.ss_list_head = ss;
            
            pthread_mutex_unlock(&ns.ss_list_lock);
            
            // Start command session handler (will read info_pkt properly)
            pthread_t cmd_thread;
            pthread_create(&cmd_thread, NULL, handle_ss_cmd_session, (void*)ss);
            pthread_detach(cmd_thread);
        } else {
            // This is the second socket (update socket) from the same SS
            found->update_sock = sock;
            pthread_mutex_unlock(&ns.ss_list_lock);
            
            // Start update session handler
            pthread_t update_thread;
            pthread_create(&update_thread, NULL, handle_ss_update_session, (void*)found);
            pthread_detach(update_thread);
        }
    }
    else {
        printf("Unknown connection type. Closing.\n");
        close(sock);
    }

    return NULL;
}

// --- Heartbeat Thread ---
void* heartbeat_thread(void* arg) {
    NSOpCode heartbeat_op = NS_SS_HEARTBEAT;
    
    while (1) {
        sleep(HEARTBEAT_INTERVAL);
        
        pthread_mutex_lock(&ns.ss_list_lock);
        SSInfo* current = ns.ss_list_head;
        while (current != NULL) {
            if (current->is_active) {
                // Lock the command socket before sending heartbeat
                pthread_mutex_lock(&current->cmd_lock);
                if (SEND_OPCODE(current->cmd_sock, heartbeat_op) <= 0) {
                    printf("Heartbeat failed (send): SS at %s is down.\n", current->ip);
                    current->is_active = 0;
                    pthread_mutex_unlock(&current->cmd_lock);
                    
                    // Note: update_session will handle cleanup and removal
                    // We just mark as inactive here to stop further heartbeats
                }
                else {
                    pthread_mutex_unlock(&current->cmd_lock);
                }
            }
            current = current->next;
        }
        pthread_mutex_unlock(&ns.ss_list_lock);
    }
    return NULL;
}