# Naming Server - Quick Reference

## Build Commands
```bash
cd /home/akshatg/cp/course-project/cp-code/NamingServer
make              # Build everything
make clean        # Clean build artifacts
make run          # Build and run NS
```

## Run Commands
```bash
# Start Naming Server
./bin/naming_server

# Run automated test
./test.sh

# Test Storage Server registration
./bin/test_ss <ns_ip> <ns_port> <my_ip> <nm_port> <client_port>

# Test Client
./bin/test_client <ns_ip> <ns_port> <username>
```

## Client Commands Quick Reference

### View & List
```
LIST              # List all users
VIEW              # Files you can access
VIEW -a           # All files (with access status)
VIEW -l           # Files with details
VIEW -al          # All files with details
```

### File Operations
```
INFO <file>       # Show file metadata
READ <file>       # Get SS info for reading
WRITE <file> <#>  # Get SS info for writing
STREAM <file>     # Get SS info for streaming
CREATE <file>     # Create new file
DELETE <file>     # Delete file (owner only)
UNDO <file>       # Undo last change
```

### Access Control (Owner Only)
```
ADDACCESS -R <file> <user>    # Grant read access
ADDACCESS -W <file> <user>    # Grant write access
REMACCESS <file> <user>       # Remove all access
```

### Meta Commands
```
HELP              # Show command help
QUIT              # Exit client
```

## Protocol Quick Reference

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

### Response Types
```
OK                              # Success
OK: <message>                   # Success with message
ERROR: <message>                # Error with description
SS_INFO <ip> <port>             # SS info for offload operations
FORWARD_TO_SS <ip> <port>       # SS info for modulator operations
USERS:\n<user> (online)\n...    # User list
FILES:\n<file>\n...             # File list
File: <details>...              # File metadata
```

## Files & Directories

```
NamingServer/
├── bin/
│   ├── naming_server      # NS executable
│   ├── test_client        # Test client
│   └── test_ss           # Test SS
├── include/
│   └── ns.h              # Header file
├── src/
│   ├── main.c            # Entry point
│   ├── ns_server.c       # Core functions
│   └── handlers.c        # Request handlers
├── obj/                  # Object files
├── test_client.c         # Test client source
├── test_ss.c            # Test SS source
├── test.sh              # Test script
├── makefile             # Build config
├── README.md            # Overview
├── USAGE.md             # Detailed guide
├── SUMMARY.md           # Implementation summary
├── QUICKREF.md          # This file
└── ns_log.txt           # Log file (generated)
```

## Common Error Messages

| Error | Meaning | Solution |
|-------|---------|----------|
| `ERROR: User limit reached` | Too many users | Increase MAX_USERS |
| `ERROR: File not found` | File doesn't exist | Check filename |
| `ERROR: Permission denied` | No access rights | Request access from owner |
| `ERROR: Only owner can modify access` | Not file owner | Must be owner to grant access |
| `ERROR: Only owner can delete` | Not file owner | Must be owner to delete |
| `ERROR: Unknown command` | Invalid command | Check command syntax |

## Configuration (in ns.h)

```c
MAX_FILENAME 256              // Max file path length
MAX_USERNAME 64               // Max username length  
MAX_IP 16                     // Max IP address length
MAX_USERS 100                 // Max number of users
MAX_STORAGE_SERVERS 10        // Max storage servers
MAX_FILES_PER_SS 1000         // Max files per SS
LOG_FILE "ns_log.txt"         // Log file name
```

## Access Control Rules

1. **File Owner**: Automatic full access (read + write)
2. **Other Users**: Need explicit permission from owner
3. **Grant/Revoke**: Only owner can modify access
4. **Delete**: Only owner can delete files
5. **Create**: Creator becomes owner

## Port Assignment

- **NS Port**: Dynamically assigned (printed on startup)
- **SS NM Port**: Specified during SS registration
- **SS Client Port**: Specified during SS registration
- **Client Port**: Dynamically assigned on connection

## Thread Safety

- One thread per connection
- Global mutex protects:
  - User list
  - File index
  - Storage server list
  - Access permissions
  - Log file writes

## Logging

All operations logged to `ns_log.txt`:
```
[Timestamp] Message
```

View log:
```bash
tail -f ns_log.txt          # Follow log in real-time
tail -50 ns_log.txt         # Last 50 lines
grep alice ns_log.txt       # Filter by username
grep ERROR ns_log.txt       # Show only errors
```

## Example Testing Session

```bash
# Terminal 1: Start NS
$ ./bin/naming_server
Naming Server started on port 41234

# Terminal 2: Register SS with 3 files
$ ./bin/test_ss 127.0.0.1 41234 127.0.0.1 6000 6001
# Files: file1.txt (alice), file2.txt (bob), docs/file3.txt (alice)

# Terminal 3: Connect as alice
$ ./bin/test_client 127.0.0.1 41234 alice
alice> LIST
alice> VIEW
alice> INFO file1.txt
alice> ADDACCESS -R file1.txt bob
alice> QUIT

# Terminal 4: Connect as bob
$ ./bin/test_client 127.0.0.1 41234 bob
bob> VIEW
bob> READ file1.txt
bob> DELETE file1.txt    # Will fail - not owner
bob> QUIT
```

## Troubleshooting

**Problem**: NS won't start
- Check port availability
- Verify log file permissions
- Check disk space

**Problem**: Client can't connect
- Verify NS is running
- Check correct port number
- Test with: `telnet 127.0.0.1 <port>`

**Problem**: Permission denied
- Verify owner with `INFO <file>`
- Check access with `VIEW -l`
- Request access from owner

**Problem**: File not found
- List files with `VIEW -a`
- Check filename spelling
- Verify SS is registered

## Performance Tips

- Use `VIEW -l` instead of multiple `INFO` calls
- Cache file list on client side
- Batch access grants when possible
- Monitor log file size

## Security Notes

- No authentication (usernames only)
- No encryption (plain text)
- No rate limiting
- Suitable for trusted networks only

## Integration with Other Components

### With Storage Server
- SS must send REGISTER_SS on startup
- SS must provide file list
- SS should handle offloaded operations (READ, WRITE, STREAM)
- SS should acknowledge modulator operations (CREATE, DELETE, UNDO)

### With Client
- Client must REGISTER_CLIENT first
- Client receives SS info for direct operations
- Client should handle disconnection gracefully
- Client should respect access control

## Next Steps

1. Implement Storage Server component
2. Enhance Client with full functionality
3. Add inter-SS communication for modulator mode
4. Test full system integration
5. Add advanced features (redundancy, fault tolerance)

## Documentation Files

- **README.md**: Project overview and quick start
- **USAGE.md**: Comprehensive usage guide (11KB)
- **SUMMARY.md**: Implementation details (11KB)
- **QUICKREF.md**: This quick reference

## Support & Resources

- Check logs: `ns_log.txt`
- Review protocol: See USAGE.md
- Example code: test_client.c, test_ss.c
- Run tests: `./test.sh`
