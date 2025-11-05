# File Request Implementation

## Overview
This document describes the implementation of the file request functionality in the Name Server (NS), which allows clients to request file operations and receive the active storage server details after permission verification.

## Components Implemented

### 1. Storage Server File Registration (NS/nameserver.c)

**Function**: `handle_ss_registration()`

**Changes**: 
- Now parses the "files" field from SS_REGISTER messages
- Extracts comma/newline separated file paths from the registration
- Calls `add_file_path()` for each file to populate the global file path list
- Logs the number of files registered per storage server

**Flow**:
```
SS sends registration → NS receives → Parse "files" field → For each file path:
    add_file_path(G_file_paths, filepath, ss_name, ss_ip, client_port)
```

### 2. File Request Handler (NS/nameserver.c)

**Function**: `handle_file_request()`

**Purpose**: Handle client requests to access files on storage servers

**Input Message Format**:
```
Message with fields:
- type: "FILE_REQUEST"
- username: client username
- filepath: requested file path
- operation: "READ", "WRITE", or "EXECUTE"
```

**Processing Steps**:
1. Parse and validate the request message
2. Call `get_active_ss_for_file(G_file_paths, filepath)` to find an active storage server
3. Check if file exists and has an active storage server
4. Convert operation to access_type character ('r', 'w', or 'x')
5. Call `query_user_info(username, filepath, access_type)` to verify permissions
6. If permission granted, return storage server details to client
7. If permission denied or file not found, return error

**Response Message Format** (on success):
```
Message with fields:
- type: "FILE_REQUEST_RESPONSE"
- status: "SUCCESS"
- ss_name: storage server name
- ss_ip: storage server IP address
- ss_port: storage server client port
- filepath: the requested file path
```

**Response Format** (on error):
```
Message with fields:
- type: "RESPONSE"
- status: "ERROR"
- message: error description
- code: 0
```

### 3. Message Routing (NS/nameserver.c)

**Function**: `handle_incoming_message()`

**Changes**: Added routing for "FILE_REQUEST" message type
```c
else if (strcmp(msg_type, "FILE_REQUEST") == 0) {
    handle_file_request(message, sender_socket_fd);
}
```

## Data Flow

### File Registration Flow
```
Storage Server                    Name Server
     |                                 |
     |---- SS_REGISTER message ------->|
     |    (includes "files" field)     |
     |                                 |
     |                                 |--- Parse files list
     |                                 |--- Add each file to G_file_paths
     |                                 |    with SS details (name, ip, port)
     |                                 |
     |<---- SUCCESS response -----------|
```

### File Request Flow
```
Client                          Name Server                     Info File
  |                                 |                               |
  |--- FILE_REQUEST --------------->|                               |
  |    (username, filepath, op)     |                               |
  |                                 |                               |
  |                                 |--- get_active_ss_for_file()  |
  |                                 |<-- SS details + file_info ----|
  |                                 |                               |
  |                                 |--- query_user_info() -------->|
  |                                 |<-- permission result ---------|
  |                                 |                               |
  |<-- FILE_REQUEST_RESPONSE -------|  (if permission granted)      |
  |    (ss_name, ss_ip, ss_port)    |                               |
  |                                 |                               |
  |--- Connect to Storage Server -->|                               |
```

## Functions Used

### From ns_filemanager.c:
- `add_file_path()` - Adds a file path and its associated storage server to the global list
- `get_active_ss_for_file()` - Returns an active storage server for a given file path
- `free_file_request_result()` - Frees the result structure

### From infofile.c:
- `query_user_info()` - Checks if a user has specific access permissions for a file
  - Parameters: username, filepath, access_type ('r', 'w', or 'x')
  - Returns: 1 if user has permission, 0 if not, -1 on error

## Integration Points

### Client Side (Client/client.c)
Clients should send FILE_REQUEST messages with:
- username (from registration)
- filepath (file they want to access)
- operation ("READ", "WRITE", or "EXECUTE")

Then parse the FILE_REQUEST_RESPONSE to get storage server details and connect.

### Storage Server Side (SS/storageserver.c)
Storage servers should:
- Send a comma-separated or newline-separated list of file paths in the "files" field during SS_REGISTER
- Keep the list updated with actual files they're hosting

**Current Issue**: The "files" array in SS registration is currently empty. The storage server needs to populate this with actual file paths before registering.

## Error Handling

The implementation handles the following error cases:
1. Invalid request format
2. Missing required fields (username, filepath, operation)
3. File not found in NS index
4. No active storage server available for file
5. Invalid operation type (not READ/WRITE/EXECUTE)
6. Permission check failed (info file error)
7. Permission denied (user doesn't have access)

All errors return descriptive error messages to the client.

## Thread Safety

- All access to `G_file_paths` is protected by mutexes in ns_filemanager.c
- The `query_user_info()` function reads info files (thread-safe as read-only)
- File request handling is done in per-connection threads

## Next Steps

To complete the implementation:

1. **Storage Server**: Update `create_ss_registration_message()` in SS/storageserver.c to populate the "files" array with actual file paths
2. **Client**: Implement FILE_REQUEST message creation and response handling
3. **Testing**: Test the complete flow with actual files and permissions
4. **Info Files**: Ensure info files exist for all registered files with proper permissions

## Example Usage

### Client Request:
```c
Message *req = create_message();
add_string_field(req, "type", "FILE_REQUEST");
add_string_field(req, "username", "alice");
add_string_field(req, "filepath", "/data/file1.txt");
add_string_field(req, "operation", "READ");
char *req_str = serialize_message(req);
send_message(ns_socket, req_str);
```

### NS Processing:
- Checks if file exists in G_file_paths
- Finds active storage server
- Checks alice's read permission for /data/file1.txt
- Returns SS details if permission granted

### Client receives:
```
type: FILE_REQUEST_RESPONSE
status: SUCCESS
ss_name: SS1
ss_ip: 192.168.1.100
ss_port: 8001
filepath: /data/file1.txt
```

Client can now connect to 192.168.1.100:8001 to access the file.
