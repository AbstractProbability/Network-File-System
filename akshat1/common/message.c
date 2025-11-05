#include "message.h"
#include <string.h>
#include <stdio.h>

void message_init(Message* msg) {
    memset(msg, 0, sizeof(Message));
    msg->timestamp = time(NULL);
}

void message_create_request(Message* msg, OperationID op, const char* username, 
                           const char* payload_data, size_t payload_len) {
    message_init(msg);
    msg->type = MSG_TYPE_REQUEST;
    msg->operation = op;
    msg->status = STATUS_OK;
    
    if (username) {
        strncpy(msg->username, username, MAX_USERNAME_LEN - 1);
    }
    
    if (payload_data && payload_len > 0) {
        size_t copy_len = payload_len < MAX_PAYLOAD_LEN ? payload_len : MAX_PAYLOAD_LEN;
        memcpy(msg->payload, payload_data, copy_len);
        msg->payload_length = copy_len;
    }
}

void message_create_response(Message* msg, StatusCode status, const char* payload_data, 
                            size_t payload_len) {
    message_init(msg);
    msg->type = MSG_TYPE_RESPONSE;
    msg->operation = OP_NONE;
    msg->status = status;
    
    if (payload_data && payload_len > 0) {
        size_t copy_len = payload_len < MAX_PAYLOAD_LEN ? payload_len : MAX_PAYLOAD_LEN;
        memcpy(msg->payload, payload_data, copy_len);
        msg->payload_length = copy_len;
    }
}

void message_create_error(Message* msg, StatusCode status, const char* error_msg) {
    message_init(msg);
    msg->type = MSG_TYPE_ERROR;
    msg->operation = OP_NONE;
    msg->status = status;
    
    if (error_msg) {
        strncpy(msg->error_msg, error_msg, MAX_ERROR_MSG_LEN - 1);
    }
}

void message_create_ack(Message* msg, uint32_t seq_num) {
    message_init(msg);
    msg->type = MSG_TYPE_ACK;
    msg->operation = OP_NONE;
    msg->status = STATUS_OK;
    msg->sequence_num = seq_num;
}

void message_create_heartbeat(Message* msg, const char* entity_id) {
    message_init(msg);
    msg->type = MSG_TYPE_HEARTBEAT;
    msg->operation = OP_HEARTBEAT;
    msg->status = STATUS_OK;
    
    if (entity_id) {
        strncpy(msg->username, entity_id, MAX_USERNAME_LEN - 1);
    }
}

int message_validate(const Message* msg) {
    if (!msg) return 0;
    
    // Check message type
    if (msg->type < MSG_TYPE_REQUEST || msg->type > MSG_TYPE_DISCONNECT) {
        return 0;
    }
    
    // Check payload length
    if (msg->payload_length > MAX_PAYLOAD_LEN) {
        return 0;
    }
    
    // Check timestamp (reasonable range)
    time_t now = time(NULL);
    if (msg->timestamp > now + 60 || msg->timestamp < now - 86400) {
        // More than 1 minute in future or 1 day in past
        return 0;
    }
    
    return 1;
}

const char* message_type_to_string(MessageType type) {
    switch (type) {
        case MSG_TYPE_REQUEST: return "REQUEST";
        case MSG_TYPE_RESPONSE: return "RESPONSE";
        case MSG_TYPE_ACK: return "ACK";
        case MSG_TYPE_ERROR: return "ERROR";
        case MSG_TYPE_DATA_CHUNK: return "DATA_CHUNK";
        case MSG_TYPE_STOP: return "STOP";
        case MSG_TYPE_HEARTBEAT: return "HEARTBEAT";
        case MSG_TYPE_CONNECT: return "CONNECT";
        case MSG_TYPE_DISCONNECT: return "DISCONNECT";
        default: return "UNKNOWN";
    }
}

const char* operation_to_string(OperationID op) {
    switch (op) {
        case OP_NONE: return "NONE";
        case OP_VIEW: return "VIEW";
        case OP_READ: return "READ";
        case OP_CREATE: return "CREATE";
        case OP_WRITE: return "WRITE";
        case OP_DELETE: return "DELETE";
        case OP_INFO: return "INFO";
        case OP_STREAM: return "STREAM";
        case OP_LIST: return "LIST";
        case OP_ADDACCESS: return "ADDACCESS";
        case OP_REMACCESS: return "REMACCESS";
        case OP_EXEC: return "EXEC";
        case OP_UNDO: return "UNDO";
        case OP_CREATEFOLDER: return "CREATEFOLDER";
        case OP_MOVE: return "MOVE";
        case OP_VIEWFOLDER: return "VIEWFOLDER";
        case OP_CHECKPOINT: return "CHECKPOINT";
        case OP_VIEWCHECKPOINT: return "VIEWCHECKPOINT";
        case OP_REVERT: return "REVERT";
        case OP_LISTCHECKPOINTS: return "LISTCHECKPOINTS";
        case OP_REQUEST_ACCESS: return "REQUEST_ACCESS";
        case OP_APPROVE_ACCESS: return "APPROVE_ACCESS";
        case OP_REJECT_ACCESS: return "REJECT_ACCESS";
        case OP_LIST_REQUESTS: return "LIST_REQUESTS";
        case OP_SS_REGISTER: return "SS_REGISTER";
        case OP_SS_FILE_LIST: return "SS_FILE_LIST";
        case OP_SS_FILE_INFO: return "SS_FILE_INFO";
        case OP_SS_HEARTBEAT: return "SS_HEARTBEAT";
        case OP_BSS_REGISTER: return "BSS_REGISTER";
        case OP_BSS_SYNC: return "BSS_SYNC";
        case OP_WRITE_LOCK: return "WRITE_LOCK";
        case OP_WRITE_UPDATE: return "WRITE_UPDATE";
        case OP_WRITE_COMMIT: return "WRITE_COMMIT";
        case OP_WRITE_ABORT: return "WRITE_ABORT";
        case OP_HEARTBEAT: return "HEARTBEAT";
        case OP_PING: return "PING";
        case OP_SHUTDOWN: return "SHUTDOWN";
        default: return "UNKNOWN";
    }
}

const char* status_to_string(StatusCode status) {
    switch (status) {
        case STATUS_OK: return "OK";
        case STATUS_CREATED: return "CREATED";
        case STATUS_ACCEPTED: return "ACCEPTED";
        case STATUS_BAD_REQUEST: return "BAD_REQUEST";
        case STATUS_UNAUTHORIZED: return "UNAUTHORIZED";
        case STATUS_FORBIDDEN: return "FORBIDDEN";
        case STATUS_NOT_FOUND: return "NOT_FOUND";
        case STATUS_LOCKED: return "LOCKED";
        case STATUS_INVALID_INDEX: return "INVALID_INDEX";
        case STATUS_FILE_EXISTS: return "FILE_EXISTS";
        case STATUS_INTERNAL_ERROR: return "INTERNAL_ERROR";
        case STATUS_SS_UNAVAILABLE: return "SS_UNAVAILABLE";
        case STATUS_TIMEOUT: return "TIMEOUT";
        case STATUS_NO_BACKUP: return "NO_BACKUP";
        default: return "UNKNOWN";
    }
}

const char* entity_type_to_string(EntityType type) {
    switch (type) {
        case ENTITY_UNKNOWN: return "UNKNOWN";
        case ENTITY_CLIENT: return "CLIENT";
        case ENTITY_NS: return "NS";
        case ENTITY_SS: return "SS";
        case ENTITY_BSS: return "BSS";
        default: return "UNKNOWN";
    }
}

int message_to_json(const Message* msg, char* buffer, size_t buffer_size) {
    if (!msg || !buffer || buffer_size < 512) return -1;
    
    int written = snprintf(buffer, buffer_size,
        "{"
        "\"type\":\"%s\","
        "\"operation\":\"%s\","
        "\"status\":\"%s\","
        "\"username\":\"%s\","
        "\"source\":\"%s:%u\","
        "\"source_type\":\"%s\","
        "\"payload_length\":%u,"
        "\"error_msg\":\"%s\","
        "\"timestamp\":%ld,"
        "\"sequence_num\":%u"
        "}",
        message_type_to_string(msg->type),
        operation_to_string(msg->operation),
        status_to_string(msg->status),
        msg->username,
        msg->source_ip,
        msg->source_port,
        entity_type_to_string(msg->source_type),
        msg->payload_length,
        msg->error_msg,
        msg->timestamp,
        msg->sequence_num
    );
    
    return (written < buffer_size) ? 0 : -1;
}
