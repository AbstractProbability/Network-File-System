# Naming Server (NS) Implementation

## Overview
This is a simple implementation of the Naming Server for the distributed file system project. The NS acts as a central coordinator managing communication between clients and storage servers.

## Features Implemented

### Core Functionality
- **User Management**: Tracks all users (registered and active)
- **Storage Server Registry**: Maintains list of connected storage servers
- **File Indexing**: Maps files to their storage locations with metadata
- **Access Control**: Manages read/write permissions per user per file
- **Concurrent Handling**: Multi-threaded to handle multiple connections
- **Logging**: Comprehensive logging with timestamps

### Operation Modes

1. **Direct Mode** (NS handles directly):
   - `VIEW`: List files user has access to
   - `VIEW -a`: List all files
   - `VIEW -l`: List files with details
   - `LIST`: List all users
   - `INFO <filename>`: Show file metadata
   - `ADDACCESS -R <filename> <username>`: Grant read access
   - `ADDACCESS -W <filename> <username>`: Grant write access
   - `REMACCESS <filename> <username>`: Remove all access

2. **Offloader Mode** (NS provides SS info for direct client-SS connection):
   - `READ <filename>`
   - `WRITE <filename> <sentence_number>`
   - `STREAM <filename>`

3. **Modulator Mode** (NS forwards to SS):
   - `CREATE <filename>`
   - `DELETE <filename>`
   - `UNDO <filename>`

## Architecture

### Data Structures
- **storage_server**: Stores SS info (IP, ports, active status)
- **file_metadata**: File info (path, owner, SS ID, permissions)
- **user_info**: User info (username, active status)
- **naming_server**: Global state with thread-safe mutex

### Thread Safety
- Global mutex protects shared data structures
- Thread-per-connection model for scalability

### Protocol
Simple text-based protocol:
- Client registration: `REGISTER_CLIENT <username>`
- SS registration: `REGISTER_SS <ip> <nm_port> <client_port> <num_files>`
- File registration: `FILE <filepath> <owner> <size>`
- Commands follow standard format

## Building

```bash
cd NamingServer
make
```

This creates the executable at `bin/naming_server`

## Running

```bash
make run
# or
./bin/naming_server
```

The server will:
1. Initialize and create log file (`ns_log.txt`)
2. Listen on a dynamically assigned port
3. Print the port number to stdout
4. Accept connections from clients and storage servers

## Usage Flow

### Storage Server Connection
1. SS connects to NS
2. Sends: `REGISTER_SS <ip> <nm_port> <client_port> <num_files>`
3. For each file, sends: `FILE <filepath> <owner> <size>`
4. NS acknowledges each message

### Client Connection
1. Client connects to NS
2. Sends: `REGISTER_CLIENT <username>`
3. NS registers/activates user
4. Client can then send commands

### Example Commands
```
LIST                          # List all users
VIEW                          # List accessible files
VIEW -a                       # List all files
VIEW -l                       # List files with details
INFO myfile.txt              # Show file info
READ myfile.txt              # Get SS info for reading
ADDACCESS -R myfile.txt bob  # Grant read access to bob
```

## Logging
All operations are logged to `ns_log.txt` with timestamps:
- Connection events
- Registration events
- Client requests
- Responses and errors

## Simplifications

To keep the implementation manageable:
1. **No actual SS communication**: For modulator mode, NS just sends SS info to client
2. **Simple load balancing**: New files go to first available SS
3. **Basic error handling**: Clear error messages for common cases
4. **Static limits**: MAX_USERS, MAX_STORAGE_SERVERS, etc.
5. **No persistence**: All data in memory (lost on restart)

## Future Enhancements

Could be added:
- Actual SS communication for modulator operations
- Persistent storage of metadata
- Load balancing across multiple SS
- Caching for frequent requests
- Heartbeat monitoring of SS
- Redundancy/replication support
- EXEC command implementation
- More sophisticated search algorithms

## File Structure
```
NamingServer/
├── include/
│   └── ns.h           # Header with structures and declarations
├── src/
│   ├── main.c         # Entry point and server loop
│   ├── ns_server.c    # Core server functions
│   └── handlers.c     # Request handlers
├── makefile           # Build configuration
└── README.md          # This file
```

## Dependencies
- POSIX threads (pthread)
- Standard C libraries
- Common socket utilities from ../common.c

## Notes
- Port is dynamically assigned (printed on startup)
- Thread-safe operations using mutex
- Owner of file gets automatic read/write access
- Only owner can modify access permissions
- Only owner can delete files
