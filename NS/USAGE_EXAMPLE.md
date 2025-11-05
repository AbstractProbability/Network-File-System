# NS File Manager - Usage Guide

## Overview
The NS File Manager provides three main components:
1. **Active Users List** - Tracks currently connected users
2. **File Path List** - Maps files to storage servers that have them
3. **File Request Handler** - Retrieves active SS for a file with permissions

## Integration with Name Server

### 1. Initialization (in main())
```c
// Already added to nameserver.c
G_active_users = create_active_users_list();
G_file_paths = create_file_path_list();
```

### 2. Client Registration (in handle_client_registration())
```c
// Already added to nameserver.c
add_active_user(G_active_users, username);
```

### 3. Client Disconnection (in handle_connection())
```c
// Already added to nameserver.c
if (strcmp(disconnecting->type, "CLIENT") == 0) {
    remove_active_user(G_active_users, disconnecting->identifier);
}
```

### 4. Storage Server Registration (TO BE ADDED to handle_ss_registration())

When SS registers, it should send a list of files it has. Parse this and add to file path list:

**Message format from SS:**
```
[["type","SS_REGISTER"],["name","SS1"],["ip","127.0.0.1"],["nm_port","5000"],["client_port","5001"],["files","file1.txt,file2.txt,file3.txt"]]
```

```c
void handle_ss_registration(const char *message, int ss_socket_fd) {
    Message *msg = parse_message(message);
    if (!msg) return;
    
    char *ss_name = get_string_field(msg, "name");
    char *ss_ip = get_string_field(msg, "ip");
    int client_port = get_int_field(msg, "client_port");
    
    // TODO: Parse files list from message
    char *files_str = get_string_field(msg, "files");
    if (files_str && strlen(files_str) > 0) {
        // Split comma-separated file paths
        char *files_copy = strdup(files_str);
        char *token = strtok(files_copy, ",");
        
        while (token != NULL) {
            // Add each file to the file path list
            add_file_path(G_file_paths, token, ss_name, ss_ip, client_port);
            token = strtok(NULL, ",");
        }
        
        free(files_copy);
    }
    
    // Rest of registration code...
}
```

### 5. Using get_active_ss_for_file() for File Requests

When a client requests a file operation (READ/WRITE/DELETE/etc.):

```c
void handle_file_request(const char *message, int client_socket_fd) {
    Message *msg = parse_message(message);
    
    char *operation = get_string_field(msg, "type"); // "READ", "WRITE", etc.
    char *file_path = get_string_field(msg, "filename");
    char *username = get_string_field(msg, "username");
    
    // Get active SS and file info
    file_request_result *result = get_active_ss_for_file(G_file_paths, file_path);
    
    if (!result) {
        // File not found or no active SS
        char *error = create_response("ERROR", "File not found or no active storage server", 404);
        send_message(client_socket_fd, error);
        free(error);
        free_message(msg);
        return;
    }
    
    // Check permissions
    if (result->file_info) {
        int has_read = query_user_info(username, file_path, 'r');
        int has_write = query_user_info(username, file_path, 'w');
        
        if (strcmp(operation, "READ") == 0 && !has_read) {
            char *error = create_response("ERROR", "Permission denied: no read access", 403);
            send_message(client_socket_fd, error);
            free(error);
            free_file_request_result(result);
            free_message(msg);
            return;
        }
        
        if ((strcmp(operation, "WRITE") == 0 || strcmp(operation, "DELETE") == 0) && !has_write) {
            char *error = create_response("ERROR", "Permission denied: no write access", 403);
            send_message(client_socket_fd, error);
            free(error);
            free_file_request_result(result);
            free_message(msg);
            return;
        }
    }
    
    // Permissions OK - Send SS info to client
    Message *response = create_message();
    add_string_field(response, "status", "SUCCESS");
    add_string_field(response, "ss_name", result->ss_name);
    add_string_field(response, "ss_ip", result->ss_ip);
    add_number_field(response, "ss_port", result->ss_client_port);
    
    char *response_str = serialize_message(response);
    send_message(client_socket_fd, response_str);
    
    free(response_str);
    free_message(response);
    free_file_request_result(result);
    free_message(msg);
}
```

## Example Flow: Client READ Request

1. **Client sends (nested array format):**
   ```
   [["type","READ"],["username","alice"],["filename","/home/user/document.txt"]]
   ```

2. **NS calls:**
   ```c
   file_request_result *result = get_active_ss_for_file(G_file_paths, "/home/user/document.txt");
   ```

3. **get_active_ss_for_file() returns:**
   ```c
   result->ss_name = "SS1"
   result->ss_ip = "127.0.0.1"
   result->ss_client_port = 5001
   result->file_info = {
       owner: "alice",
       wc: 150,
       size: 1024,
       r_access_users: ["alice", "bob"],
       w_access_users: ["alice"],
       ...
   }
   ```

4. **NS checks permissions:**
   ```c
   has_read = query_user_info("alice", "/home/user/document.txt", 'r'); // Returns 1
   ```

5. **NS sends to client (nested array format):**
   ```
   [["status","SUCCESS"],["ss_name","SS1"],["ss_ip","127.0.0.1"],["ss_port","5001"]]
   ```

6. **Client connects directly to SS1 at 127.0.0.1:5001**

## Debugging Functions

```c
// Print all active users
print_active_users(G_active_users);

// Print all file mappings
print_file_path_list(G_file_paths);

// Check if user is active
if (is_user_active(G_active_users, "alice")) {
    printf("Alice is online\n");
}
```

## Important Notes

1. **Info Files Location**: The `get_active_ss_for_file()` function currently calls `read_info_file()` which expects info files to be in the NS's `info_dir/`. This might need modification:
   - Option A: NS maintains a copy of all info files
   - Option B: Request info file from SS when needed
   - Option C: Cache info files at NS after first request

2. **Thread Safety**: All functions are thread-safe and use mutexes internally. Safe to call from multiple connection handler threads.

3. **SS Health**: The `is_alive` flag is automatically set to 0 when SS disconnects (in handle_connection). When checking heartbeat timeouts, you should also call:
   ```c
   mark_ss_status(G_file_paths, ss_name, 0); // Mark as DOWN
   ```

4. **Cleanup**: All structures are freed in main() at shutdown.

## TODO for Complete Integration

1. ✅ Initialize data structures in main()
2. ✅ Add/remove users on connect/disconnect
3. ⚠️ Parse file list from SS registration message
4. ⚠️ Implement file request handlers (READ, WRITE, DELETE, etc.)
5. ⚠️ Integrate permissions checking
6. ⚠️ Handle info file synchronization between NS and SS
7. ⚠️ Update file list when files are created/deleted
8. ⚠️ Mark SS status based on heartbeat failures
