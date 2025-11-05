#ifndef MESSAGE_H
#define MESSAGE_H

#include <stdint.h>
#include <time.h>
#include <sys/types.h>

// Maximum lengths
#define MAX_USERNAME_LEN    64
#define MAX_FILEPATH_LEN    512
#define MAX_PAYLOAD_LEN     8192
#define MAX_ERROR_MSG_LEN   256
#define MAX_IP_LEN          16

// Message types
typedef enum {
    MSG_TYPE_REQUEST = 1,
    MSG_TYPE_RESPONSE,
    MSG_TYPE_ACK,
    MSG_TYPE_ERROR,
    MSG_TYPE_DATA_CHUNK,
    MSG_TYPE_STOP,
    MSG_TYPE_HEARTBEAT,
    MSG_TYPE_CONNECT,
    MSG_TYPE_DISCONNECT
} MessageType;

// Operation IDs
typedef enum {
    OP_NONE = 0,
    
    // Client operations
    OP_VIEW = 100,
    OP_READ,
    OP_CREATE,
    OP_WRITE,
    OP_DELETE,
    OP_INFO,
    OP_STREAM,
    OP_LIST,
    OP_ADDACCESS,
    OP_REMACCESS,
    OP_EXEC,
    OP_UNDO,
    
    // Folder operations (bonus)
    OP_CREATEFOLDER = 150,
    OP_MOVE,
    OP_VIEWFOLDER,
    
    // Checkpoint operations (bonus)
    OP_CHECKPOINT = 160,
    OP_VIEWCHECKPOINT,
    OP_REVERT,
    OP_LISTCHECKPOINTS,
    
    // Access request operations (bonus)
    OP_REQUEST_ACCESS = 170,
    OP_APPROVE_ACCESS,
    OP_REJECT_ACCESS,
    OP_LIST_REQUESTS,
    
    // SS operations
    OP_SS_REGISTER = 200,
    OP_SS_FILE_LIST,
    OP_SS_FILE_INFO,
    OP_SS_HEARTBEAT,
    
    // BSS operations
    OP_BSS_REGISTER = 250,
    OP_BSS_SYNC,
    
    // Write operations (direct client-SS)
    OP_WRITE_LOCK = 300,
    OP_WRITE_UPDATE,
    OP_WRITE_COMMIT,
    OP_WRITE_ABORT,
    
    // Internal operations
    OP_HEARTBEAT = 400,
    OP_PING,
    OP_SHUTDOWN
} OperationID;

// Status codes
typedef enum {
    // Success codes (100-199)
    STATUS_OK = 100,
    STATUS_CREATED = 101,
    STATUS_ACCEPTED = 102,
    
    // Client errors (200-299)
    STATUS_BAD_REQUEST = 200,
    STATUS_UNAUTHORIZED = 201,
    STATUS_FORBIDDEN = 202,
    STATUS_NOT_FOUND = 203,
    STATUS_LOCKED = 204,
    STATUS_INVALID_INDEX = 205,
    STATUS_FILE_EXISTS = 206,
    
    // Server errors (300-399)
    STATUS_INTERNAL_ERROR = 300,
    STATUS_SS_UNAVAILABLE = 301,
    STATUS_TIMEOUT = 302,
    STATUS_NO_BACKUP = 303
} StatusCode;

// Entity types
typedef enum {
    ENTITY_UNKNOWN = 0,
    ENTITY_CLIENT,
    ENTITY_NS,
    ENTITY_SS,
    ENTITY_BSS
} EntityType;

// Main message structure (fixed size for easy serialization)
typedef struct {
    MessageType type;
    OperationID operation;
    StatusCode status;
    char username[MAX_USERNAME_LEN];
    char source_ip[MAX_IP_LEN];
    uint16_t source_port;
    EntityType source_type;
    uint32_t payload_length;
    char payload[MAX_PAYLOAD_LEN];
    char error_msg[MAX_ERROR_MSG_LEN];
    time_t timestamp;
    uint32_t sequence_num;      // For ordering
    uint8_t flags;               // Bit flags for special handling
} Message;

// Flag definitions
#define MSG_FLAG_REQUIRES_ACK   0x01
#define MSG_FLAG_IS_LAST_CHUNK  0x02
#define MSG_FLAG_IS_BROADCAST   0x04

// Helper functions for message creation
void message_init(Message* msg);
void message_create_request(Message* msg, OperationID op, const char* username, 
                           const char* payload_data, size_t payload_len);
void message_create_response(Message* msg, StatusCode status, const char* payload_data, 
                            size_t payload_len);
void message_create_error(Message* msg, StatusCode status, const char* error_msg);
void message_create_ack(Message* msg, uint32_t seq_num);
void message_create_heartbeat(Message* msg, const char* entity_id);

// Message validation
int message_validate(const Message* msg);

// Conversion utilities
const char* message_type_to_string(MessageType type);
const char* operation_to_string(OperationID op);
const char* status_to_string(StatusCode status);
const char* entity_type_to_string(EntityType type);

// Serialization (for JSON logging)
int message_to_json(const Message* msg, char* buffer, size_t buffer_size);

#endif // MESSAGE_H
