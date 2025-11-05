NAMING SERVER (NS) - documentation

OVERVIEW
--------
The Naming Server is the central coordinator of the distributed file system. It acts as a
directory service that tracks which Storage Servers are online, monitors their health via
heartbeats, and routes client requests to the appropriate Storage Server. The NS accepts
connections from both Storage Servers (for registration/heartbeats) and Clients (for
file operation requests).


ALL FUNCTIONS
-------------

LOGGING FUNCTIONS
-----------------
• ns_log(const char *level, const char *message)
  - Thread-safe logging with timestamps
  - Writes to console and log file (logs/ns.log) using mutex protection
  - Ensures no interleaving of log entries from multiple threads
  - Levels: INFO, DEBUG, WARNING, ERROR

HEARTBEAT FUNCTIONS
-------------------
• send_heartbeats_to_ss()
  - Sends periodic heartbeat requests to all alive Storage Servers
  - Iterates through heartbeat array and finds corresponding SS connections
  - Creates JSON message: {"type":"HEARTBEAT", "timestamp":...}
  - Sends to each registered Storage Server
  - Thread-safe using heartbeat_mutex

• handle_heartbeat_response(const char *message)
  - Processes HEARTBEAT_RESPONSE from Storage Servers
  - Extracts SS name from JSON message
  - Updates last_heartbeat timestamp and sets is_alive flag
  - Logs successful heartbeat reception
  - Thread-safe using heartbeat_mutex

• check_heartbeat_timeouts()
  - Checks if any Storage Server hasn't responded in >10 seconds
  - Marks timed-out servers as DOWN (is_alive = 0)
  - Logs warnings for failed heartbeats
  - Called periodically by heartbeat thread
  - Thread-safe using heartbeat_mutex

MESSAGE HANDLERS
----------------
• handle_client_registration(const char *message, int client_socket_fd)
  - Processes CLIENT_REGISTER messages from new clients
  - Extracts username from JSON
  - Updates connection in registry with type="CLIENT" and identifier=username
  - Sends success response back to client
  - Logs client registration

• handle_ss_registration(const char *message, int ss_socket_fd)
  - Processes SS_REGISTER messages from new Storage Servers
  - Extracts SS name, IP, and client_port from JSON
  - Updates connection in registry with type="STORAGE_SERVER" and identifier=ss_name
  - Initializes heartbeat tracking for this SS
  - Sends success response back to SS
  - Logs SS registration

• handle_incoming_message(const char *message, int sender_socket_fd)
  - Main message router that parses incoming JSON messages
  - Extracts "type" field and routes to appropriate handler:
    - "CLIENT_REGISTER" → handle_client_registration()
    - "SS_REGISTER" → handle_ss_registration()
    - "HEARTBEAT_RESPONSE" → handle_heartbeat_response()
  - Logs unknown message types as warnings
  - Thread-safe (called within connection handler threads)

THREAD FUNCTIONS
----------------
• handle_connection(void* arg)
  - Thread that handles communication with a single client or Storage Server
  - Runs infinite loop receiving messages from assigned socket
  - Calls handle_incoming_message() for each received message
  - Detects disconnection (receive_message returns NULL)
  - Cleans up: closes socket, removes from registry, frees memory
  - Thread exits on connection close
  - One thread spawned per connection

• heartbeat_thread(void* arg)
  - Dedicated background thread for Storage Server health monitoring
  - Runs infinite loop with 5-second intervals (sleep(5))
  - Calls send_heartbeats_to_ss() - sends heartbeat pings
  - Calls check_heartbeat_timeouts() - detects failed servers
  - Never exits (runs for entire NS lifetime)
  - Single instance (spawned at startup)

EVENT LOOP FUNCTIONS
--------------------
• event_loop(int server_fd)
  - Main event loop that coordinates all NS operations
  - Spawns heartbeat thread at startup
  - Continuously accepts new connections in infinite loop
  - For each accepted connection:
    - Adds to connection registry
    - Spawns handle_connection() thread
    - Stores thread ID in registry
    - Detaches thread for auto-cleanup
  - Never exits (runs until process terminated)

MAIN FUNCTION
-------------
• main()
  - Initializes Naming Server
  - Creates connection registry
  - Opens log file (logs/ns.log)
  - Creates server socket on port 5000
  - Enters event_loop() to handle all operations
  - Cleanup on shutdown (never reached in normal operation)


ARCHITECTURE: HOW NS SERVES CLIENTS AND STORAGE SERVERS
--------------------------------------------------------

SINGLE SERVER SOCKET:
The NS uses ONE server socket that accepts connections from BOTH clients and Storage Servers.
Differentiation happens at the application layer based on the first message received.

Socket: server_fd (created on port 5000)
Created: create_server_socket(ns_port)
Direction: Incoming (Clients/SS → NS)
Role: NS is SERVER to both clients and Storage Servers
Purpose: Accept all incoming connections, route based on message type
Threads: 1 accept loop + 1 heartbeat monitor + N connection handlers
Lifecycle: Persistent (entire NS lifetime)
Protocol: Receives: CLIENT_REGISTER, SS_REGISTER, HEARTBEAT_RESPONSE
          Sends: HEARTBEAT (to SS), Success/Error responses
Port: Listens on port 5000


DIFFERENTIATION MECHANISM
--------------------------
The NS identifies connection type based on the FIRST MESSAGE received:

1. Storage Server Connection:
   First message: {"type":"SS_REGISTER", "name":"SS1", ...}
   Action: 
   - Mark connection as type="STORAGE_SERVER"
   - Add to heartbeat monitoring
   - Store in path registry (future)

2. Client Connection:
   First message: {"type":"CLIENT_REGISTER", "username":"user1", ...}
   Action:
   - Mark connection as type="CLIENT"
   - Store username
   - No heartbeat monitoring


THREAD MODEL
------------

Naming Server Process
│
├─── Main Thread
│    ├─ Initialize registry, logs
│    ├─ Create server socket (port 5000)
│    └─ Enter event_loop()
│         │
│         └─ while(1) accept_client()
│
├─── Heartbeat Monitor Thread (spawned at startup)
│    └─ while(1) {
│         sleep(5)                           // Wait 5 seconds
│         send_heartbeats_to_ss()           // Ping all Storage Servers
│         check_heartbeat_timeouts()        // Mark failed servers as DOWN
│       }
│
├─── Connection Handler Thread #1 (Storage Server "SS1")
│    └─ while(1) {
│         receive_message()                 // Wait for messages from SS1
│         handle_incoming_message()         // Process (mainly HEARTBEAT_RESPONSE)
│       }
│
├─── Connection Handler Thread #2 (Client "alice")
│    └─ while(1) {
│         receive_message()                 // Wait for messages from alice
│         handle_incoming_message()         // Process (READ/WRITE/etc requests)
│       }
│
├─── Connection Handler Thread #3 (Storage Server "SS2")
│    └─ while(1) receive/handle SS2 messages
│
├─── Connection Handler Thread #4 (Client "bob")
│    └─ while(1) receive/handle bob's requests
│
└─── Connection Handler Thread #N
     └─ while(1) receive/handle messages


EXECUTION FLOW
--------------

main()
  │
  ├─> [1] create_registry()               // Initialize connection registry
  │
  ├─> [2] Open log file (logs/ns.log)
  │
  ├─> [3] Create server socket (port 5000)
  │       └─> server_fd = create_server_socket(5000)
  │               └─> socket(), bind(), listen()
  │
  └─> [4] event_loop(server_fd)
          │
          ├─> Spawn Heartbeat Thread
          │   │
          │   └─> pthread_create(&hb_thread, heartbeat_thread, NULL)
          │           │
          │           └─> while(1) {
          │                 sleep(5)
          │                 send_heartbeats_to_ss()      // Send HEARTBEAT to all SS
          │                 check_heartbeat_timeouts()   // Check for failures
          │               }
          │
          └─> Accept Loop (Main Thread)
              │
              └─> while(1) {
                    new_conn = accept_client(server_fd)  // Block until connection arrives
                    add_connection(registry, new_conn)   // Add to registry
                    
                    pthread_create(&thread, handle_connection, conn_copy)
                        │
                        └─> while(1) {
                              msg = receive_message(socket)
                              handle_incoming_message(msg, socket)
                                  │
                                  ├─> if "SS_REGISTER" → handle_ss_registration()
                                  ├─> if "CLIENT_REGISTER" → handle_client_registration()
                                  └─> if "HEARTBEAT_RESPONSE" → handle_heartbeat_response()
                            }
                  }


HEARTBEAT MECHANISM DETAILS
----------------------------

Data Structure:
    typedef struct {
        char name[100];           // Storage Server name (e.g., "SS1")
        time_t last_heartbeat;    // Timestamp of last successful response
        int is_alive;             // 1 = alive, 0 = down/timed out
    } HeartbeatInfo;

    HeartbeatInfo heartbeats[MAX_CONNECTIONS];  // Array of all registered SS
    int heartbeat_count;                        // Number of registered SS
    pthread_mutex_t heartbeat_mutex;            // Protects heartbeats array

Flow:
    1. SS registers → add to heartbeats array with is_alive=1
    2. Heartbeat thread runs every 5 seconds
    3. send_heartbeats_to_ss() sends {"type":"HEARTBEAT"} to all alive SS
    4. SS responds with {"type":"HEARTBEAT_RESPONSE", "name":"SS1"}
    5. handle_heartbeat_response() updates last_heartbeat timestamp
    6. check_heartbeat_timeouts() marks SS as DOWN if >10 seconds elapsed

Timeline:
    T=0s:  SS registers, last_heartbeat = now, is_alive = 1
    T=5s:  NS sends HEARTBEAT
    T=5s:  SS responds, last_heartbeat updated
    T=10s: NS sends HEARTBEAT
    T=10s: SS responds (OK)
    T=15s: NS sends HEARTBEAT
    T=15s: SS fails to respond
    T=20s: NS sends HEARTBEAT, check_timeouts() sees elapsed > 10s
           → is_alive = 0, log "SS1 failed heartbeat - Status: DOWN"


CONNECTION LIFECYCLE
--------------------

Storage Server Connection:
    1. SS connects to NS port 5000
    2. NS accepts, spawns connection handler thread
    3. SS sends SS_REGISTER message
    4. NS processes registration:
       - Marks connection as "STORAGE_SERVER"
       - Adds to heartbeat monitoring
       - Stores SS name as identifier
    5. Thread enters receive loop
    6. Periodically receives HEARTBEAT requests from heartbeat thread
    7. SS sends HEARTBEAT_RESPONSE
    8. Connection stays open indefinitely
    9. On disconnect: thread exits, removes from registry

Client Connection:
    1. Client connects to NS port 5000
    2. NS accepts, spawns connection handler thread
    3. Client sends CLIENT_REGISTER message
    4. NS processes registration:
       - Marks connection as "CLIENT"
       - Stores username as identifier
    5. Thread enters receive loop
    6. Client sends file operation requests (READ/WRITE/etc)
    7. NS processes and responds
    8. Connection may close after operations complete
    9. On disconnect: thread exits, removes from registry


GLOBAL VARIABLES
----------------
• registry               - ConnectionRegistry* - Tracks all active connections
• log_file               - FILE* - Log file pointer (logs/ns.log)
• ns_port                - int - Port NS listens on (5000)
• heartbeats[]           - HeartbeatInfo[] - Array tracking SS health
• heartbeat_count        - int - Number of registered Storage Servers
• heartbeat_mutex        - pthread_mutex_t - Protects heartbeats array
• log_mutex              - pthread_mutex_t - Protects log file writes


KEY DIFFERENCES: CLIENT vs STORAGE SERVER HANDLING
---------------------------------------------------

Aspect              | Storage Server                    | Client
--------------------|-----------------------------------|----------------------------------
First message       | SS_REGISTER                       | CLIENT_REGISTER
Connection type     | "STORAGE_SERVER"                  | "CLIENT"
Identifier          | SS name (e.g., "SS1")            | Username (e.g., "alice")
Heartbeat monitored | Yes (added to heartbeats array)   | No
Health tracking     | Yes (is_alive flag, timeouts)     | No
Receives pings      | Yes (HEARTBEAT every 5s)          | No
Expected lifetime   | Long-lived (persistent)           | Short-lived (per-operation)
Purpose             | Service provider (stores files)   | Service consumer (requests files)
NS tracks           | Paths it serves, health status    | Current requests only


THREAD SAFETY
-------------
• Connection Registry: Protected by registry->lock (mutex in ConnectionRegistry)
• Heartbeat Array: Protected by heartbeat_mutex
• Log File: Protected by log_mutex
• All shared data structures use mutexes to prevent race conditions


USAGE
-----
# Start Naming Server (listens on port 5000)
./nameserver


DEPENDENCIES
------------
• communication.h   - Socket and message handling functions
• cJSON             - JSON parsing and generation
• pthread           - POSIX threads for concurrency
• POSIX sockets     - TCP/IP networking


NOTES
-----
• Naming Server is the central coordinator - if it fails, the entire system is unavailable
• Uses single server socket but differentiates clients vs SS at application layer
• Heartbeat mechanism ensures NS knows which Storage Servers are operational
• Connection handlers run independently - failure of one doesn't affect others
• Thread-safe design allows concurrent client and SS connections
• Future enhancements: path registry, load balancing, request routing to appropriate SS
