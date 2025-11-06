To keep in mind : 

1. while logging heartbeat messages, timestamp isnt being saved

2. log directory is being created in the current working directory instead of home directory

3. ss talks to client using soocket at its 5001 port

4. ss talks to ns using socket at ns 5000 port

5. ss event loop creates detached threads for each client connection and for ns listener

6. ss creates log file at logs/SS1.log (or SS2.log etc based on name) 


STORAGE SERVER (SS) - documentation

OVERVIEW
--------
The Storage Server acts as both a CLIENT (to the Naming Server) and a SERVER (to direct clients).
It maintains a persistent connection to the NS for registration and heartbeats, while simultaneously
accepting and serving client file operation requests on its own port.


ALL FUNCTIONS
-------------

LOGGING FUNCTIONS
-----------------
• ss_log(const char *level, const char *message)
  - Thread-safe logging with timestamps
  - Writes to console and log file using mutex protection
  - Ensures no interleaving of log entries from multiple threads

CONFIGURATION FUNCTIONS
-----------------------
• parse_arguments(int argc, char *argv[])
  - Parses command-line arguments (--name, --port)
  - Configures SS name and port number
  - Default: name="SS1", port=5001

MESSAGE CREATION FUNCTIONS
---------------------------
• create_ss_registration_message()
  - Creates JSON registration message with SS name, IP, ports, and file list
  - Sends to NS during startup to register this storage server
  - Format: {"type":"SS_REGISTER", "name":"SS1", "ip":"127.0.0.1", ...}

HEARTBEAT FUNCTIONS
-------------------
• handle_heartbeat_from_ns(const char *message)
  - Receives heartbeat request from NS
  - Validates message type
  - Creates and sends HEARTBEAT_RESPONSE back to NS
  - Keeps NS informed that this SS is alive

THREAD FUNCTIONS
----------------
• ns_listener_thread(void* arg)
  - Dedicated thread that continuously listens for messages from NS
  - Primarily handles heartbeat requests
  - Runs for entire SS lifetime
  - If NS connection lost, logs error and exits

• handle_client_connection(void* arg)
  - Thread that handles a single client connection
  - Receives messages and processes requests (stub for now - to be implemented)
  - Cleans up connection on client disconnect
  - One thread spawned per connected client

EVENT LOOP FUNCTIONS
--------------------
• ss_event_loop(int client_server_fd)
  - Main event loop that coordinates all operations
  - Spawns NS listener thread at startup
  - Continuously accepts client connections in infinite loop
  - Spawns handler thread for each accepted client
  - Never exits (runs until process terminated)

MAIN FUNCTION
-------------
• main(int argc, char *argv[])
  - Initializes storage server (registry, logs)
  - Connects to NS as a client (port 5000)
  - Sends registration message to NS
  - Creates server socket for clients (my_port)
  - Enters event loop to handle connections
  - Cleanup on shutdown


ARCHITECTURE: HOW SS SERVES BOTH NS AND CLIENTS
------------------------------------------------

TWO SEPARATE SOCKETS:

1. NS Connection (Outgoing - SS acts as CLIENT)
   Socket: ns_socket (global variable)
   Created: create_client_socket("127.0.0.1", 5000)
   Direction: Outgoing (SS → NS)
   Role: SS is CLIENT to NS
   Purpose: Registration + Heartbeat responses
   Thread: ns_listener_thread() - 1 dedicated thread
   Lifecycle: Persistent (entire SS lifetime)
   Protocol: Sends: SS_REGISTER, HEARTBEAT_RESPONSE
             Receives: HEARTBEAT
   Port: Connects to NS port 5000

2. Client Connections (Incoming - SS acts as SERVER)
   Socket: client_server_fd (listening) + individual client sockets
   Created: create_server_socket(my_port)
   Direction: Incoming (Clients → SS)
   Role: SS is SERVER to clients
   Purpose: File operations (READ/WRITE/DELETE/etc.)
   Threads: 1 accept loop + N handler threads (1 per client)
   Lifecycle: Short-lived per request
   Protocol: Receives: READ, WRITE, DELETE, etc.
             Sends: File data, status responses
   Port: Listens on my_port (default 5001)


THREAD MODEL
------------

Storage Server Process
│
├─── Main Thread
│    ├─ Initialize registry, logs
│    ├─ Connect to NS (ns_socket)
│    ├─ Send registration
│    ├─ Create server socket (client_server_fd)
│    └─ Enter ss_event_loop()
│
├─── NS Listener Thread (spawned at line 168)
│    └─ while(1) {
│         receive_message(ns_socket)           // Wait for NS heartbeat
│         handle_heartbeat_from_ns()           // Send response
│       }
│
├─── Accept Loop (in main thread, lines 174-197)
│    └─ while(1) {
│         accept_client(client_server_fd)      // Wait for client
│         spawn handler thread
│       }
│
├─── Client Handler Thread #1
│    └─ handle_client_connection()
│         └─ while(1) receive/process client requests
│
├─── Client Handler Thread #2
│    └─ handle_client_connection()
│
└─── Client Handler Thread #N
     └─ handle_client_connection()


EXECUTION FLOW
--------------

main()
  │
  ├─> [1] parse_arguments()              // Parse --name, --port
  │
  ├─> [2] create_registry()              // Initialize connection registry
  │
  ├─> [3] Open log file (logs/SS1.log)
  │
  ├─> [4] CONNECT TO NS (as client)
  │       │
  │       ├─> ns_socket = create_client_socket("127.0.0.1", 5000)
  │       │       └─> Creates TCP socket, connects to NS
  │       │
  │       ├─> create_ss_registration_message()
  │       │       └─> JSON: {"type":"SS_REGISTER", "name":"SS1", ...}
  │       │
  │       ├─> send_message(ns_socket, reg_msg)
  │       │       └─> Sends registration to NS
  │       │
  │       └─> receive_message(ns_socket)
  │               └─> Waits for NS acknowledgment
  │
  ├─> [5] CREATE SERVER FOR CLIENTS
  │       │
  │       └─> client_server_fd = create_server_socket(my_port)
  │               └─> Creates socket, binds to port 5001, calls listen()
  │
  └─> [6] ss_event_loop(client_server_fd)
          │
          ├─> Spawn NS Listener Thread
          │   │
          │   └─> pthread_create(&ns_thread, ns_listener_thread, NULL)
          │           │
          │           └─> while(1) {
          │                 msg = receive_message(ns_socket)  // Blocks waiting for NS
          │                 handle_heartbeat_from_ns(msg)     // Send response
          │               }
          │
          └─> Main Loop: Accept Clients
              │
              └─> while(1) {
                    new_conn = accept_client(client_server_fd)  // Blocks waiting for clients
                    add_connection(registry, new_conn)
                    
                    pthread_create(&thread, handle_client_connection, conn_copy)
                        │
                        └─> while(1) {
                              msg = receive_message(client_socket)  // Receive client requests
                              // Process READ/WRITE/etc (stub for now)
                            }
                  }


KEY DIFFERENCES: NS vs CLIENT HANDLING
---------------------------------------

Aspect              | NS Connection                    | Client Connections
--------------------|----------------------------------|----------------------------------
Socket creation     | create_client_socket() (connect) | create_server_socket() + accept_client()
Initiated by        | SS connects to NS                | Clients connect to SS
Number of conns     | 1 persistent                     | N concurrent (dynamic)
Threads             | 1 dedicated listener             | 1 per client (spawned on-demand)
Messages            | Heartbeat requests/responses     | File operation requests/responses
Error handling      | If lost, SS can't function       | Individual client failure doesn't affect others
Port                | NS port 5000                     | SS's own port 5001


GLOBAL VARIABLES
----------------
• ns_socket         - Socket connected to Naming Server
• my_port           - Port SS listens on for clients (default 5001)
• my_name           - Storage server identifier (default "SS1")
• my_ip             - Storage server IP address (default "127.0.0.1")
• log_file          - File pointer for logging
• registry          - Connection registry for tracking clients
• log_mutex         - Mutex for thread-safe logging


USAGE
-----
# Start with defaults (name=SS1, port=5001)
./storageserver

# Start with custom name and port
./storageserver --name SS2 --port 5002


DEPENDENCIES
------------
• communication.h   - Socket and message handling functions
• cJSON             - JSON parsing and generation
• pthread           - POSIX threads for concurrency
• POSIX sockets     - TCP/IP networking


NOTES
-----
• Storage Server simultaneously acts as CLIENT (to NS) and SERVER (to clients)
• Uses separate sockets and threads for each role
• Thread-safe operations using mutexes (log_mutex, registry->lock)
• Heartbeat mechanism ensures NS knows SS is alive
• File operations (READ/WRITE/etc.) to be implemented in handle_client_connection()
