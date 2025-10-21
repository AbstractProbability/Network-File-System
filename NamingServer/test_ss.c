#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../common.h"

#define BUFFER_SIZE 4096

int main(int argc, char *argv[]) {
    if (argc != 6) {
        printf("Usage: %s <ns_ip> <ns_port> <my_ip> <nm_port> <client_port>\n", argv[0]);
        printf("Example: %s 127.0.0.1 5000 127.0.0.1 6000 6001\n", argv[0]);
        return 1;
    }
    
    const char *ns_ip = argv[1];
    int ns_port = atoi(argv[2]);
    const char *my_ip = argv[3];
    int nm_port = atoi(argv[4]);
    int client_port = atoi(argv[5]);
    
    int sock_fd;
    char buffer[BUFFER_SIZE];
    
    // Connect to naming server
    printf("Connecting to Naming Server at %s:%d...\n", ns_ip, ns_port);
    int local_port = client_socket_init(&sock_fd, ns_ip, ns_port);
    if (local_port < 0) {
        printf("Failed to connect to Naming Server\n");
        return 1;
    }
    
    printf("Connected! Local port: %d\n", local_port);
    
    // Register as storage server
    // Format: REGISTER_SS <ip> <nm_port> <client_port> <num_files>
    int num_files = 3; // Example: 3 files
    snprintf(buffer, BUFFER_SIZE, "REGISTER_SS %s %d %d %d", 
             my_ip, nm_port, client_port, num_files);
    
    printf("Registering SS: %s\n", buffer);
    if (send(sock_fd, buffer, strlen(buffer), 0) < 0) {
        perror("Failed to register");
        close(sock_fd);
        return 1;
    }
    
    // Wait for registration response
    memset(buffer, 0, BUFFER_SIZE);
    int bytes = recv(sock_fd, buffer, BUFFER_SIZE - 1, 0);
    if (bytes <= 0) {
        printf("Registration failed\n");
        close(sock_fd);
        return 1;
    }
    
    buffer[bytes] = '\0';
    if (strncmp(buffer, "OK", 2) != 0) {
        printf("Registration error: %s\n", buffer);
        close(sock_fd);
        return 1;
    }
    
    printf("Storage Server registration acknowledged\n");
    
    // Send file list
    const char *files[][3] = {
        {"file1.txt", "alice", "1024"},
        {"file2.txt", "bob", "2048"},
        {"docs/file3.txt", "alice", "512"}
    };
    
    for (int i = 0; i < num_files; i++) {
        snprintf(buffer, BUFFER_SIZE, "FILE %s %s %s", 
                 files[i][0], files[i][1], files[i][2]);
        
        printf("Sending file %d: %s\n", i + 1, buffer);
        if (send(sock_fd, buffer, strlen(buffer), 0) < 0) {
            perror("Failed to send file info");
            close(sock_fd);
            return 1;
        }
        
        // Wait for ack
        memset(buffer, 0, BUFFER_SIZE);
        bytes = recv(sock_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) {
            printf("Failed to receive ack for file %d\n", i + 1);
            close(sock_fd);
            return 1;
        }
        
        buffer[bytes] = '\0';
        if (strncmp(buffer, "OK", 2) != 0) {
            printf("File %d error: %s\n", i + 1, buffer);
        } else {
            printf("File %d registered successfully\n", i + 1);
        }
    }
    
    printf("\nStorage Server registration complete!\n");
    printf("Press Enter to disconnect...");
    getchar();
    
    close(sock_fd);
    return 0;
}
