1. The maximum number of simultaneous connections is set to 100 by default. This can be changed by modifying the MAX_CONNECTIONS constant in the CommConfig.h file.
2. Unique libraries used are :
   - Boost.Asio for asynchronous networking
   - OpenSSL for secure communications
   - JSON for Modern C++ for data serialization
3. setsockopt() configures specific options or behaviors for a socket at the OS level.
Behaviors like allowing port reuse (SO_REUSEADDR), enabling keep-alive probes (SO_KEEPALIVE), setting timeouts (SO_RCVTIMEO / SO_SNDTIMEO), controlling buffer sizes (SO_RCVBUF / SO_SNDBUF), or permitting multiple binds on one port (SO_REUSEPORT).
4. htons(port) converts the 16-bit integer port from the host machine's byte order to network byte order (big-endian). It's required before storing a port number into struct sockaddr_in.sin_port so the value is transmitted/understood correctly across different CPU architectures.
5. Provides socket wrapper functions for TCP server/client creation, connection management, and message passing with JSON support. Includes a thread-safe connection registry for tracking active clients and storage servers, with automatic retry on signal interruption (EINTR) for robust accept/recv operations.
    while ((new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen)) < 0) {
        if (errno == EINTR) continue; // signal interrupted — retry
        perror("accept failed");
        return NULL;
    }
6. To handle signals like SIGINT gracefully during blocking socket operations, set up a signal handler using sigaction with the SA_RESTART flag. This ensures interrupted system calls are automatically retried.
    struct sigaction sa = { .sa_handler = handler /* or SIG_IGN */, .sa_flags = SA_RESTART };
    sigaction(SIGINT, &sa, NULL);
7. Handles partial sends
8. dont send an array as the value of a field in the message struct


LIST OF functions

Server Socket Functions

create_server_socket(int port) — Creates, binds, and sets a socket to listen on the specified port; returns the server socket fd or -1 on error.
accept_client(int server_fd) — Blocks until a client connects, then returns a heap-allocated Connection* with the new socket and peer details, or NULL on error.
close_server_socket(int server_fd) — Closes the server listening socket and logs a debug message.
Client Socket Functions
create_client_socket(const char *server_ip, int server_port) — Creates a socket, connects to the specified server IP and port, and returns the socket fd or -1 on error.
close_client_socket(int client_fd) — Closes the client socket and logs a debug message.

Message Functions

send_message(int socket_fd, const char *message) — Sends the entire message string over the socket in a loop until all bytes are sent; returns 0 on success, -1 on error.
receive_message(int socket_fd) — Receives data from the socket, null-terminates it, allocates a new string, and returns it (caller must free), or NULL on error/closed connection.

JSON Helper Functions

parse_message(const char *json_string) — Parses a JSON string and returns a cJSON* object, or NULL if parsing fails.
get_string_field(cJSON *json, const char *field_name) — Returns the string value of a JSON field, or NULL if not found or not a string.
get_int_field(cJSON *json, const char *field_name) — Returns the integer value of a JSON field, or -1 if not found or not a number.
create_response(const char *status, const char *message, int error_code) — Creates a JSON response object with status, message, and error_code fields; returns a formatted JSON string (caller must free).

Connection Registry Functions

create_registry() — Allocates and initializes a ConnectionRegistry with a mutex and initial capacity, or NULL on error.
add_connection(ConnectionRegistry *registry, Connection *conn) — Adds a connection to the registry (thread-safe); automatically expands capacity if needed.
get_connection(ConnectionRegistry *registry, int socket_fd) — Finds and returns a pointer to the connection with the given socket_fd, or NULL if not found (thread-safe).
remove_connection(ConnectionRegistry *registry, int socket_fd) — Removes the connection with the given socket_fd from the registry (thread-safe).
print_all_connections(ConnectionRegistry *registry) — Prints a formatted table of all active connections with counts by type (thread-safe).

MULTI-THREADING APPROACH
========================

ARCHITECTURE:
- Main thread runs accept loop calling accept_client() sequentially
- Each accepted client spawns a dedicated worker thread via pthread_create()
- Worker threads handle client communication independently and in parallel
- Connection queue (backlog=10) holds waiting clients in FIFO order

THREAD SAFETY:
- ConnectionRegistry protected by pthread_mutex for concurrent access
- One thread accepts, many threads process simultaneously

WHY: Scalable (100s-1000s clients), isolated (one slow client doesn't block others), standard pattern

