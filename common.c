#include "./common.h"

int server_socket_init(int *p_socket_fd) {
    // 1. Create the socket (Common step)
    *p_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (*p_socket_fd < 0) {
        perror("Server socket creation failed");
        return -1;
    }

    // 2. Bind to any IP and a dynamic port (Server-specific)
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(0); // Port 0 for dynamic assignment

    if (bind(*p_socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Server bind failed");
        close(*p_socket_fd);
        return -1;
    }

    // 3. Get the assigned port (Server-specific)
    struct sockaddr_in bound_addr;
    socklen_t len = sizeof(bound_addr);
    getsockname(*p_socket_fd, (struct sockaddr *)&bound_addr, &len);
    
    return ntohs(bound_addr.sin_port); // Return the assigned port
}

// --- CLIENT ---
int client_socket_init(int *p_socket_fd, const char* server_ip, int server_port) {
    // 1. Create the socket
    *p_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (*p_socket_fd < 0) {
        perror("Client socket creation failed");
        return -1;
    }

    // 2. Prepare the server's address for connection
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid server IP address");
        close(*p_socket_fd);
        return -1;
    }

    // 3. Connect to the server. This implicitly binds the client socket to an ephemeral port.
    if (connect(*p_socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Client connection failed");
        close(*p_socket_fd);
        return -1;
    }

    // 4. Get the local address and port assigned to our client socket
    struct sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr);
    if (getsockname(*p_socket_fd, (struct sockaddr *)&client_addr, &len) == -1) {
        perror("getsockname failed");
        close(*p_socket_fd);
        return -1;
    }
    
    // 5. Return the assigned port, converted to a readable integer
    return ntohs(client_addr.sin_port);
}