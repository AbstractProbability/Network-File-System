1. Main Thread

Purpose: Initialization and accepting new connections
What it does:
Creates server socket and connection registry
Initializes heartbeat system
Spawns the heartbeat monitor thread
Runs infinite accept loop waiting for new connections
For each accepted connection, spawns a connection handler thread
Never exits (runs continuously)

2. Heartbeat Monitor Thread

Created by: pthread_create(&heartbeat_thread, NULL, heartbeat_monitor, NULL)
Function: heartbeat_monitor(void *arg)
What it does:
Runs infinite loop with 5-second intervals (sleep(5))
Calls send_heartbeats_to_ss() - sends heartbeat pings to all alive storage servers
Calls check_heartbeat_timeouts() - marks storage servers as DOWN if no response in 10 seconds
Continuously monitors storage server health
Thread-safe using heartbeat_mutex
Lifecycle: Runs forever (daemon-like behavior)

3. Connection Handler Threads (Multiple)

Created by: pthread_create(&thread_id, NULL, connection_handler, (void*)conn) for each new connection
Function: connection_handler(void *arg)
What each does:
Handles one specific client or storage server connection
Runs infinite loop receiving messages from its assigned socket
Parses incoming JSON requests
Routes requests to appropriate handlers:
handle_register() - storage server registration
handle_heartbeat_response() - heartbeat acknowledgments
handle_read() - client read requests
handle_write() - client write requests
handle_delete() - client delete requests
handle_create() - client create requests
handle_list() - client list requests
Other operations as needed
Sends JSON responses back to client/storage server
Cleans up on disconnect: removes from registry, closes socket, frees memory
Thread exits when connection closes


Lifecycle: One per connection, exits when client/storage server disconnects

Count: Dynamic - grows with number of concurrent connections


Thread Synchronization
Thread Type	Shared Resources	                Synchronization
Main Thread	registry	                        registry->lock mutex
Heartbeat Monitor	                            heartbeats[] array	heartbeat_mutex
Connection Handlers	registry, heartbeats[]	    registry->lock, heartbeat_mutex
