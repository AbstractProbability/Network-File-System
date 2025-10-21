#include "../include/ns.h"

naming_server ns;

int main(int argc __attribute__((unused)), char *argv[] __attribute__((unused))) {
    int server_fd, client_fd;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    pthread_t thread_id;
    
    // Initialize naming server
    init_naming_server();
    
    // Create server socket
    int port = server_socket_init(&server_fd);
    if (port < 0) {
        log_message("Failed to initialize server socket");
        return 1;
    }
    
    // Start listening
    if (listen(server_fd, 10) < 0) {
        log_message("Listen failed");
        perror("listen");
        return 1;
    }
    
    printf("Naming Server started on port %d\n", port);
    log_message("Naming Server started on port %d", port);
    
    // Accept connections
    while (1) {
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            log_message("Accept failed");
            continue;
        }
        
        char client_ip[MAX_IP];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, MAX_IP);
        log_message("New connection from %s:%d", client_ip, ntohs(client_addr.sin_port));
        
        // Create thread to handle connection
        int *pclient_fd = malloc(sizeof(int));
        *pclient_fd = client_fd;
        if (pthread_create(&thread_id, NULL, handle_connection, pclient_fd) != 0) {
            log_message("Failed to create thread");
            close(client_fd);
            free(pclient_fd);
        } else {
            pthread_detach(thread_id);
        }
    }
    
    close(server_fd);
    fclose(ns.log_file);
    pthread_mutex_destroy(&ns.lock);
    
    return 0;
}
