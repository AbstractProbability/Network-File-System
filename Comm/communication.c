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

// NESTED ARRAY MESSAGE FUNCTIONS

// Creates a new message structure
Message* create_message() {
    Message *msg = (Message *)malloc(sizeof(Message));
    if (!msg) {
        perror("malloc failed");
        return NULL;
    }
    
    msg->capacity = 10;
    msg->count = 0;
    msg->keys = (char **)malloc(sizeof(char *) * msg->capacity);
    msg->values = (char **)malloc(sizeof(char *) * msg->capacity);
    
    if (!msg->keys || !msg->values) {
        perror("malloc failed");
        free(msg);
        return NULL;
    }
    
    return msg;
}

// Adds a string field to the message
void add_string_field(Message *msg, const char *key, const char *value) {
    if (!msg) return;
    
    // Expand capacity if needed
    if (msg->count >= msg->capacity) {
        msg->capacity *= 2;
        msg->keys = (char **)realloc(msg->keys, sizeof(char *) * msg->capacity);
        msg->values = (char **)realloc(msg->values, sizeof(char *) * msg->capacity);
    }
    
    // Allocate and copy key
    msg->keys[msg->count] = (char *)malloc(strlen(key) + 1);
    strcpy(msg->keys[msg->count], key);
    
    // Allocate and copy value
    msg->values[msg->count] = (char *)malloc(strlen(value) + 1);
    strcpy(msg->values[msg->count], value);
    
    msg->count++;
}

// Adds a number field to the message (converted to string)
void add_number_field(Message *msg, const char *key, double value) {
    if (!msg) return;
    
    char value_str[64];
    snprintf(value_str, sizeof(value_str), "%.0f", value);
    add_string_field(msg, key, value_str);
}

// Adds an empty array field to the message
void add_array_field(Message *msg, const char *key) {
    if (!msg) return;
    add_string_field(msg, key, "[]");
}

// Serializes message to nested array format: [["key1","val1"],["key2","val2"]]
char* serialize_message(Message *msg) {
    if (!msg) return NULL;
    
    // Calculate required buffer size
    size_t size = 2; // For "[]"
    for (int i = 0; i < msg->count; i++) {
        size += strlen(msg->keys[i]) + strlen(msg->values[i]) + 8; // ["",""],
    }
    
    char *result = (char *)malloc(size);
    if (!result) {
        perror("malloc failed");
        return NULL;
    }
    
    strcpy(result, "[");
    
    for (int i = 0; i < msg->count; i++) {
        if (i > 0) strcat(result, ",");
        strcat(result, "[\"");
        strcat(result, msg->keys[i]);
        strcat(result, "\",\"");
        strcat(result, msg->values[i]);
        strcat(result, "\"]");
    }
    
    strcat(result, "]");
    
    return result;
}

// Parses a nested array format string: [["key1","val1"],["key2","val2"]]
Message* parse_message(const char *msg_string) {
    if (!msg_string) return NULL;
    
    Message *msg = create_message();
    if (!msg) return NULL;
    
    const char *ptr = msg_string;
    
    // Skip initial '['
    while (*ptr && (*ptr == ' ' || *ptr == '[')) ptr++;
    
    while (*ptr && *ptr != ']') {
        // Skip to opening quote of key
        while (*ptr && *ptr != '"') ptr++;
        if (!*ptr) break;
        ptr++; // Skip opening quote
        
        // Extract key
        const char *key_start = ptr;
        while (*ptr && *ptr != '"') ptr++;
        if (!*ptr) break;
        
        size_t key_len = ptr - key_start;
        char *key = (char *)malloc(key_len + 1);
        strncpy(key, key_start, key_len);
        key[key_len] = '\0';
        ptr++; // Skip closing quote
        
        // Skip to opening quote of value
        while (*ptr && *ptr != '"') ptr++;
        if (!*ptr) {
            free(key);
            break;
        }
        ptr++; // Skip opening quote
        
        // Extract value
        const char *val_start = ptr;
        while (*ptr && *ptr != '"') ptr++;
        if (!*ptr) {
            free(key);
            break;
        }
        
        size_t val_len = ptr - val_start;
        char *value = (char *)malloc(val_len + 1);
        strncpy(value, val_start, val_len);
        value[val_len] = '\0';
        ptr++; // Skip closing quote
        
        // Add to message
        add_string_field(msg, key, value);
        
        free(key);
        free(value);
        
        // Skip to next pair
        while (*ptr && (*ptr == ' ' || *ptr == ',' || *ptr == ']')) {
            if (*ptr == ']') {
                // Check if this is the end bracket for a pair or the whole message
                ptr++;
                if (*ptr == ',' || *ptr == ' ') continue;
                break;
            }
            ptr++;
        }
        if (*ptr == '[') ptr++;
    }
    
    return msg;
}

// Extracts a string value from a message by field name
char* get_string_field(Message *msg, const char *field_name) {
    if (!msg) return NULL;
    
    for (int i = 0; i < msg->count; i++) {
        if (strcmp(msg->keys[i], field_name) == 0) {
            return msg->values[i];
        }
    }
    
    return NULL;
}

// Extracts an integer value from a message by field name
int get_int_field(Message *msg, const char *field_name) {
    char *value = get_string_field(msg, field_name);
    if (!value) return -1;
    
    return atoi(value);
}

// Extracts a double value from a message by field name
double get_double_field(Message *msg, const char *field_name) {
    char *value = get_string_field(msg, field_name);
    if (!value) return -1.0;
    
    return atof(value);
}

// Frees a message structure and all its allocated memory
void free_message(Message *msg) {
    if (!msg) return;
    
    for (int i = 0; i < msg->count; i++) {
        free(msg->keys[i]);
        free(msg->values[i]);
    }
    
    free(msg->keys);
    free(msg->values);
    free(msg);
}

// Creates a response message with status, message, and error_code fields (caller must free)
char* create_response(const char *status, const char *message, int error_code) {
    Message *msg = create_message();
    
    add_string_field(msg, "status", status);
    add_string_field(msg, "message", message);
    add_number_field(msg, "error_code", error_code);
    
    char *result = serialize_message(msg);
    free_message(msg);
    
    return result;
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
