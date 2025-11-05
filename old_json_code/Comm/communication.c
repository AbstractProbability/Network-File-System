#include "communication.h"

// Server Socket Functions

// Creates a TCP server socket, binds it to the port, and starts listening
int create_server_socket(int port) {
    int server_fd;
    struct sockaddr_in address;
    int opt = 1; // Enables SO_REUSEADDR to quickly rebind after restart

    // Create a TCP socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        return -1;
    }

    // Allow reusing the port even if it's in TIME_WAIT state
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt failed");
        close(server_fd);
        return -1;
    }

    // Configure to listen on all network interfaces on the specified port
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port); // Convert port to network byte order

    // Bind the socket to the address and port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        return -1;
    }

    // Start listening with a backlog queue of 10 pending connections
    if (listen(server_fd, 10) < 0) {
        perror("listen failed");
        close(server_fd);
        return -1;
    }

    return server_fd;
}

// Waits for and accepts an incoming connection, returning a Connection struct with client details
Connection* accept_client(int server_fd) {
    int new_socket;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    // Block until a client connects (retries if interrupted by signal)
    while ((new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen)) < 0) {
        if (errno == EINTR) continue; // Retry on signal interruption
        perror("accept failed");
        return NULL;
    }

    // Allocate and populate a Connection structure for this client
    Connection *conn = (Connection *)malloc(sizeof(Connection));
    if (!conn) {
        perror("malloc failed");
        close(new_socket);
        return NULL;
    }

    // Extract and store client information
    conn->socket_fd = new_socket;
    inet_ntop(AF_INET, &address.sin_addr, conn->client_ip, INET_ADDRSTRLEN); // Convert IP to string
    conn->client_port = ntohs(address.sin_port); // Convert port to host byte order
    conn->type[0] = '\0';
    conn->identifier[0] = '\0';
    conn->connected_at = time(NULL);

    printf("[DEBUG] New connection: fd=%d, ip=%s, port=%d\n", 
           new_socket, conn->client_ip, conn->client_port);

    return conn;
}

void close_server_socket(int server_fd) {
    close(server_fd);
    printf("[DEBUG] Server socket closed\n");
}

// CLIENT SOCKET FUNCTIONS

// Creates a socket and connects to a remote server at the specified IP and port
int create_client_socket(const char *server_ip, int server_port) {
    int sock;
    struct sockaddr_in serv_addr;

    // Create a TCP socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket creation failed");
        return -1;
    }

    // Configure the server's address
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(server_port);

    // Convert IP string (like "127.0.0.1") to binary format
    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        perror("invalid address");
        close(sock);
        return -1;
    }

    // Initiate connection to the server (blocks until connected or fails)
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connection failed");
        close(sock);
        return -1;
    }

    printf("[DEBUG] Connected to %s:%d\n", server_ip, server_port);
    return sock;
}

void close_client_socket(int client_fd) {
    close(client_fd);
    printf("[DEBUG] Client socket closed\n");
}

// MESSAGE FUNCTIONS

// Sends a complete message over the socket, retrying until all bytes are sent
int send_message(int socket_fd, const char *message) {
    size_t len = strlen(message);
    size_t sent = 0;

    // Loop to handle partial sends
    while (sent < len) {
        ssize_t n = send(socket_fd, message + sent, len - sent, 0);
        if (n < 0) {
            perror("send failed");
            return -1;
        }
        if (n == 0) {
            printf("[ERROR] Connection closed during send\n");
            return -1;
        }
        sent += n;
    }

    return 0;
}

// Receives data from socket and returns it as a newly allocated string (caller must free)
char* receive_message(int socket_fd) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    // Read data from the socket
    bytes_received = recv(socket_fd, buffer, BUFFER_SIZE - 1, 0);
    
    if (bytes_received < 0) {
        perror("recv failed");
        return NULL;
    }
    
    if (bytes_received == 0) {
        printf("[DEBUG] Connection closed by peer\n");
        return NULL;
    }

    buffer[bytes_received] = '\0';

    // Copy to heap memory so it persists after function returns
    char *message = (char *)malloc(bytes_received + 1);
    if (!message) {
        perror("malloc failed");
        return NULL;
    }

    strcpy(message, buffer);
    return message;
}

// JSON HELPER FUNCTIONS

// Parses a JSON string and returns a cJSON object, or NULL on parse error
cJSON* parse_message(const char *json_string) {
    cJSON *json = cJSON_Parse(json_string);
    if (!json) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr) {
            fprintf(stderr, "[ERROR] JSON parse error: %s\n", error_ptr);
        }
        return NULL;
    }
    return json;
}

// Extracts a string value from a JSON object by field name
char* get_string_field(cJSON *json, const char *field_name) {
    if (!json) return NULL;
    
    cJSON *field = cJSON_GetObjectItem(json, field_name);
    if (!field) return NULL;
    
    if (!cJSON_IsString(field)) return NULL;
    
    return field->valuestring;
}

// Extracts an integer value from a JSON object by field name
int get_int_field(cJSON *json, const char *field_name) {
    if (!json) return -1;
    
    cJSON *field = cJSON_GetObjectItem(json, field_name);
    if (!field) return -1;
    
    if (!cJSON_IsNumber(field)) return -1;
    
    return field->valueint;
}

// Creates a JSON response with status, message, and error_code fields (caller must free)
char* create_response(const char *status, const char *message, int error_code) {
    cJSON *json = cJSON_CreateObject();
    
    cJSON_AddStringToObject(json, "status", status);
    cJSON_AddStringToObject(json, "message", message);
    cJSON_AddNumberToObject(json, "error_code", error_code);
    
    char *json_string = cJSON_Print(json);
    cJSON_Delete(json);
    
    return json_string;
}

// CONNECTION REGISTRY FUNCTIONS

// Allocates and initializes a thread-safe connection registry
ConnectionRegistry* create_registry() {
    ConnectionRegistry *registry = (ConnectionRegistry *)malloc(sizeof(ConnectionRegistry));
    if (!registry) {
        perror("malloc failed");
        return NULL;
    }
    
    registry->count = 0;
    registry->capacity = MAX_CONNECTIONS;
    registry->connections = (Connection *)malloc(sizeof(Connection) * MAX_CONNECTIONS);
    
    if (!registry->connections) {
        perror("malloc failed");
        free(registry);
        return NULL;
    }
    
    // Create mutex for thread-safe access
    if (pthread_mutex_init(&registry->lock, NULL) != 0) {
        perror("mutex init failed");
        free(registry->connections);
        free(registry);
        return NULL;
    }
    
    return registry;
}

// Adds a connection to the registry, expanding capacity if needed (thread-safe)
void add_connection(ConnectionRegistry *registry, Connection *conn) {
    pthread_mutex_lock(&registry->lock);
    
    if (registry->count >= registry->capacity) {
        // Dynamically expand the registry when full
        registry->capacity *= 2;
        registry->connections = (Connection *)realloc(
            registry->connections, 
            sizeof(Connection) * registry->capacity
        );
        printf("[DEBUG] Registry expanded to %d connections\n", registry->capacity);
    }
    
    registry->connections[registry->count] = *conn;
    registry->count++;
    
    printf("[DEBUG] Connection added: %s (%s) - Total: %d\n", 
           conn->identifier, conn->type, registry->count);
    
    pthread_mutex_unlock(&registry->lock);
}

// Finds and returns a connection by socket file descriptor (thread-safe)
Connection* get_connection(ConnectionRegistry *registry, int socket_fd) {
    pthread_mutex_lock(&registry->lock);
    
    Connection *result = NULL;
    for (int i = 0; i < registry->count; i++) {
        if (registry->connections[i].socket_fd == socket_fd) {
            result = &registry->connections[i];
            break;
        }
    }
    
    pthread_mutex_unlock(&registry->lock);
    return result;
}

// Removes a connection from the registry by socket file descriptor (thread-safe)
void remove_connection(ConnectionRegistry *registry, int socket_fd) {
    pthread_mutex_lock(&registry->lock);
    
    for (int i = 0; i < registry->count; i++) {
        if (registry->connections[i].socket_fd == socket_fd) {
            printf("[DEBUG] Removing connection: %s (%s)\n", 
                   registry->connections[i].identifier,
                   registry->connections[i].type);
            
            // Compact the array by shifting remaining connections
            for (int j = i; j < registry->count - 1; j++) {
                registry->connections[j] = registry->connections[j + 1];
            }
            
            registry->count--;
            printf("[DEBUG] Connection removed - Total: %d\n", registry->count);
            pthread_mutex_unlock(&registry->lock);
            return;
        }
    }
    
    pthread_mutex_unlock(&registry->lock);
}

// Prints a formatted table of all active connections with summary counts (thread-safe)
void print_all_connections(ConnectionRegistry *registry) {
    pthread_mutex_lock(&registry->lock);
    
    printf("\n=== Active Connections ===\n");
    printf("%-6s %-20s %-20s %-15s %-6s\n", 
           "Socket", "Identifier", "Type", "IP", "Port");
    printf("---------------------------------------------------------------\n");
    
    int clients = 0, servers = 0;
    
    for (int i = 0; i < registry->count; i++) {
        Connection *c = &registry->connections[i];
        printf("%-6d %-20s %-20s %-15s %-6d\n",
               c->socket_fd, c->identifier, c->type, c->client_ip, c->client_port);
        
        if (strcmp(c->type, "CLIENT") == 0) clients++;
        else if (strcmp(c->type, "STORAGE_SERVER") == 0) servers++;
    }
    
    printf("---------------------------------------------------------------\n");
    printf("Total: %d (Clients: %d, Storage Servers: %d)\n\n", 
           registry->count, clients, servers);
    
    pthread_mutex_unlock(&registry->lock);
}
