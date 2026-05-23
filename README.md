# Network File System - Usage Guide

## Table of Contents
- [System Overview](#system-overview)
- [Building the System](#building-the-system)
- [Starting the Name Server](#starting-the-name-server)
- [Starting Storage Servers](#starting-storage-servers)
- [Starting Clients](#starting-clients)
- [Client Commands Reference](#client-commands-reference)
  - [File Operations](#file-operations)
  - [Directory Operations](#directory-operations)
  - [Access Control](#access-control)
  - [Checkpoint Operations](#checkpoint-operations)
  - [Advanced Operations](#advanced-operations)
- [Write Mode Commands](#write-mode-commands)
- [Example Workflow](#example-workflow)

---

## System Overview

This is a distributed network file system with three main components:

1. **Name Server (NS)**: Central coordinator that manages metadata and file indexing
2. **Storage Servers (SS)**: Store actual file data and handle client read/write operations
3. **Clients**: User interfaces to interact with the file system

---

## Building the System

To compile all components:

```bash
# Build everything
make all

# Or build individually
make nameserver
make storageserver
make client

# Clean build artifacts
make clean
```

---

## Starting the Name Server

The Name Server must be started first before any Storage Servers or Clients can connect.

### Syntax
```bash
./nameserver
```

### Behavior
- Listens on port **8080** by default (defined in `common.h` as `NS_LISTEN_PORT`)
- Accepts connections from both Storage Servers and Clients
- Maintains file index and metadata
- Coordinates operations between clients and storage servers

### Example
```bash
# Terminal 1: Start Name Server
./nameserver
```

**Output:**
```
Name Server initializing...
Name Server listening on port 8080
```

---

## Starting Storage Servers

Storage Servers connect to the Name Server and provide file storage.

### Syntax
```bash
# Using localhost NS (default)
./storageserver <ss_root_dir>

# Using remote NS
./storageserver <ss_root_dir> <ns_ip> <ns_port>
```

### Arguments
- **ss_root_dir**: Root directory for this storage server's files
  - The directory structure will be created automatically:
    - `file_dir/` - Actual file contents
    - `info_dir/` - Metadata files
    - `undo_dir/` - Undo snapshots
    - `checkpoint_dir/` - Named checkpoints
  - The directory name is used to derive the SS identifier
  
- **ns_ip**: IP address of Name Server (optional, default: `127.0.0.1`)
- **ns_port**: Port of Name Server (optional, default: `8080`)

**Note:** Client and backup ports are **automatically assigned** by the OS (ephemeral ports). You don't need to specify them.
  - Each SS needs a unique port

### Examples

```bash
# Terminal 2: Start first storage server (localhost NS)
./storageserver tmp/ss1_root

# Terminal 3: Start second storage server (localhost NS)
./storageserver tmp/ss2_root

# Start storage server connecting to remote NS at 192.168.1.100:8080
./storageserver /data/storage1 192.168.1.100 8080
```

### Empty vs Non-Empty Storage Servers

When a storage server starts, it checks if `file_dir/` is empty:

- **Non-empty SS**: Has files, sends file index to NS on connection
- **Empty SS**: No files, will be paired with a non-empty SS for backup/replication

---

## Starting Clients

Clients connect to the Name Server to interact with files.

### Syntax
```bash
# Using localhost NS (default)
./client <username>

# Using remote NS
./client <username> <ns_ip> <ns_port>
```

### Arguments
- **username**: Unique identifier for this client
  - Used for authentication and access control
  - Files created by a user are owned by them
  
- **ns_ip**: IP address of Name Server (default: 127.0.0.1)
  
- **ns_port**: Port of Name Server (default: 8080)

### Examples

```bash
# Terminal 4: Start client for user "alice" (localhost NS)
./client alice

# Terminal 5: Start client for user "bob" (localhost NS)
./client bob

# Start client connecting to remote NS
./client charlie 192.168.1.100 8080
```

**Output:**
```
Connecting to NS at 127.0.0.1:8080...
[NS Response]: Welcome alice! You are now connected.
> 
```

---

## Client Commands Reference

Once connected, the client provides an interactive prompt (`>`) for commands.

### File Operations

#### `list`
List all registered users in the system.

**Syntax:**
```
list
```

**Example:**
```
> list
[NS Response]:
Registered users:
- alice
- bob
- charlie
```

---

#### `view [flags]`
View files you have access to.

**Syntax:**
```
view           # Simple view (just filenames)
view -a        # View all files (including others' files you have access to)
view -l        # Long format with metadata
view -al       # Both all files and long format
```

**Flags:**
- `-a`: Show all accessible files (not just your own)
- `-l`: Long format with word count, char count, timestamps, owner

**Examples:**
```
> view
[NS Response]:
--> myfile.txt
--> documents/report.txt
--> scripts/test.sh

> view -l
[NS Response]:
---------------------------------------------------------
| Filename             | Words | Chars | Last Access      | Owner      |
|----------------------|-------|-------|------------------|------------|
| myfile.txt           |   150 |   789 | 2025-11-19 10:30 | alice      |
| documents/report.txt |   500 |  3200 | 2025-11-19 09:15 | alice      |
---------------------------------------------------------
```

---

#### `create <path>`
Create a new empty file.

**Syntax:**
```
create <path>
```

**Behavior:**
- Creates file in all directories (file_dir, info_dir, undo_dir, checkpoint_dir)
- Parent directory must exist (won't auto-create directories)
- File is owned by the current user
- File is created on an available storage server

**Examples:**
```
> create myfile.txt
[NS Response]: File 'myfile.txt' created successfully.

> create documents/report.txt
[NS Response]: Error: Invalid path. Parent directory 'documents' does not exist.

> createfolder documents
> create documents/report.txt
[NS Response]: File 'documents/report.txt' created successfully.
```

---

#### `delete <path>`
Delete a file.

**Syntax:**
```
delete <path>
```

**Permissions:**
- Only the file owner can delete a file

**Examples:**
```
> delete myfile.txt
[NS Response]: File 'myfile.txt' deleted successfully.

> delete bob/file.txt
[NS Response]: Error: You don't have permission to delete this file.
```

---

#### `read <path>`
Read and display the contents of a file.

**Syntax:**
```
read <path>
```

**Permissions:**
- File owner always has read access
- Other users need explicit read permission

**Example:**
```
> read myfile.txt
[NS Response]: Redirecting to SS at 127.0.0.1:9001
--- File Content ---
This is the content of myfile.txt.
It can span multiple lines.
--- End of File ---
```

---

#### `stream <path>`
Stream file contents in real-time (similar to `read` but optimized for large files).

**Syntax:**
```
stream <path>
```

**Example:**
```
> stream largefile.txt
[NS Response]: Redirecting to SS at 127.0.0.1:9001
[Streaming content...]
```

---

#### `write <path> <sentence_index>`
Enter write mode to edit a specific sentence in a file.

**Syntax:**
```
write <path> <sentence_index>
```

**Behavior:**
- Enters write mode (prompt changes to `(writing)>`)
- Locks the specified sentence for editing
- Fetches current file contents from storage server
- Allows insertion of text before committing changes

**Example:**
```
> write myfile.txt 1
[NS Response]: Redirecting to SS at 127.0.0.1:9001 for WRITE
(writing)> l_view
Sentence 0: This is the first sentence.
Sentence 1: This is the second sentence. [LOCKED FOR EDITING]
Sentence 2: This is the third sentence.
(writing)> l_insert 1 Additional text to insert.
(writing)> commit
[Write committed successfully]
> 
```

---

#### `info <path>`
Display detailed metadata about a file.

**Syntax:**
```
info <path>
```

**Output includes:**
- File path and owner
- Creation, modification, and last access timestamps
- Size in bytes, word count, character count
- Access control lists (read, write, execute permissions)
- Number of storage servers hosting the file

**Example:**
```
> info myfile.txt
[NS Response]:
Path: myfile.txt
Owner: alice
Created: 2025-11-19 08:00:00
Modified: 2025-11-19 10:30:00
Accessed: 2025-11-19 10:30:00 by alice
Size: 789 | Words: 150 | Chars: 789
Read: Owner, bob
Write: Owner
Exec: Owner
Servers: 2 (active: 2)
```

---

### Directory Operations

#### `createfolder <path>`
Create a new directory.

**Syntax:**
```
createfolder <path>
```

**Behavior:**
- Creates directory in all four locations (file_dir, info_dir, undo_dir, checkpoint_dir)
- Only creates ONE level at a time (parent must exist)
- Does NOT recursively create multiple levels

**Examples:**
```
> createfolder documents
[NS Response]: Folder 'documents' created successfully.

> createfolder documents/reports
[NS Response]: Folder 'documents/reports' created successfully.

> createfolder a/b/c
[NS Response]: Error: Invalid path. Parent directory 'a/b' does not exist.
```

---

#### `viewfolder <path>`
List files in a specific folder.

**Syntax:**
```
viewfolder <path>
```

**Behavior:**
- Shows only files directly in the folder (not subdirectories)
- Only shows files you have access to

**Examples:**
```
> viewfolder documents
[NS Response]:
Files in 'documents':
report.txt
notes.txt
summary.txt

> viewfolder .
[NS Response]:
Files in '.':
myfile.txt
test.sh
```

---

#### `move <old_path> <new_path>`
Rename or move a file.

**Syntax:**
```
move <old_path> <new_path>
```

**Permissions:**
- Only the file owner can move/rename a file

**Behavior:**
- Renames file in all directories (file_dir, info_dir, undo_dir)
- Handles checkpoint files automatically
- Updates file index in name server

**Examples:**
```
> move myfile.txt newname.txt
[NS Response]: File moved from 'myfile.txt' to 'newname.txt'.

> move documents/report.txt documents/final_report.txt
[NS Response]: File moved from 'documents/report.txt' to 'documents/final_report.txt'.

> move bob/file.txt myfile.txt
[NS Response]: Error: You don't have permission to move this file.
```

---

### Access Control

#### `addaccess <path> <username> <type>`
Grant access to another user for a file.

**Syntax:**
```
addaccess <path> <username> <type>
```

**Arguments:**
- **path**: File path
- **username**: User to grant access to
- **type**: Access type: `R` (read), `W` (write), or `X` (execute)

**Permissions:**
- Only the file owner can grant access

**Examples:**
```
> addaccess myfile.txt bob R
[NS Response]: Read access granted to bob for 'myfile.txt'.

> addaccess scripts/test.sh charlie X
[NS Response]: Execute access granted to charlie for 'scripts/test.sh'.
```

---

#### `remaccess <path> <username> <type>`
Revoke access from a user.

**Syntax:**
```
remaccess <path> <username> <type>
```

**Arguments:**
- **path**: File path
- **username**: User to revoke access from
- **type**: Access type: `R`, `W`, or `X`

**Examples:**
```
> remaccess myfile.txt bob R
[NS Response]: Read access revoked from bob for 'myfile.txt'.
```

---

#### `reqaccess <path> <type>`
Request access to a file owned by another user.

**Syntax:**
```
reqaccess <path> <type>
```

**Arguments:**
- **path**: File path
- **type**: Access type: `R`, `W`, or `X`

**Behavior:**
- Creates an access request that the file owner can approve/reject
- Request is assigned a unique ID

**Example:**
```
> reqaccess alice/report.txt R
[NS Response]: Access request #5 created for 'alice/report.txt' (READ).
```

---

#### `reqlist`
List all pending access requests for files you own.

**Syntax:**
```
reqlist
```

**Example:**
```
> reqlist
[NS Response]:
Pending access requests:
#5: bob requests READ access to 'myfile.txt'
#6: charlie requests WRITE access to 'documents/report.txt'
```

---

#### `approve <request_id>`
Approve a pending access request.

**Syntax:**
```
approve <request_id>
```

**Example:**
```
> approve 5
[NS Response]: Access request #5 approved. Bob now has READ access to 'myfile.txt'.
```

---

#### `reject <request_id>`
Reject a pending access request.

**Syntax:**
```
reject <request_id>
```

**Example:**
```
> reject 6
[NS Response]: Access request #6 rejected.
```

---

### Checkpoint Operations

Checkpoints allow you to save named snapshots of files for later recovery.

#### `checkpoint <path> <tag>`
Create a named checkpoint of a file.

**Syntax:**
```
checkpoint <path> <tag>
```

**Arguments:**
- **path**: File to checkpoint
- **tag**: Name for this checkpoint (alphanumeric, no spaces)

**Permissions:**
- Only the file owner can create checkpoints

**Behavior:**
- Creates snapshot in `checkpoint_dir/<path>_<tag>`
- Copies both file content and metadata

**Example:**
```
> checkpoint myfile.txt v1
[NS Response]: Checkpoint 'v1' created for 'myfile.txt'.

> checkpoint documents/report.txt draft1
[NS Response]: Checkpoint 'draft1' created for 'documents/report.txt'.
```

---

#### `listcheckpoints <path>`
List all checkpoints for a file.

**Syntax:**
```
listcheckpoints <path>
```

**Example:**
```
> listcheckpoints myfile.txt
[NS Response]:
Checkpoints for 'myfile.txt':
- v1
- v2
- final
```

---

#### `viewcheckpoint <path> <tag>`
View the contents of a checkpoint without restoring it.

**Syntax:**
```
viewcheckpoint <path> <tag>
```

**Example:**
```
> viewcheckpoint myfile.txt v1
[NS Response]: Redirecting to SS at 127.0.0.1:9001
--- Checkpoint Content (v1) ---
This is the content as it was in checkpoint v1.
--- End of Checkpoint ---
```

---

#### `revert <path> <tag>`
Restore a file to a previous checkpoint.

**Syntax:**
```
revert <path> <tag>
```

**Permissions:**
- Only the file owner can revert

**Behavior:**
- Overwrites current file with checkpoint data
- Updates metadata

**Example:**
```
> revert myfile.txt v1
[NS Response]: File 'myfile.txt' reverted to checkpoint 'v1'.
```

---

#### `undo <path>`
Undo the last write operation on a file.

**Syntax:**
```
undo <path>
```

**Permissions:**
- Only the file owner can undo

**Behavior:**
- Restores file from `undo_dir` (automatic snapshot before each write)
- Only one level of undo (last write only)

**Example:**
```
> undo myfile.txt
[NS Response]: File 'myfile.txt' undone to previous version.
```

---

### Advanced Operations

#### `exec <path>`
Execute a bash script file on the name server and stream output.

**Syntax:**
```
exec <path>
```

**Permissions:**
- User must have execute (`X`) permission on the file

**Behavior:**
- NS fetches script from storage server
- Executes script using `/bin/bash`
- Streams output line-by-line to client
- Client is blocked during execution
- Other clients can continue operations

**Example:**
```
> exec scripts/test.sh
[NS Response]: Executing script 'scripts/test.sh'...
Starting script execution...
Current date: Tue Nov 19 10:30:00 UTC 2025
Listing files:
total 20
drwxr-xr-x  5 user user 4096 Nov 19 10:00 .
drwxr-xr-x 10 user user 4096 Nov 19 09:00 ..
System uptime:
 10:30:00 up 5 days,  3:15,  2 users,  load average: 0.15, 0.10, 0.05
Script execution complete!
[Execution finished]
```

---

#### `exit`
Disconnect from the name server and exit the client.

**Syntax:**
```
exit
```

**Example:**
```
> exit
[Exiting...]
```

---

## Write Mode Commands

When you enter write mode with `write <path> <index>`, the prompt changes to `(writing)>` and you can use these commands:

### `l_view`
View the current sentence structure.

**Syntax:**
```
l_view
```

**Example:**
```
(writing)> l_view
Sentence 0: This is the first sentence.
Sentence 1: This is the second sentence. [LOCKED FOR EDITING]
Sentence 2: This is the third sentence.
```

---

### `l_insert <index> <content>`
Insert text at the specified position within the locked sentence.

**Syntax:**
```
l_insert <index> <content>
```

**Arguments:**
- **index**: Character position within the sentence
- **content**: Text to insert (can contain spaces)

**Example:**
```
(writing)> l_insert 0 New text at the beginning.
(writing)> l_view
Sentence 1: New text at the beginning. This is the second sentence. [LOCKED FOR EDITING]
```

---

### `commit`
Save changes and exit write mode.

**Syntax:**
```
commit
```

**Behavior:**
- Sends modified sentence back to storage server
- Releases sentence lock
- Returns to normal mode

**Example:**
```
(writing)> commit
[Write committed successfully]
> 
```

---

### `abort`
Discard changes and exit write mode.

**Syntax:**
```
abort
```

**Behavior:**
- Discards all changes
- Releases sentence lock
- Returns to normal mode

**Example:**
```
(writing)> abort
[Write aborted]
> 
```

---

## Example Workflow

Here's a complete example workflow demonstrating the system:

```bash
# Terminal 1: Start Name Server
./nameserver

# Terminal 2: Start first storage server
./storageserver tmp/ss1_root

# Terminal 3: Start second storage server
./storageserver tmp/ss2_root

# Terminal 4: Alice's client
./client alice
```

**Alice's session:**
```
> list
[NS Response]:
Registered users:
- alice

> createfolder documents
[NS Response]: Folder 'documents' created successfully.

> create documents/report.txt
[NS Response]: File 'documents/report.txt' created successfully.

> write documents/report.txt 0
[NS Response]: Redirecting to SS at 127.0.0.1:9001 for WRITE
(writing)> l_insert 0 This is my first report.
(writing)> commit
[Write committed successfully]

> checkpoint documents/report.txt draft1
[NS Response]: Checkpoint 'draft1' created for 'documents/report.txt'.

> view -l
[NS Response]:
---------------------------------------------------------
| Filename             | Words | Chars | Last Access      | Owner      |
|----------------------|-------|-------|------------------|------------|
| documents/report.txt |     5 |    25 | 2025-11-19 10:45 | alice      |
---------------------------------------------------------
```

**Terminal 5: Bob's client**
```bash
./client bob
```

**Bob's session:**
```
> view
[NS Response]:
No files available.

> reqaccess documents/report.txt R
[NS Response]: Access request #1 created for 'documents/report.txt' (READ).
```

**Back to Alice:**
```
> reqlist
[NS Response]:
Pending access requests:
#1: bob requests READ access to 'documents/report.txt'

> approve 1
[NS Response]: Access request #1 approved. Bob now has READ access to 'documents/report.txt'.
```

**Back to Bob:**
```
> read documents/report.txt
[NS Response]: Redirecting to SS at 127.0.0.1:9001
--- File Content ---
This is my first report.
--- End of File ---
```

---

## Notes

- **Concurrent Operations**: Multiple clients can operate simultaneously
- **Fault Tolerance**: If a storage server goes down, files become temporarily unavailable but metadata is preserved
- **Session Persistence**: User list and access requests are saved in `nameserver_data/` and restored on NS restart
- **File Ownership**: Files are always owned by the user who created them
- **Path Format**: Use forward slashes `/` for paths (e.g., `documents/reports/file.txt`)
- **Case Sensitivity**: All paths and usernames are case-sensitive

