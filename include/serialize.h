#ifndef SERIALIZE_H
#define SERIALIZE_H

#include "common.h"
#include <arpa/inet.h>
#include <string.h>

// Helper to receive exactly n bytes (handles partial recv)
static inline int recv_full(int sock, void* buffer, size_t length) {
    size_t total = 0;
    char* buf = (char*)buffer;
    while (total < length) {
        int n = recv(sock, buf + total, length - total, 0);
        if (n <= 0) return n;  // Error or connection closed
        total += n;
    }
    return total;
}

static inline int send_full(int sock, const void* buffer, size_t length) {
    size_t total = 0;
    const char* buf = (const char*)buffer;
    while (total < length) {
        int n = send(sock, buf + total, length - total, 0);
        if (n <= 0) return n;  // Error or connection closed
        total += n;
    }
    return total;
}

// Helper for 64-bit network byte order (if not available on system)
#ifndef htonll
static inline uint64_t htonll(uint64_t value) {
    static const int num = 1;
    if (*(char *)&num == 1) {
        // Little endian
        return ((uint64_t)htonl(value & 0xFFFFFFFF) << 32) | htonl(value >> 32);
    }
    return value;
}
#endif

#ifndef ntohll
static inline uint64_t ntohll(uint64_t value) {
    static const int num = 1;
    if (*(char *)&num == 1) {
        // Little endian
        return ((uint64_t)ntohl(value & 0xFFFFFFFF) << 32) | ntohl(value >> 32);
    }
    return value;
}
#endif

// SS_Info_Packet serialization
static inline void serialize_ss_info_packet(const SS_Info_Packet* pkt, char* buffer) {
    int offset = 0;
    memcpy(buffer + offset, pkt->ip, INET_ADDRSTRLEN);
    offset += INET_ADDRSTRLEN;
    
    uint32_t port_for_clients = htonl(pkt->port_for_clients);
    memcpy(buffer + offset, &port_for_clients, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    uint32_t backup_port = htonl(pkt->backup_port);
    memcpy(buffer + offset, &backup_port, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    uint32_t is_empty = htonl(pkt->is_empty);
    memcpy(buffer + offset, &is_empty, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    uint32_t update_port = htonl(pkt->update_port);
    memcpy(buffer + offset, &update_port, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    memcpy(buffer + offset, pkt->name, MAX_PATH_LEN);
}

static inline void deserialize_ss_info_packet(const char* buffer, SS_Info_Packet* pkt) {
    int offset = 0;
    memcpy(pkt->ip, buffer + offset, INET_ADDRSTRLEN);
    offset += INET_ADDRSTRLEN;
    
    uint32_t port_for_clients;
    memcpy(&port_for_clients, buffer + offset, sizeof(uint32_t));
    pkt->port_for_clients = ntohl(port_for_clients);
    offset += sizeof(uint32_t);
    
    uint32_t backup_port;
    memcpy(&backup_port, buffer + offset, sizeof(uint32_t));
    pkt->backup_port = ntohl(backup_port);
    offset += sizeof(uint32_t);
    
    uint32_t is_empty;
    memcpy(&is_empty, buffer + offset, sizeof(uint32_t));
    pkt->is_empty = ntohl(is_empty);
    offset += sizeof(uint32_t);
    
    uint32_t update_port;
    memcpy(&update_port, buffer + offset, sizeof(uint32_t));
    pkt->update_port = ntohl(update_port);
    offset += sizeof(uint32_t);
    
    memcpy(pkt->name, buffer + offset, MAX_PATH_LEN);
}

#define SERIALIZED_SS_INFO_PACKET_SIZE (INET_ADDRSTRLEN + 4*sizeof(uint32_t) + MAX_PATH_LEN)

// ClientRequest serialization
static inline void serialize_client_request(const ClientRequest* pkt, char* buffer) {
    int offset = 0;
    
    uint32_t op = htonl(pkt->op);
    memcpy(buffer + offset, &op, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    memcpy(buffer + offset, pkt->username, MAX_USERNAME_LEN);
    offset += MAX_USERNAME_LEN;
    
    memcpy(buffer + offset, pkt->path, MAX_PATH_LEN);
    offset += MAX_PATH_LEN;
    
    memcpy(buffer + offset, pkt->arg1, MAX_PATH_LEN);
    offset += MAX_PATH_LEN;
    
    memcpy(buffer + offset, pkt->arg2, 10);
    offset += 10;
    
    uint32_t flags = htonl(pkt->flags);
    memcpy(buffer + offset, &flags, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    uint32_t index = htonl(pkt->index);
    memcpy(buffer + offset, &index, sizeof(uint32_t));
}

static inline void deserialize_client_request(const char* buffer, ClientRequest* pkt) {
    int offset = 0;
    
    uint32_t op;
    memcpy(&op, buffer + offset, sizeof(uint32_t));
    pkt->op = ntohl(op);
    offset += sizeof(uint32_t);
    
    memcpy(pkt->username, buffer + offset, MAX_USERNAME_LEN);
    offset += MAX_USERNAME_LEN;
    
    memcpy(pkt->path, buffer + offset, MAX_PATH_LEN);
    offset += MAX_PATH_LEN;
    
    memcpy(pkt->arg1, buffer + offset, MAX_PATH_LEN);
    offset += MAX_PATH_LEN;
    
    memcpy(pkt->arg2, buffer + offset, 10);
    offset += 10;
    
    uint32_t flags;
    memcpy(&flags, buffer + offset, sizeof(uint32_t));
    pkt->flags = ntohl(flags);
    offset += sizeof(uint32_t);
    
    uint32_t index;
    memcpy(&index, buffer + offset, sizeof(uint32_t));
    pkt->index = ntohl(index);
}

#define SERIALIZED_CLIENT_REQUEST_SIZE (sizeof(uint32_t) + MAX_USERNAME_LEN + 2*MAX_PATH_LEN + 10 + 2*sizeof(uint32_t))

// ServerResponse serialization
static inline void serialize_server_response(const ServerResponse* pkt, char* buffer) {
    int offset = 0;
    
    uint32_t status = htonl(pkt->status);
    memcpy(buffer + offset, &status, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    memcpy(buffer + offset, pkt->message, MAX_BUFFER_LEN);
    offset += MAX_BUFFER_LEN;
    
    memcpy(buffer + offset, pkt->ss_ip, INET_ADDRSTRLEN);
    offset += INET_ADDRSTRLEN;
    
    uint32_t ss_port = htonl(pkt->ss_port);
    memcpy(buffer + offset, &ss_port, sizeof(uint32_t));
}

static inline void deserialize_server_response(const char* buffer, ServerResponse* pkt) {
    int offset = 0;
    
    uint32_t status;
    memcpy(&status, buffer + offset, sizeof(uint32_t));
    pkt->status = ntohl(status);
    offset += sizeof(uint32_t);
    
    memcpy(pkt->message, buffer + offset, MAX_BUFFER_LEN);
    offset += MAX_BUFFER_LEN;
    
    memcpy(pkt->ss_ip, buffer + offset, INET_ADDRSTRLEN);
    offset += INET_ADDRSTRLEN;
    
    uint32_t ss_port;
    memcpy(&ss_port, buffer + offset, sizeof(uint32_t));
    pkt->ss_port = ntohl(ss_port);
}

#define SERIALIZED_SERVER_RESPONSE_SIZE (2*sizeof(uint32_t) + MAX_BUFFER_LEN + INET_ADDRSTRLEN)

// SS_File_Sync_Packet serialization
static inline void serialize_ss_file_sync_packet(const SS_File_Sync_Packet* pkt, char* buffer) {
    int offset = 0;
    
    memcpy(buffer + offset, pkt->path, MAX_PATH_LEN);
    offset += MAX_PATH_LEN;
    
    memcpy(buffer + offset, pkt->info_content, MAX_INFO_LEN);
    offset += MAX_INFO_LEN;
    
    memcpy(buffer + offset, pkt->undo_content, MAX_COMMIT_LEN);
    offset += MAX_COMMIT_LEN;
    
    // Convert long to network byte order (assuming 64-bit)
    uint64_t undo_size = htonll(pkt->undo_size);
    memcpy(buffer + offset, &undo_size, sizeof(uint64_t));
}

static inline void deserialize_ss_file_sync_packet(const char* buffer, SS_File_Sync_Packet* pkt) {
    int offset = 0;
    
    memcpy(pkt->path, buffer + offset, MAX_PATH_LEN);
    offset += MAX_PATH_LEN;
    
    memcpy(pkt->info_content, buffer + offset, MAX_INFO_LEN);
    offset += MAX_INFO_LEN;
    
    memcpy(pkt->undo_content, buffer + offset, MAX_COMMIT_LEN);
    offset += MAX_COMMIT_LEN;
    
    uint64_t undo_size;
    memcpy(&undo_size, buffer + offset, sizeof(uint64_t));
    pkt->undo_size = ntohll(undo_size);
}

#define SERIALIZED_SS_FILE_SYNC_PACKET_SIZE (MAX_PATH_LEN + MAX_INFO_LEN + MAX_COMMIT_LEN + sizeof(uint64_t))

// SS_Sync_File_Packet serialization (for SS-to-SS sync)
static inline void serialize_ss_sync_file_packet(const SS_Sync_File_Packet* pkt, char* buffer) {
    int offset = 0;
    
    memcpy(buffer + offset, pkt->path, MAX_PATH_LEN);
    offset += MAX_PATH_LEN;
    
    uint64_t file_size = htonll(pkt->file_size);
    memcpy(buffer + offset, &file_size, sizeof(uint64_t));
    offset += sizeof(uint64_t);
    
    memcpy(buffer + offset, pkt->info_content, MAX_INFO_LEN);
    offset += MAX_INFO_LEN;
    
    memcpy(buffer + offset, pkt->undo_content, MAX_COMMIT_LEN);
    offset += MAX_COMMIT_LEN;
    
    uint64_t undo_size = htonll(pkt->undo_size);
    memcpy(buffer + offset, &undo_size, sizeof(uint64_t));
}

static inline void deserialize_ss_sync_file_packet(const char* buffer, SS_Sync_File_Packet* pkt) {
    int offset = 0;
    
    memcpy(pkt->path, buffer + offset, MAX_PATH_LEN);
    offset += MAX_PATH_LEN;
    
    uint64_t file_size;
    memcpy(&file_size, buffer + offset, sizeof(uint64_t));
    pkt->file_size = ntohll(file_size);
    offset += sizeof(uint64_t);
    
    memcpy(pkt->info_content, buffer + offset, MAX_INFO_LEN);
    offset += MAX_INFO_LEN;
    
    memcpy(pkt->undo_content, buffer + offset, MAX_COMMIT_LEN);
    offset += MAX_COMMIT_LEN;
    
    uint64_t undo_size;
    memcpy(&undo_size, buffer + offset, sizeof(uint64_t));
    pkt->undo_size = ntohll(undo_size);
}

#define SERIALIZED_SS_SYNC_FILE_PACKET_SIZE (MAX_PATH_LEN + 2*sizeof(uint64_t) + MAX_INFO_LEN + MAX_COMMIT_LEN)

// InitialPacket serialization
static inline void serialize_initial_packet(const InitialPacket* pkt, char* buffer) {
    int offset = 0;
    uint32_t type = htonl(pkt->type);
    memcpy(buffer + offset, &type, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    memcpy(buffer + offset, pkt->username, MAX_USERNAME_LEN);
}

static inline void deserialize_initial_packet(const char* buffer, InitialPacket* pkt) {
    int offset = 0;
    uint32_t type;
    memcpy(&type, buffer + offset, sizeof(uint32_t));
    pkt->type = ntohl(type);
    offset += sizeof(uint32_t);
    memcpy(pkt->username, buffer + offset, MAX_USERNAME_LEN);
}

#define SERIALIZED_INITIAL_PACKET_SIZE (sizeof(uint32_t) + MAX_USERNAME_LEN)

// --- ClientWritePacket Serialization ---
#define SERIALIZED_CLIENT_WRITE_PACKET_SIZE (sizeof(int) * 2 + MAX_COMMIT_LEN)

static inline void serialize_client_write_packet(const ClientWritePacket* pkt, char* buffer) {
    int offset = 0;
    int op_network = htonl((int)pkt->op);
    memcpy(buffer + offset, &op_network, sizeof(int));
    offset += sizeof(int);
    
    int word_index_network = htonl(pkt->word_index);
    memcpy(buffer + offset, &word_index_network, sizeof(int));
    offset += sizeof(int);
    
    memcpy(buffer + offset, pkt->content, MAX_COMMIT_LEN);
}

static inline void deserialize_client_write_packet(const char* buffer, ClientWritePacket* pkt) {
    int offset = 0;
    int op_network;
    memcpy(&op_network, buffer + offset, sizeof(int));
    pkt->op = (ClientWriteOpCode)ntohl(op_network);
    offset += sizeof(int);
    
    int word_index_network;
    memcpy(&word_index_network, buffer + offset, sizeof(int));
    pkt->word_index = ntohl(word_index_network);
    offset += sizeof(int);
    
    memcpy(pkt->content, buffer + offset, MAX_COMMIT_LEN);
}

// --- OpCode Serialization (NSOpCode, SSOpCode, etc.) ---
static inline void serialize_opcode(int opcode, char* buffer) {
    int opcode_network = htonl(opcode);
    memcpy(buffer, &opcode_network, sizeof(int));
}

static inline int deserialize_opcode(const char* buffer) {
    int opcode_network;
    memcpy(&opcode_network, buffer, sizeof(int));
    return ntohl(opcode_network);
}

// --- Raw int Serialization ---
static inline void serialize_int(int value, char* buffer) {
    int value_network = htonl(value);
    memcpy(buffer, &value_network, sizeof(int));
}

static inline int deserialize_int(const char* buffer) {
    int value_network;
    memcpy(&value_network, buffer, sizeof(int));
    return ntohl(value_network);
}

// Helper macros for send/recv with automatic serialization
#define SEND_PACKET(sock, pkt_ptr, type_lower) do { \
    char _buf[SERIALIZED_##type_lower##_SIZE]; \
    serialize_##type_lower(pkt_ptr, _buf); \
    send(sock, _buf, SERIALIZED_##type_lower##_SIZE, 0); \
} while(0)

#define RECV_PACKET_RET(sock, pkt_ptr, type_lower) ({ \
    char _buf[SERIALIZED_##type_lower##_SIZE]; \
    int _ret = recv(sock, _buf, SERIALIZED_##type_lower##_SIZE, 0); \
    if (_ret > 0) deserialize_##type_lower(_buf, pkt_ptr); \
    _ret; \
})

#define SEND_INITIAL_PACKET(sock, pkt_ptr) do { \
    char _buf[SERIALIZED_INITIAL_PACKET_SIZE]; \
    serialize_initial_packet(pkt_ptr, _buf); \
    send(sock, _buf, SERIALIZED_INITIAL_PACKET_SIZE, 0); \
} while(0)

#define RECV_INITIAL_PACKET(sock, pkt_ptr) ({ \
    char _buf[SERIALIZED_INITIAL_PACKET_SIZE]; \
    int _ret = recv_full(sock, _buf, SERIALIZED_INITIAL_PACKET_SIZE); \
    if (_ret > 0) deserialize_initial_packet(_buf, pkt_ptr); \
    _ret; \
})

#define SEND_SS_INFO_PACKET(sock, pkt_ptr) do { \
    char _buf[SERIALIZED_SS_INFO_PACKET_SIZE]; \
    serialize_ss_info_packet(pkt_ptr, _buf); \
    send(sock, _buf, SERIALIZED_SS_INFO_PACKET_SIZE, 0); \
} while(0)

#define RECV_SS_INFO_PACKET(sock, pkt_ptr) ({ \
    char _buf[SERIALIZED_SS_INFO_PACKET_SIZE]; \
    int _ret = recv_full(sock, _buf, SERIALIZED_SS_INFO_PACKET_SIZE); \
    if (_ret > 0) deserialize_ss_info_packet(_buf, pkt_ptr); \
    _ret; \
})

#define SEND_CLIENT_REQUEST(sock, pkt_ptr) do { \
    char _buf[SERIALIZED_CLIENT_REQUEST_SIZE]; \
    serialize_client_request(pkt_ptr, _buf); \
    send(sock, _buf, SERIALIZED_CLIENT_REQUEST_SIZE, 0); \
} while(0)

#define RECV_CLIENT_REQUEST(sock, pkt_ptr) ({ \
    char _buf[SERIALIZED_CLIENT_REQUEST_SIZE]; \
    int _ret = recv_full(sock, _buf, SERIALIZED_CLIENT_REQUEST_SIZE); \
    if (_ret > 0) deserialize_client_request(_buf, pkt_ptr); \
    _ret; \
})

#define SEND_SERVER_RESPONSE(sock, pkt_ptr) do { \
    char _buf[SERIALIZED_SERVER_RESPONSE_SIZE]; \
    serialize_server_response(pkt_ptr, _buf); \
    send(sock, _buf, SERIALIZED_SERVER_RESPONSE_SIZE, 0); \
} while(0)

#define RECV_SERVER_RESPONSE(sock, pkt_ptr) ({ \
    char _buf[SERIALIZED_SERVER_RESPONSE_SIZE]; \
    int _ret = recv_full(sock, _buf, SERIALIZED_SERVER_RESPONSE_SIZE); \
    if (_ret > 0) deserialize_server_response(_buf, pkt_ptr); \
    _ret; \
})

#define SEND_CLIENT_WRITE_PACKET(sock, pkt_ptr) do { \
    char _buf[SERIALIZED_CLIENT_WRITE_PACKET_SIZE]; \
    serialize_client_write_packet(pkt_ptr, _buf); \
    send(sock, _buf, SERIALIZED_CLIENT_WRITE_PACKET_SIZE, 0); \
} while(0)

#define RECV_CLIENT_WRITE_PACKET(sock, pkt_ptr) ({ \
    char _buf[SERIALIZED_CLIENT_WRITE_PACKET_SIZE]; \
    int _ret = recv_full(sock, _buf, SERIALIZED_CLIENT_WRITE_PACKET_SIZE); \
    if (_ret > 0) deserialize_client_write_packet(_buf, pkt_ptr); \
    _ret; \
})

#define SEND_OPCODE(sock, opcode) ({ \
    char _buf[sizeof(int)]; \
    serialize_opcode(opcode, _buf); \
    send(sock, _buf, sizeof(int), 0); \
})

#define RECV_OPCODE(sock, opcode_ptr) ({ \
    char _buf[sizeof(int)]; \
    int _ret = recv_full(sock, _buf, sizeof(int)); \
    if (_ret > 0) *(opcode_ptr) = deserialize_opcode(_buf); \
    _ret; \
})

#define SEND_INT(sock, value) ({ \
    char _buf[sizeof(int)]; \
    serialize_int(value, _buf); \
    send(sock, _buf, sizeof(int), 0); \
})

#define RECV_INT(sock, value_ptr) ({ \
    char _buf[sizeof(int)]; \
    int _ret = recv_full(sock, _buf, sizeof(int)); \
    if (_ret > 0) *(value_ptr) = deserialize_int(_buf); \
    _ret; \
})

#endif // SERIALIZE_H
