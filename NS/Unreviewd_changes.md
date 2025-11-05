# NS File Manager Implementation - Summary

## What Was Implemented

### 1. Data Structures

#### Active Users List (`active_users_list`)
- Thread-safe linked list tracking currently connected users
- Functions: `add_active_user()`, `remove_active_user()`, `is_user_active()`
- Automatically updated on client connect/disconnect

#### File Path List (`file_path_list`)
- Maps file paths to storage servers that have them
- Supports multiple storage servers per file (for replication)
- Each SS has `is_alive` status based on heartbeat
- Functions: `add_file_path()`, `remove_file_path()`, `mark_ss_status()`

#### File Request Result (`file_request_result`)
- Contains SS connection info (name, IP, port)
- Includes file metadata (info_file structure with permissions)
- Returned by `get_active_ss_for_file()`

### 2. Core Function

```c
file_request_result* get_active_ss_for_file(file_path_list *list, const char *file_path);
```

**What it does:**
1. Searches for the file path in the NS index
2. Finds an active (alive) storage server that has the file
3. Reads the info file to get permissions and metadata
4. Returns all information in a single structure

**Returns NULL if:**
- File path not found in NS index
- No active storage servers have the file
- All storage servers are down

### 3. Integration with Name Server

Already integrated into `nameserver.c`:
- ✅ Global variables `G_active_users` and `G_file_paths` declared
- ✅ Initialization in `main()`
- ✅ Cleanup in `main()` shutdown
- ✅ User added on client registration
- ✅ User removed on client disconnect
- ✅ SS marked as DOWN on disconnect

### 4. File Structure

```
NS/
├── ns_filemanager.h      # Header with all data structures and function declarations
├── ns_filemanager.c      # Implementation of all functions
├── test_filemanager.c    # Test program demonstrating usage
├── USAGE_EXAMPLE.md      # Comprehensive usage guide with examples
└── test_filemanager      # Compiled test executable
```

## How to Use

### Basic Usage Pattern

```c
// When client requests a file operation:
file_request_result *result = get_active_ss_for_file(G_file_paths, file_path);

if (!result) {
    // File not found or no active SS - send error to client
    return;
}

// Check permissions
if (result->file_info) {
    int has_access = query_user_info(username, file_path, 'r'); // or 'w', 'x'
    if (!has_access) {
        // Send permission denied error
        free_file_request_result(result);
        return;
    }
}

// Send SS info to client: result->ss_ip, result->ss_client_port
// Client will connect directly to SS

free_file_request_result(result);
```

## What Still Needs to Be Done

### Immediate Next Steps:

1. **Parse SS File List on Registration**
   - When SS registers, it sends `"files": ["path1", "path2", ...]`
   - Need to parse this array and call `add_file_path()` for each file
   - Location: `handle_ss_registration()` in `nameserver.c`

2. **Implement File Operation Handlers**
   - `handle_read_request()` - Use `get_active_ss_for_file()`, check permissions, send SS info
   - `handle_write_request()` - Same pattern
   - `handle_delete_request()` - Same pattern
   - `handle_create_request()` - Create file, update file list
   - `handle_info_request()` - Return file_info directly
   - `handle_list_request()` - Return list of files based on user permissions

3. **Update File List on Create/Delete**
   - When file is created: `add_file_path(G_file_paths, new_path, ss_name, ss_ip, port)`
   - When file is deleted: `remove_file_path(G_file_paths, deleted_path)`

4. **Info File Synchronization**
   - Decide strategy: NS maintains copy, or requests from SS?
   - Currently assumes info files are accessible from NS side

5. **Heartbeat Integration**
   - In `check_heartbeat_timeouts()`, when SS fails heartbeat:
     ```c
     mark_ss_status(G_file_paths, ss_name, 0);
     ```

## Test Results

All tests passed successfully:
- ✅ Active users list operations
- ✅ File path list operations  
- ✅ Multiple SS per file
- ✅ Failover (returns different SS when first is down)
- ✅ Returns NULL when no active SS
- ✅ Thread-safe operations

## Example Workflow

**Scenario: Alice wants to READ /home/user/notes.txt**

1. Client sends (nested array format): 
   ```
   [["type","READ"],["username","alice"],["filename","/home/user/notes.txt"]]
   ```

2. NS calls: `get_active_ss_for_file(G_file_paths, "/home/user/notes.txt")`

3. Function returns:
   - `ss_name: "SS1"`
   - `ss_ip: "127.0.0.1"`
   - `ss_port: 5001`
   - `file_info: {owner:"alice", r_access_users:["alice","bob"], ...}`

4. NS checks: `query_user_info("alice", "/home/user/notes.txt", 'r')` → returns 1 (has access)

5. NS sends to client (nested array format): 
   ```
   [["status","SUCCESS"],["ss_ip","127.0.0.1"],["ss_port","5001"]]
   ```

6. Client connects directly to SS1 at port 5001 and requests the file

## Thread Safety

All functions use mutexes:
- `active_users_list->lock` - Protects user list operations
- `file_path_list->lock` - Protects file list operations
- `file_entry->lock` - Protects per-file SS list operations

Safe to call from multiple connection handler threads simultaneously.

## Performance

- User lookup: O(N) where N = number of active users
- File lookup: O(M) where M = number of tracked files
- SS selection: O(K) where K = number of SS per file (typically small, 1-3)

Can be optimized with hash tables if needed for large scale.

## Next Integration Point

Start by implementing `handle_read_request()` in nameserver.c as a template:

```c
void handle_read_request(const char *message, int client_socket_fd) {
    Message *msg = parse_message(message);
    char *file_path = get_string_field(msg, "filename");
    char *username = get_string_field(msg, "username");
    
    // Get SS and permissions
    file_request_result *result = get_active_ss_for_file(G_file_paths, file_path);
    if (!result) {
        send_error(client_socket_fd, "File not found");
        free_message(msg);
        return;
    }
    
    // Check read permission
    if (result->file_info && !query_user_info(username, file_path, 'r')) {
        send_error(client_socket_fd, "Permission denied");
        free_file_request_result(result);
        free_message(msg);
        return;
    }
    
    // Send SS info to client
    send_ss_info(client_socket_fd, result->ss_ip, result->ss_client_port);
    
    free_file_request_result(result);
    free_message(msg);
}
```

This becomes the pattern for all file operations.
