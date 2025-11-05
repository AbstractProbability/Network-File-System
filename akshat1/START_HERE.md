# For Your Teammate: Getting Started with the Communications Layer

## What This Is

This is a **complete communications layer** that handles ALL network programming for the Docs++ project. You don't need to write any socket code - just include one header file and call simple functions.

## What You Get

✅ **Complete abstraction** - No socket programming needed  
✅ **Thread-safe** - Handle multiple clients automatically  
✅ **Message passing** - Send/receive with one function call  
✅ **Connection tracking** - Know who's connected  
✅ **Error handling** - Built-in retry and timeout logic  
✅ **Logging** - Automatic logging of all network activity  
✅ **Examples** - Working code to learn from  

## Quick Start (5 minutes)

### 1. Build the library

```bash
cd /path/to/akshat
make all
```

You should see:
```
Built communications library: libcomm.a
Built test program: test_comm
```

### 2. Test it works

```bash
./test_comm
```

You should see all tests pass ✓

### 3. Include in your code

```c
#include "communications/comm.h"
```

### 4. Write your Name Server main()

```c
#include "communications/comm.h"
#include <stdio.h>

int main(int argc, char* argv[]) {
    // Get port from command line
    uint16_t port = (argc > 1) ? atoi(argv[1]) : 5000;
    
    // Initialize communications
    comm_init(ENTITY_NS, port);
    comm_set_log_file("ns.log");
    comm_start_listener(0);
    
    printf("Name Server running on port %d\n", port);
    
    // Main loop - receive and process messages
    while (1) {
        Message msg;
        int result = comm_receive_message(&msg, 5000);
        
        if (result == 0) {
            // Got a message! Process it
            printf("Received: %s from %s\n", 
                   operation_to_string(msg.operation),
                   msg.username);
            
            // Handle based on operation
            switch (msg.operation) {
                case OP_CREATE:
                    // Your CREATE logic here
                    comm_send_response(msg.source_ip, msg.source_port,
                                      OP_CREATE, "Created!", 8);
                    break;
                    
                case OP_READ:
                    // Your READ logic here
                    break;
                    
                // ... more operations
            }
        }
    }
    
    comm_shutdown();
    return 0;
}
```

### 5. Compile your code

```c
gcc ns_main.c -I. -L. -lcomm -pthread -o ns
```

### 6. Run your server

```bash
./ns 5000
```

Done! Your server is now accepting connections.

## Common Operations

### Send a response
```c
comm_send_response(msg.source_ip, msg.source_port, 
                  OP_CREATE, "Success", 7);
```

### Send an error
```c
comm_send_error(msg.source_ip, msg.source_port,
               STATUS_NOT_FOUND, "File not found");
```

### Register a Storage Server connection
```c
comm_register_connection("ss1", "192.168.1.20", 6000, ENTITY_SS);
```

### Get connection info
```c
char ip[MAX_IP_LEN];
uint16_t port;
comm_get_connection_info("ss1", ip, &port);
```

### Broadcast to all SSes
```c
const char* ss_ips[] = {"192.168.1.20", "192.168.1.21"};
uint16_t ss_ports[] = {6000, 6001};
comm_broadcast(ss_ips, ss_ports, 2, &msg);
```

## Message Structure

Every message has:
- `type` - REQUEST, RESPONSE, ERROR, etc.
- `operation` - CREATE, READ, WRITE, etc.
- `status` - OK, NOT_FOUND, FORBIDDEN, etc.
- `username` - Who sent it
- `source_ip` - Where it came from
- `source_port` - Port number
- `payload` - Data (up to 8KB)
- `payload_length` - Size of data

Access like this:
```c
Message msg;
comm_receive_message(&msg, 5000);

printf("From: %s\n", msg.username);
printf("Operation: %s\n", operation_to_string(msg.operation));
printf("Data: %.*s\n", msg.payload_length, msg.payload);
```

## Example: Handle CREATE

```c
void handle_create(Message* msg) {
    // Extract filename from payload
    char filename[512];
    strncpy(filename, msg->payload, msg->payload_length);
    filename[msg->payload_length] = '\0';
    
    // Check if file exists
    if (file_exists(filename)) {
        comm_send_error(msg->source_ip, msg->source_port,
                       STATUS_FILE_EXISTS, "File already exists");
        return;
    }
    
    // Create the file (your logic here)
    create_file(filename, msg->username);
    
    // Send success
    comm_send_response(msg->source_ip, msg->source_port,
                      OP_CREATE, "File created", 12);
}
```

## Example: Handle READ

```c
void handle_read(Message* msg) {
    char filename[512];
    strncpy(filename, msg->payload, msg->payload_length);
    filename[msg->payload_length] = '\0';
    
    // Check permission
    if (!has_read_permission(msg->username, filename)) {
        comm_send_error(msg->source_ip, msg->source_port,
                       STATUS_FORBIDDEN, "No permission");
        return;
    }
    
    // Find which SS has this file
    char ss_ip[MAX_IP_LEN];
    uint16_t ss_port;
    find_file_location(filename, ss_ip, &ss_port);
    
    // Tell client to connect to SS
    char response[MAX_PAYLOAD_LEN];
    snprintf(response, sizeof(response), "%s:%u", ss_ip, ss_port);
    
    comm_send_response(msg->source_ip, msg->source_port,
                      OP_READ, response, strlen(response));
}
```

## Need Help?

1. **Read INTEGRATION.md** - Detailed guide with many examples
2. **Look at examples/simple_ns.c** - Complete working example
3. **Look at tests/test_comm.c** - See how to use each function
4. **Check comm.h** - Full API documentation

## File Reference

- `communications/comm.h` - **Include this** in your code
- `libcomm.a` - **Link this** when compiling
- `INTEGRATION.md` - **Read this** for detailed guide
- `examples/simple_ns.c` - **Copy this** as starting point

## Common Mistakes to Avoid

❌ Don't forget to call `comm_init()` before anything else  
❌ Don't forget to call `comm_start_listener()` for servers  
❌ Don't forget to check return values (0 = success, -1 = error)  
❌ Don't forget to null-terminate strings from payload  
❌ Don't forget to call `comm_shutdown()` on exit  

✅ Always check return values  
✅ Use timeouts (don't wait forever)  
✅ Register connections for tracking  
✅ Log everything during development  
✅ Handle errors gracefully  

## Your Workflow

1. **Start simple** - Implement VIEW and LIST first (easy)
2. **Add CREATE/DELETE** - Basic request-response pattern
3. **Add READ/WRITE** - Client-SS direct connection
4. **Add SS registration** - Track storage servers
5. **Add heartbeat** - Monitor connections
6. **Add backup/replication** - Use broadcasting
7. **Test thoroughly** - One operation at a time

## Questions?

- Check the documentation files
- Look at the example code
- Run the test program
- Ask your teammate (the one who wrote this communications layer)

## Summary

**You don't need to learn socket programming.**

Just:
1. Include `communications/comm.h`
2. Call `comm_init()` and `comm_start_listener()`
3. Loop: receive message, process, send response
4. Call `comm_shutdown()` on exit

Everything else is handled for you! 🎉

---

**First step: Read INTEGRATION.md for detailed examples**

Good luck with the project!
