# In-Memory File Cleanup Fix

## Problem Description

### Observed Bug
When a file was deleted and then recreated with the same name, the WRITE operation would show content from the previously deleted file, even though the file was verifiably deleted from disk.

### Root Cause
The Storage Server maintains an in-memory list of `OpenFile` structures (`g_open_files_list`) for files that are currently being accessed. When a file is loaded into memory via `get_open_file()`, it's added to this global list. However, when `handle_op_delete()` deleted the file from disk, it **never removed the in-memory representation** from this list.

### Sequence of Events (Bug):
```
1. CREATE file1                → File created on disk
2. WRITE file1 sent 0          → get_open_file() loads file1 into memory
                               → file1 added to g_open_files_list
                               → In-memory: file1 has sentence data
3. DELETE file1                → File deleted from disk
                               → BUG: file1 still in g_open_files_list!
4. CREATE file1                → New empty file1 created on disk
5. WRITE file1 sent 0          → get_open_file() called
                               → Finds OLD file1 in g_open_files_list
                               → Returns stale in-memory data!
6. User sees old content       → Old sentences still present
```

### Why Only WRITE was Affected
- **READ operations**: Read directly from disk file, so they saw the correct (new) content
- **WRITE operations**: Used `get_open_file()` which returned the stale in-memory structure
- **STREAM operations**: Also read from disk, not affected

## Solution Implementation

### 1. Created `remove_open_file()` Function

Added a new function to properly remove and clean up an `OpenFile` from the global list:

```c
void remove_open_file(const char* path) {
    pthread_mutex_lock(&g_open_files_mutex);
    
    OpenFile* current = g_open_files_list;
    OpenFile* prev = NULL;
    
    // Search for the file in the list
    while (current != NULL) {
        if (strcmp(current->path, path) == 0) {
            // Remove from linked list
            if (prev == NULL) {
                g_open_files_list = current->next;
            } else {
                prev->next = current->next;
            }
            
            // Free all memory
            printf("SS: Removing in-memory representation of '%s'\n", path);
            
            // Destroy the rwlock
            pthread_rwlock_destroy(&current->global_lock);
            
            // Free sentence list (includes words, whitespace, locks)
            free_sentence_list(current->sentences);
            
            // Free the structure itself
            free(current);
            
            printf("SS: In-memory file '%s' cleaned up successfully\n", path);
            break;
        }
        prev = current;
        current = current->next;
    }
    
    pthread_mutex_unlock(&g_open_files_mutex);
}
```

**Key Features:**
- **Thread-safe**: Acquires `g_open_files_mutex` before modifying the list
- **Complete cleanup**: Frees all allocated memory:
  - Destroys the `pthread_rwlock_t global_lock`
  - Frees all sentences (which frees all words and whitespace)
  - Destroys all sentence-level mutexes
  - Frees the `OpenFile` structure
- **Logging**: Prints confirmation messages for debugging

### 2. Updated `handle_op_delete()`

Modified the delete function to call `remove_open_file()` after files are deleted from disk:

```c
ErrorCode handle_op_delete(const char* path) {
    // ... check locks, delete files from disk ...
    
    // Release the lock if we acquired it
    if (of != NULL) {
        pthread_rwlock_unlock(&of->global_lock);
        printf("SS: DELETE released lock for '%s'\n", path);
        
        // CRITICAL FIX: Remove the file from in-memory list
        // This must be done AFTER releasing the lock to avoid deadlock
        // (remove_open_file acquires g_open_files_mutex)
        remove_open_file(path);
    }
    
    printf("SS: Successfully deleted all files for '%s'\n", path);
    return ERR_OK;
}
```

**Critical Ordering:**
1. Release `global_lock` first
2. Then call `remove_open_file()`
3. This prevents deadlock (different lock order)

### 3. Added Function Prototype

Updated `ss/ss.h` to include the new function:

```c
OpenFile* get_open_file(const char* path);
void remove_open_file(const char* path);  // NEW
```

## Fixed Behavior

### Sequence of Events (Fixed):
```
1. CREATE file1                → File created on disk
2. WRITE file1 sent 0          → get_open_file() loads file1 into memory
                               → file1 added to g_open_files_list
                               → In-memory: file1 has sentence data
3. DELETE file1                → File deleted from disk
                               → FIX: remove_open_file() called
                               → file1 removed from g_open_files_list
                               → All memory freed (sentences, locks, etc.)
4. CREATE file1                → New empty file1 created on disk
5. WRITE file1 sent 0          → get_open_file() called
                               → NOT found in g_open_files_list
                               → Loads NEW file1 from disk (empty)
                               → Adds fresh OpenFile to list
6. User sees correct content   → Empty file, no old sentences
```

## Memory Management

### What Gets Freed
When `remove_open_file()` is called:

1. **Global Lock**: `pthread_rwlock_destroy(&current->global_lock)`
2. **Sentence List**: `free_sentence_list(current->sentences)` which frees:
   - Leading whitespace for each sentence
   - Lock holder username (if any)
   - All word nodes and their trailing whitespace
   - Sentence-level mutexes (`pthread_mutex_destroy(&s->lock)`)
   - Sentence nodes themselves
3. **OpenFile Structure**: `free(current)`

### Memory Leak Prevention
- All dynamically allocated memory is properly freed
- All pthread primitives (rwlocks, mutexes) are destroyed
- No dangling pointers in the global list

## Thread Safety Analysis

### Lock Ordering
1. **In get_open_file()**: Acquires `g_open_files_mutex`
2. **In handle_op_delete()**: 
   - First acquires `of->global_lock` (rwlock in write mode)
   - Releases `of->global_lock`
   - Then calls `remove_open_file()` which acquires `g_open_files_mutex`

**Why This Order Matters:**
- If we called `remove_open_file()` while still holding `global_lock`, and another thread in `get_open_file()` held `g_open_files_mutex` and tried to access the file, we could deadlock
- Current order is safe: release all locks before acquiring new ones

### Race Condition Prevention
- **DELETE during WRITE**: Global lock prevents deletion while commit is in progress
- **New WRITE after DELETE**: `remove_open_file()` ensures stale data is gone before new operations can start
- **Concurrent ACCESS**: `g_open_files_mutex` serializes access to the list

## Testing Verification

### Test Case: Delete and Recreate
```bash
# Terminal 1
./client alice
CREATE testfile bob
WRITE testfile 0
Hello world.
COMMIT
INFO testfile
# Should show: "Hello world."

DELETE testfile

CREATE testfile bob  
WRITE testfile 0
New content.
COMMIT
INFO testfile
# Should show: "New content." (NOT "Hello world.")
```

### Expected Log Output
```
SS: Removing in-memory representation of 'testfile'
SS: In-memory file 'testfile' cleaned up successfully
SS: Successfully deleted all files for 'testfile'
```

## Files Modified

1. **ss/ss_ops.c**
   - Added `remove_open_file()` function (40 lines)
   - Modified `handle_op_delete()` to call `remove_open_file()`

2. **ss/ss.h**
   - Added `void remove_open_file(const char* path);` prototype

## Compilation
✅ Compiles without errors or warnings

## Impact Assessment

### Performance
- **Minimal overhead**: Only called during DELETE (rare operation)
- **Memory benefit**: Prevents memory leaks from deleted files

### Correctness
- **Critical fix**: Prevents data corruption from stale in-memory data
- **No regression**: Doesn't affect READ, STREAM, or other operations

### Edge Cases Handled
- **File not in memory**: `remove_open_file()` safely handles when file isn't in list
- **Multiple deletes**: Idempotent - second call has no effect
- **Concurrent operations**: Proper locking prevents races

## Summary

The bug was caused by the Storage Server caching file structures in memory but never removing them when files were deleted. This caused newly created files with the same name to inherit stale in-memory data. The fix adds proper cleanup via `remove_open_file()` which removes the entry from the global list and frees all associated memory.
