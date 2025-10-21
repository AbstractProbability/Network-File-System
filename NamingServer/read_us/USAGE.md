# Naming Server - Complete Guide

## Quick Start

### Build Everything
```bash
cd /home/akshatg/cp/course-project/cp-code/NamingServer
make
```

This creates:
- `bin/naming_server` - The NS server
- `bin/test_client` - Test client program
- `bin/test_ss` - Test storage server program

### Run the Test
```bash
./test.sh
```

This automated test will:
1. Start the Naming Server
2. Register a mock storage server with 3 sample files
3. Display the log
4. Give you instructions for manual testing

## Manual Testing

### Terminal 1: Start Naming Server
```bash
cd /home/akshatg/cp/course-project/cp-code/NamingServer
./bin/naming_server
```

Note the port number it prints (e.g., "Naming Server started on port 12345")

### Terminal 2: Register a Storage Server
```bash
./bin/test_ss 127.0.0.1 <NS_PORT> 127.0.0.1 6000 6001
```

Replace `<NS_PORT>` with the actual port. This will:
- Register a storage server
- Add 3 sample files: file1.txt (owner: alice), file2.txt (owner: bob), docs/file3.txt (owner: alice)

Press Enter after registration to keep SS connected, or it will disconnect.

### Terminal 3: Test Client as Alice
```bash
./bin/test_client 127.0.0.1 <NS_PORT> alice
```

Try these commands:
```
LIST                          # See all users (alice, bob)
VIEW                          # See files alice can access
VIEW -a                       # See all files
VIEW -l                       # See files with details
INFO file1.txt               # Get file1.txt metadata
READ file1.txt               # Get SS info for reading
ADDACCESS -R file1.txt bob   # Grant bob read access to file1.txt
ADDACCESS -W file1.txt bob   # Grant bob write access to file1.txt
```

### Terminal 4: Test Client as Bob
```bash
./bin/test_client 127.0.0.1 <NS_PORT> bob
```

Try these commands:
```
LIST                   # See users
VIEW                   # See files bob can access (file2.txt, and file1.txt if alice granted access)
INFO file2.txt         # Get file2.txt metadata
READ file1.txt         # Try to read alice's file (should work after alice grants access)
DELETE file1.txt       # Try to delete alice's file (should fail - not owner)
```

## Protocol Details

### Client Registration
```
Client -> NS: REGISTER_CLIENT <username>
NS -> Client: OK
```

### Storage Server Registration
```
SS -> NS: REGISTER_SS <ip> <nm_port> <client_port> <num_files>
NS -> SS: OK
SS -> NS: FILE <filepath> <owner> <size>
NS -> SS: OK
(repeat for each file)
```

### Client Commands

#### Direct Operations (NS handles)
```
Client -> NS: LIST
NS -> Client: USERS:\nalice (online)\nbob (offline)\n...

Client -> NS: VIEW
NS -> Client: FILES:\nfile1.txt\nfile2.txt\n...

Client -> NS: VIEW -a
NS -> Client: FILES:\nfile1.txt\nfile2.txt (no access)\n...

Client -> NS: INFO <filename>
NS -> Client: File: ...\nOwner: ...\nSize: ...\n...

Client -> NS: ADDACCESS -R <filename> <username>
NS -> Client: OK: Access granted
```

#### Offload Operations (NS provides SS info)
```
Client -> NS: READ <filename>
NS -> Client: SS_INFO <ss_ip> <ss_port>
(Client then connects to SS directly)

Client -> NS: WRITE <filename> <sentence_num>
NS -> Client: SS_INFO <ss_ip> <ss_port>

Client -> NS: STREAM <filename>
NS -> Client: SS_INFO <ss_ip> <ss_port>
```

#### Modulator Operations (NS forwards to SS)
```
Client -> NS: CREATE <filename>
NS -> Client: FORWARD_TO_SS <ss_ip> <ss_port>

Client -> NS: DELETE <filename>
NS -> Client: FORWARD_TO_SS <ss_ip> <ss_port>

Client -> NS: UNDO <filename>
NS -> Client: FORWARD_TO_SS <ss_ip> <ss_port>
```

## Data Structures

### Storage Server
```c
typedef struct {
    int id;                    // Unique ID assigned by NS
    char ip[MAX_IP];          // IP address
    int nm_port;              // Port for NS-SS communication
    int client_port;          // Port for Client-SS communication
    int active;               // 1 if active, 0 if disconnected
} storage_server;
```

### File Metadata
```c
typedef struct {
    char filepath[MAX_FILENAME];
    char owner[MAX_USERNAME];
    int ss_id;                     // Which SS has this file
    int size;
    time_t last_access;
    int read_access[MAX_USERS];    // Per-user read permissions
    int write_access[MAX_USERS];   // Per-user write permissions
} file_metadata;
```

### User Info
```c
typedef struct {
    char username[MAX_USERNAME];
    int active;               // 1 if currently connected
    int user_index;          // Index in users array
} user_info;
```

## Access Control Rules

1. **File Owner**: Has automatic read and write access
2. **Other Users**: Need explicit permissions granted by owner
3. **Permission Commands**: Only owner can grant/revoke access
4. **Delete Permission**: Only owner can delete files
5. **File Creation**: Creator becomes the owner

## Logging

All operations are logged to `ns_log.txt` with format:
```
[Timestamp] Message
```

Examples:
```
[Mon Oct 21 10:30:15 2025] Naming Server initialized
[Mon Oct 21 10:30:15 2025] Naming Server started on port 45123
[Mon Oct 21 10:30:20 2025] New connection from 127.0.0.1:54321
[Mon Oct 21 10:30:20 2025] Storage Server registered: 127.0.0.1:6000 (SS_ID=0)
[Mon Oct 21 10:30:25 2025] Client registered: alice
[Mon Oct 21 10:30:30 2025] Request from alice: VIEW
[Mon Oct 21 10:30:30 2025] Response to alice: VIEW command
```

## Error Handling

### Common Errors
- `ERROR: User limit reached` - Too many users registered
- `ERROR: SS limit reached` - Too many storage servers
- `ERROR: File not found` - Requested file doesn't exist
- `ERROR: Permission denied` - User lacks required permissions
- `ERROR: Only owner can modify access` - Non-owner tried to change permissions
- `ERROR: Only owner can delete` - Non-owner tried to delete file
- `ERROR: Unknown command` - Invalid command sent

## Configuration

Edit constants in `include/ns.h`:
```c
#define MAX_FILENAME 256          // Max file path length
#define MAX_USERNAME 64           // Max username length
#define MAX_IP 16                 // Max IP address length
#define MAX_USERS 100             // Max number of users
#define MAX_STORAGE_SERVERS 10    // Max number of storage servers
#define MAX_FILES_PER_SS 1000     // Max files per SS
#define LOG_FILE "ns_log.txt"     // Log file name
```

## Thread Safety

- Global `naming_server` structure protected by mutex
- Each connection handled in separate thread
- Thread-safe logging
- Lock acquired for:
  - Adding/modifying users
  - Adding/modifying files
  - Checking permissions
  - Updating access rights

## Limitations (Simplified Implementation)

1. **No persistent storage**: All data lost on restart
2. **No actual SS communication**: For modulator operations, NS just provides SS address
3. **Simple load balancing**: New files always go to first SS
4. **Static limits**: Hard-coded max users, files, etc.
5. **No heartbeat**: Doesn't detect SS failures
6. **No redundancy**: Each file on only one SS
7. **No EXEC implementation**: Would require shell execution
8. **Basic search**: Linear search through files (not optimized)

## Production Enhancements

For a production system, consider:

1. **Persistent Storage**
   - Store metadata in database (SQLite, PostgreSQL)
   - Recover state on restart

2. **Advanced Search**
   - Trie for prefix matching
   - Hash tables for O(1) lookups
   - Indexed database queries

3. **Caching**
   - LRU cache for frequently accessed files
   - Cache SS availability status

4. **Load Balancing**
   - Round-robin SS selection
   - Consider SS load and capacity
   - Balance file distribution

5. **Fault Tolerance**
   - Heartbeat monitoring of SS
   - Automatic failover
   - File replication

6. **Security**
   - Real authentication (passwords, tokens)
   - Encrypted communication
   - Audit logging

7. **Scalability**
   - Dynamic resource allocation
   - Distributed NS (multiple NS instances)
   - Sharding of file metadata

## Troubleshooting

### NS won't start
- Check if port is already in use
- Verify permissions to create log file
- Check ulimit for open files

### Client can't connect
- Verify NS is running
- Check firewall rules
- Ensure correct IP and port

### SS registration fails
- Check network connectivity
- Verify SS sends correct format
- Check SS limit not reached

### Permission errors
- Verify user is owner for protected operations
- Check access was granted with ADDACCESS
- View permissions with INFO command

### Log file issues
- Check write permissions in directory
- Disk space available
- File not locked by another process

## Example Session

```bash
# Terminal 1: Start NS
$ ./bin/naming_server
Naming Server started on port 41234

# Terminal 2: Register SS
$ ./bin/test_ss 127.0.0.1 41234 127.0.0.1 6000 6001
Connecting to Naming Server at 127.0.0.1:41234...
Connected! Local port: 54321
Registering SS: REGISTER_SS 127.0.0.1 6000 6001 3
Storage Server registration acknowledged
Sending file 1: FILE file1.txt alice 1024
File 1 registered successfully
Sending file 2: FILE file2.txt bob 2048
File 2 registered successfully
Sending file 3: FILE docs/file3.txt alice 512
File 3 registered successfully
Storage Server registration complete!

# Terminal 3: Client as alice
$ ./bin/test_client 127.0.0.1 41234 alice
Connecting to Naming Server at 127.0.0.1:41234...
Connected! Client port: 54322
Successfully registered as: alice

alice> LIST
USERS:
alice (online)
bob (offline)

alice> VIEW
FILES:
file1.txt
docs/file3.txt

alice> VIEW -l
FILES:
file1.txt [owner=alice, size=1024, ss=0]
docs/file3.txt [owner=alice, size=512, ss=0]

alice> INFO file1.txt
File: file1.txt
Owner: alice
Size: 1024 bytes
Last Access: Mon Oct 21 10:30:15 2025

alice> ADDACCESS -R file1.txt bob
OK: Access granted

alice> QUIT
```

## Files Generated

- `bin/naming_server` - NS executable
- `bin/test_client` - Test client executable
- `bin/test_ss` - Test SS executable
- `ns_log.txt` - Operation log
- `obj/*.o` - Object files (can be deleted with `make clean`)

## Support

For issues or questions:
1. Check `ns_log.txt` for detailed operation logs
2. Verify protocol format matches documentation
3. Test with provided test programs first
4. Review access control rules

## Summary

This Naming Server implementation provides:
- ✅ User management and tracking
- ✅ Storage server registry
- ✅ File indexing with metadata
- ✅ Access control (read/write permissions)
- ✅ Three operation modes (Direct, Offload, Modulator)
- ✅ Thread-safe concurrent handling
- ✅ Comprehensive logging
- ✅ Test programs for validation

It's a simplified but functional implementation suitable for understanding the core concepts of a distributed file system's naming server.
