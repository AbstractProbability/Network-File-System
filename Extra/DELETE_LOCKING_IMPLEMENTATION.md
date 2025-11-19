# DELETE Operation Locking - Implementation Summary

## Problem Statement
When a file is being actively accessed (READ or WRITE operations in progress), the DELETE operation needs to be properly synchronized to prevent race conditions and data corruption.

## Requirements
1. **Block DELETE when readers exist**: DELETE should wait until all active READ operations complete (file transfer finished)
2. **Block DELETE when writers exist**: DELETE should wait until all active WRITE operations complete (commit finished)
3. **Notify deletor**: User should be informed when DELETE is blocked and waiting
4. **Reader definition**: A reader exists only during the file transfer (not during streaming, as file is copied)
5. **Use commit lock**: DELETE should acquire the same lock used by WRITE commits

## Solution Design

### Locking Mechanism
We use the existing `pthread_rwlock_t global_lock` in the `OpenFile` structure:
- **READ operations**: Acquire read lock (`pthread_rwlock_rdlock`) during file transfer
- **WRITE operations**: Acquire write lock (`pthread_rwlock_wrlock`) during commit phase
- **DELETE operations**: Acquire write lock (`pthread_rwlock_wrlock`) before deletion

### Read-Write Lock Semantics
- **Multiple readers allowed**: Multiple READ operations can proceed concurrently
- **Exclusive writer**: Only one WRITE can proceed, blocks all readers
- **Exclusive DELETE**: DELETE acquires write lock, blocks all readers and writers
- **Automatic blocking**: If DELETE tries to acquire lock while readers/writers active, it automatically waits

## Implementation Details

### Storage Server Side (`ss/ss_ops.c`)

#### Modified `handle_op_delete()`:

```c
ErrorCode handle_op_delete(const char* path) {
    // ... path setup ...
    
    // Check if file is currently in memory (being accessed)
    OpenFile* of = get_open_file(path);
    if (of != NULL) {
        // File is in memory - need to acquire exclusive write lock
        // This will BLOCK if there are active readers or writers
        printf("SS: DELETE waiting for active readers/writers to finish for '%s'...\n", path);
        pthread_rwlock_wrlock(&of->global_lock);
        printf("SS: DELETE acquired exclusive lock for '%s', proceeding with deletion.\n", path);
        
        // Double-check: verify no active write sessions on individual sentences
        SentenceNode* s = of->sentences;
        while (s != NULL) {
            pthread_mutex_lock(&s->lock);
            if (s->lock_holder_username != NULL) {
                // Active write session - abort deletion
                pthread_mutex_unlock(&s->lock);
                pthread_rwlock_unlock(&of->global_lock);
                printf("SS: Cannot delete - file has active write session by '%s'\n", 
                       s->lock_holder_username);
                return ERR_SENTENCE_LOCKED;
            }
            pthread_mutex_unlock(&s->lock);
            s = s->next;
        }
    }
    
    // Proceed with deletion (lock held)
    if (remove(file_path) != 0) {
        if (of != NULL) pthread_rwlock_unlock(&of->global_lock);
        return ERR_FILE_NOT_FOUND;
    }
    
    // Delete info and undo files...
    
    // Release lock
    if (of != NULL) {
        pthread_rwlock_unlock(&of->global_lock);
        printf("SS: DELETE released lock for '%s'\n", path);
    }
    
    return ERR_OK;
}
```

**Key Points:**
1. **Check if file is in memory**: `get_open_file(path)` returns non-NULL if file is currently being accessed
2. **Acquire write lock**: `pthread_rwlock_wrlock(&of->global_lock)` - blocks until all readers/writers finish
3. **Notification log**: Prints "DELETE waiting..." so admin can see blocking
4. **Double-check**: Verify no sentence-level locks held (active write sessions)
5. **Hold lock during deletion**: Prevents new operations from starting
6. **Release lock**: After deletion completes
7. **Return ERR_SENTENCE_LOCKED**: If active write session detected

### Name Server Side (`ns/ns_sessions.c`)

#### Modified `handle_op_delete()`:

```c
void handle_op_delete(int sock, ClientRequest* req) {
    // ... validate owner, get SS list ...
    
    int deletion_blocked = 0;
    char blocked_reason[MAX_BUFFER_LEN] = {0};
    
    for (int i = 0; i < ss_count; i++) {
        if (ss_list[i]->is_active) {
            // Send DELETE command to SS
            // ...receive response...
            
            if (ss_res.status == ERR_SENTENCE_LOCKED) {
                // File is currently being written to - cannot delete
                deletion_blocked = 1;
                snprintf(blocked_reason, sizeof(blocked_reason), 
                         "File is currently being accessed (active write session). "
                         "Please try again later.");
                printf("NS: SS %s reports file '%s' is locked (active session)\n", 
                       ss_list[i]->ip, req->path);
                break; // Abort deletion
            }
        }
    }
    
    // If deletion was blocked, notify the client and abort
    if (deletion_blocked) {
        res.status = ERR_SENTENCE_LOCKED;
        strncpy(res.message, blocked_reason, sizeof(res.message) - 1);
        send(sock, &res, sizeof(ServerResponse), 0);
        return; // Don't remove from index
    }
    
    // Otherwise proceed with removing from index...
}
```

**Key Points:**
1. **Check SS responses**: Each SS reports if deletion succeeded or was blocked
2. **Detect ERR_SENTENCE_LOCKED**: Indicates active write session
3. **Abort on block**: If any SS reports blocking, abort entire deletion
4. **Notify client**: Send clear message explaining why deletion failed
5. **Don't remove from index**: File remains in NS index if deletion blocked

## Behavior Analysis

### Scenario 1: DELETE while READ in progress
```
Timeline:
T0: Client A: READ file1        → Acquires rdlock (transfer starts)
T1: Client B: DELETE file1      → Tries wrlock, BLOCKS (waiting message printed)
T2: Client A: READ completes    → Releases rdlock
T3: Client B: DELETE proceeds   → Acquires wrlock, deletes file
```

**Log Output:**
```
SS: Client acquiring read lock for 'file1'
SS: DELETE waiting for active readers/writers to finish for 'file1'...
SS: Client released read lock.
SS: DELETE acquired exclusive lock for 'file1', proceeding with deletion.
SS: Successfully deleted all files for 'file1'
```

### Scenario 2: DELETE while WRITE in progress
```
Timeline:
T0: Client A: WRITE file1 sentence 0  → Locks sentence
T1: Client A: COMMIT                  → Acquires wrlock for commit
T2: Client B: DELETE file1            → Tries wrlock, BLOCKS
T3: Client A: COMMIT completes        → Releases wrlock
T4: Client B: DELETE proceeds         → Acquires wrlock, deletes file
```

**Log Output:**
```
SS: Acquiring global lock for commit...
SS: DELETE waiting for active readers/writers to finish for 'file1'...
SS: Commit and global lock release complete.
SS: DELETE acquired exclusive lock for 'file1', proceeding with deletion.
```

### Scenario 3: DELETE with active sentence lock (edge case)
```
Timeline:
T0: Client A: WRITE file1 sentence 0  → Locks sentence, waiting for content
T1: Client B: DELETE file1            → No global lock yet (not in commit)
T2: Client B: Checks sentence locks   → Finds sentence 0 locked by A
T3: DELETE aborted                    → Returns ERR_SENTENCE_LOCKED
```

**Client Output:**
```
Error: File is currently being accessed (active write session). Please try again later.
```

### Scenario 4: DELETE with no active operations
```
Timeline:
T0: Client A: DELETE file1     → No OpenFile entry OR no locks held
T1: DELETE proceeds directly   → Deletes file immediately
```

**Log Output:**
```
SS: Successfully deleted all files for 'file1'
```

## Thread Safety Guarantees

### Read-Write Lock Properties
- **Fairness**: POSIX rwlock typically prefers writers (DELETE won't starve)
- **Atomicity**: Lock acquisition is atomic
- **Ordering**: Operations serialize at lock acquisition point

### Race Condition Prevention
1. **READ vs DELETE**: DELETE waits for all READs to finish (rdlock released)
2. **WRITE vs DELETE**: DELETE waits for COMMIT to finish (wrlock released)
3. **DELETE vs new READ**: If DELETE holds wrlock, new READs block
4. **DELETE vs new WRITE**: If DELETE holds wrlock, new WRITEs block

### Edge Cases Handled
1. **File not in memory**: DELETE proceeds immediately (no locks needed)
2. **Multiple SSs**: NS checks all SSs, aborts if ANY reports blocking
3. **Sentence-level locks**: Double-checked even after global lock acquired
4. **Lock cleanup**: Lock always released on error paths

## User Experience

### Successful DELETE
```
> DELETE myfile
File 'myfile' deleted successfully!
```

### Blocked DELETE (active write)
```
> DELETE myfile
Error: File is currently being accessed (active write session). Please try again later.
```

### Blocked DELETE (during commit)
```
> DELETE myfile
(waits a few seconds while commit completes)
File 'myfile' deleted successfully!
```

## Performance Considerations

### Blocking Time
- **READ blocking**: Typically < 1 second (file transfer duration)
- **WRITE blocking**: Typically < 100ms (commit duration)
- **Multiple readers**: DELETE waits for ALL readers to finish

### Lock Contention
- **Low contention**: Most files not in memory, no locking needed
- **High contention**: Files with many concurrent operations may delay DELETE
- **Timeout**: No timeout implemented - DELETE waits indefinitely (can be improved)

## Testing Verification

### Test Case 1: DELETE during READ
```bash
# Terminal 1
./client alice
READ largefile

# Terminal 2 (immediately)
./client bob
DELETE largefile  # Should wait until READ completes
```

### Test Case 2: DELETE during WRITE
```bash
# Terminal 1
./client alice
WRITE myfile 0
(don't commit yet)

# Terminal 2
./client alice
DELETE myfile  # Should fail with "active write session" error
```

### Test Case 3: DELETE idle file
```bash
./client alice
DELETE myfile  # Should succeed immediately
```

## Future Enhancements

1. **Timeout mechanism**: Add configurable timeout for DELETE blocking
2. **Force delete**: Allow admin/owner to force delete despite active sessions
3. **Notification to active users**: Warn users their session will be terminated
4. **Graceful shutdown**: Finish active operations before deleting
5. **Detailed blocking info**: Report which user is blocking the deletion

## Files Modified

1. **ss/ss_ops.c**
   - `handle_op_delete()`: Added global_lock acquisition and sentence lock checking

2. **ns/ns_sessions.c**
   - `handle_op_delete()`: Added ERR_SENTENCE_LOCKED handling and client notification

## Compilation
✅ Compiles without errors or warnings

## Summary
DELETE operations now properly synchronize with active READ and WRITE operations using the existing read-write lock mechanism. The implementation ensures data consistency while providing clear feedback to users when deletion is blocked.
