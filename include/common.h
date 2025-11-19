#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// --- Configuration ---
#define NS_LISTEN_PORT 8080
#define MAX_PATH_LEN 256
#define MAX_USERNAME_LEN 50
#define MAX_BUFFER_LEN 1024
#define HEARTBEAT_INTERVAL 5 // Seconds
#define MAX_WORD_LEN 100
#define MAX_COMMIT_LEN 4096 // Max size of a single write
#define SYNC_END_MARKER "SYNC_COMPLETE"
#define MAX_INFO_LEN 2048
#define MAX_ACCESS_LIST_LEN 512

// --- Initial Connection Type ---
typedef enum {
    CONN_CLIENT,
    CONN_SS
} ConnectionType;

// --- Operation Codes (Client to NS) ---
typedef enum {
    // Type 1
    OP_VIEW,
    OP_LIST,
    OP_ADDACCESS,
    OP_REMACCESS,
    OP_REQACCESS,
    OP_REQLIST,
    OP_APPROVE,
    OP_REJECT,
    OP_INFO,
    // Type 2
    OP_CREATE,
    OP_DELETE,
    OP_CREATEFOLDER,
    OP_MOVE,
    OP_VIEWFOLDER,
    OP_CHECKPOINT,
    OP_VIEWCHECKPOINT,
    OP_REVERT,
    OP_LISTCHECKPOINTS,
    OP_UNDO,
    OP_EXEC,
    // Type 3
    OP_READ,
    OP_WRITE,
    OP_STREAM,
    // Other
    OP_EXIT,
    OP_WRITER_DONE  // Notify NS that write session is complete
} ClientOpCode;

// --- Operation Codes (NS to SS) ---
typedef enum {
    NS_SS_CREATE,
    NS_SS_DELETE,
    NS_SS_COPY_FILE, 
    NS_SS_CHECKPOINT,
    NS_SS_REVERT,
    NS_SS_UNDO,
    NS_SS_FETCH_FILE, 
    NS_SS_UPDATE_INFO,
    NS_SS_GET_INFO,  // Request metadata from SS
    NS_SS_HEARTBEAT, 
    NS_SS_REPLICATE_ALL,
    NS_SS_REPLICATE_FILE,
    NS_SS_CREATEFOLDER,
    NS_SS_MOVE,
    NS_SS_PAIR_AND_SYNC,      // Tell SS to pair with partner and sync
    NS_SS_PARTNER_DISCONNECTED // Tell SS its partner died
} NSOpCode;

// --- Operation Codes (SS to NS) ---
typedef enum {
    SS_NS_REGISTER,
    SS_NS_UPDATE_INFO,
    SS_NS_HEARTBEAT,
    SS_NS_PARTNER_DIED  // Notify NS that partner is unreachable
} SSOpCode;

// --- Error Codes ---
typedef enum {
    ERR_OK = 0,
    ERR_FILE_NOT_FOUND,
    ERR_SS_NOT_FOUND,
    ERR_ACCESS_DENIED,
    ERR_FILE_EXISTS,
    ERR_INVALID_PATH,
    ERR_USERNAME_TAKEN, 
    ERR_UNKNOWN_COMMAND,
    ERR_SENTENCE_LOCKED,
    ERR_INDEX_OUT_OF_BOUNDS,
    ERR_NOT_OWNER 
} ErrorCode;

// --- Client-SS Write Protocol Opcodes ---
typedef enum {
    WRITE_OP_INSERT_WORD, 
    WRITE_OP_ETIRW       // Client signals end of write
} ClientWriteOpCode;


// --- Core Data Structures ---

// Sent by Client/SS on first connect
typedef struct {
    ConnectionType type;
    char username[MAX_USERNAME_LEN]; 
} InitialPacket;

// Sent by SS right after InitialPacket
typedef struct {
    char ip[INET_ADDRSTRLEN];
    int port_for_clients;
    int backup_port;          // Port for partner sync connections
    int is_empty;
    int update_port;          // Port for async updates from SS to NS
    char name[MAX_PATH_LEN];  // SS identifier (from root dir)
} SS_Info_Packet;

// Sent by Client for operations
typedef struct {
    ClientOpCode op;
    char username[MAX_USERNAME_LEN];
    char path[MAX_PATH_LEN];
    char arg1[MAX_PATH_LEN]; // For <username>, <dirname>, <tag>
    char arg2[10];           // For access type 'T' (R, W, X)
    int flags;               // For VIEW -a, -l
    int index;               // For APPROVE, sentence_num (for WRITE)
} ClientRequest;

// Sent by NS in response to most operations
typedef struct {
    ErrorCode status;
    char message[MAX_BUFFER_LEN]; 
    
    // For Type-3 ops: Redirect info
    char ss_ip[INET_ADDRSTRLEN];
    int ss_port;
} ServerResponse;

// Packet for SS-to-NS file synchronization
typedef struct {
    char path[MAX_PATH_LEN];
    char info_content[MAX_INFO_LEN]; // Content of the corresponding info_file
    char undo_content[MAX_COMMIT_LEN]; // Content of the undo file
    long undo_size; // Size of undo file content
} SS_File_Sync_Packet;

// --- MODIFIED: Packet for Client-SS Write Session ---
// This is now for the *session*, as originally intended
typedef struct {
    ClientWriteOpCode op;
    int word_index; // Per-sentence word index
    char content[MAX_COMMIT_LEN]; // Can hold multiple words
} ClientWritePacket;
// --- END MODIFIED ---

// --- SS-to-SS Sync Opcodes ---
typedef enum {
    SS_SYNC_CREATE,
    SS_SYNC_WRITE,
    SS_SYNC_DELETE,
    SS_SYNC_CREATEFOLDER,
    SS_SYNC_MOVE,
    SS_SYNC_UNDO,
    SS_SYNC_CHECKPOINT,
    SS_SYNC_UPDATE_INFO,
    SS_SYNC_FILE_DATA,      // Transfer file data during initial sync
    SS_SYNC_COMPLETE        // End marker for initial sync
} SSSyncOpCode;

// Packet for SS-to-SS file data transfer
typedef struct {
    char path[MAX_PATH_LEN];
    long file_size;
    char info_content[MAX_INFO_LEN];
    char undo_content[MAX_COMMIT_LEN]; // Content of the undo file
    long undo_size; // Size of undo file content
} SS_Sync_File_Packet;

#endif // COMMON_H