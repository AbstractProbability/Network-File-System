#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../common.h"

#define BUFFER_SIZE 4096

void print_menu() {
    printf("\n=== Naming Server Test Client ===\n");
    printf("Commands:\n");
    printf("  LIST                     - List all users\n");
    printf("  VIEW                     - List accessible files\n");
    printf("  VIEW -a                  - List all files\n");
    printf("  VIEW -l                  - List files with details\n");
    printf("  INFO <filename>          - Show file info\n");
    printf("  READ <filename>          - Get SS info for reading\n");
    printf("  WRITE <filename> <sent#> - Get SS info for writing\n");
    printf("  STREAM <filename>        - Get SS info for streaming\n");
    printf("  CREATE <filename>        - Create a new file\n");
    printf("  DELETE <filename>        - Delete a file\n");
    printf("  ADDACCESS -R <file> <user> - Grant read access\n");
    printf("  ADDACCESS -W <file> <user> - Grant write access\n");
    printf("  REMACCESS <file> <user>  - Remove access\n");
    printf("  HELP                     - Show this menu\n");
    printf("  QUIT                     - Exit\n");
    printf("================================\n\n");
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Usage: %s <ns_ip> <ns_port> <username>\n", argv[0]);
        return 1;
    }
    
    const char *ns_ip = argv[1];
    int ns_port = atoi(argv[2]);
    const char *username = argv[3];
    
    int sock_fd;
    char buffer[BUFFER_SIZE];
    
    // Connect to naming server
    printf("Connecting to Naming Server at %s:%d...\n", ns_ip, ns_port);
    int client_port = client_socket_init(&sock_fd, ns_ip, ns_port);
    if (client_port < 0) {
        printf("Failed to connect to Naming Server\n");
        return 1;
    }
    
    printf("Connected! Client port: %d\n", client_port);
    
    // Register as client
    snprintf(buffer, BUFFER_SIZE, "REGISTER_CLIENT %s", username);
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
    
    printf("Successfully registered as: %s\n", username);
    print_menu();
    
    // Command loop
    while (1) {
        printf("%s> ", username);
        fflush(stdout);
        
        if (fgets(buffer, BUFFER_SIZE, stdin) == NULL) {
            break;
        }
        
        // Remove newline
        buffer[strcspn(buffer, "\n")] = 0;
        
        // Check for quit
        if (strcasecmp(buffer, "QUIT") == 0 || strcasecmp(buffer, "EXIT") == 0) {
            break;
        }
        
        // Check for help
        if (strcasecmp(buffer, "HELP") == 0) {
            print_menu();
            continue;
        }
        
        // Send command to NS
        if (send(sock_fd, buffer, strlen(buffer), 0) < 0) {
            perror("Send failed");
            break;
        }
        
        // Receive response
        memset(buffer, 0, BUFFER_SIZE);
        bytes = recv(sock_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) {
            printf("Connection lost\n");
            break;
        }
        
        buffer[bytes] = '\0';
        printf("\n%s\n", buffer);
    }
    
    printf("Disconnecting...\n");
    close(sock_fd);
    
    return 0;
}
