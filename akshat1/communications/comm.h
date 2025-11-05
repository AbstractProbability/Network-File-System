#ifndef COMM_H
#define COMM_H

#include "../common/message.h"

/**
 * @file comm.h
 * @brief Public API for the communications layer
 * 
 * This is the ONLY header file your teammate needs to include.
 * All network/socket complexity is abstracted away.
 */

// ============ Initialization & Lifecycle ============

/**
 * Initialize communication layer
 * @param role Entity type (ENTITY_NS, ENTITY_SS, ENTITY_CLIENT, ENTITY_BSS)
 * @param port Port to listen on (0 for client-only mode)
 * @return 0 on success, -1 on error
 */
int comm_init(EntityType role, uint16_t port);

/**
 * Start listening for incoming connections
 * This spawns a background listener thread
 * @param max_connections Maximum concurrent connections (0 for default 1024)
 * @return 0 on success, -1 on error
 */
int comm_start_listener(int max_connections);

/**
 * Gracefully shutdown communication layer
 * Closes all connections and cleans up resources
 */
void comm_shutdown(void);

// ============ Message Operations ============

/**
 * Send message to specific destination
 * @param dest_ip Destination IP address
 * @param dest_port Destination port
 * @param msg Message to send
 * @return 0 on success, -1 on error
 */
int comm_send_message(const char* dest_ip, uint16_t dest_port, const Message* msg);

/**
 * Receive next message from queue (blocking)
 * @param msg Buffer to store received message
 * @param timeout_ms Timeout in milliseconds (0 = wait forever)
 * @return 0 on success, -1 on error, -2 on timeout
 */
int comm_receive_message(Message* msg, int timeout_ms);

/**
 * Receive message from specific source
 * @param source_ip Expected source IP (NULL for any)
 * @param source_port Expected source port (0 for any)
 * @param msg Buffer to store received message
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, -1 on error, -2 on timeout
 */
int comm_receive_from_specific(const char* source_ip, uint16_t source_port, 
                                Message* msg, int timeout_ms);

// ============ Convenience Wrappers ============

/**
 * Send success response
 * @param dest_ip Destination IP
 * @param dest_port Destination port
 * @param op_id Original operation ID
 * @param payload Response data (can be NULL)
 * @param payload_len Response data length
 * @return 0 on success, -1 on error
 */
int comm_send_response(const char* dest_ip, uint16_t dest_port, 
                       OperationID op_id, const char* payload, size_t payload_len);

/**
 * Send error response
 * @param dest_ip Destination IP
 * @param dest_port Destination port
 * @param status Error status code
 * @param error_msg Error message
 * @return 0 on success, -1 on error
 */
int comm_send_error(const char* dest_ip, uint16_t dest_port, 
                    StatusCode status, const char* error_msg);

/**
 * Send acknowledgment
 * @param dest_ip Destination IP
 * @param dest_port Destination port
 * @param seq_num Sequence number to acknowledge
 * @return 0 on success, -1 on error
 */
int comm_send_ack(const char* dest_ip, uint16_t dest_port, uint32_t seq_num);

/**
 * Broadcast message to multiple destinations
 * @param ips Array of IP addresses
 * @param ports Array of ports
 * @param count Number of destinations
 * @param msg Message to broadcast
 * @return Number of successful sends
 */
int comm_broadcast(const char** ips, const uint16_t* ports, int count, const Message* msg);

// ============ Streaming Support ============

/**
 * Send data chunk (for large file transfers)
 * @param dest_ip Destination IP
 * @param dest_port Destination port
 * @param chunk_data Data to send
 * @param chunk_size Size of data
 * @param is_last Whether this is the last chunk
 * @return 0 on success, -1 on error
 */
int comm_send_chunk(const char* dest_ip, uint16_t dest_port, 
                    const char* chunk_data, size_t chunk_size, int is_last);

/**
 * Receive stream of chunks until complete
 * @param buffer Buffer to store received data
 * @param buffer_size Maximum buffer size
 * @param bytes_received Output: actual bytes received
 * @param timeout_ms Timeout per chunk
 * @return 0 on success, -1 on error
 */
int comm_receive_stream(char* buffer, size_t buffer_size, 
                        size_t* bytes_received, int timeout_ms);

// ============ Connection Management ============

/**
 * Register a connection with metadata
 * @param entity_id Unique identifier (username, ss_id, etc.)
 * @param ip IP address
 * @param port Port number
 * @param type Entity type
 * @return 0 on success, -1 on error
 */
int comm_register_connection(const char* entity_id, const char* ip, 
                             uint16_t port, EntityType type);

/**
 * Unregister a connection
 * @param entity_id Entity identifier
 * @return 0 on success, -1 on error
 */
int comm_unregister_connection(const char* entity_id);

/**
 * Get connection info by entity ID
 * @param entity_id Entity identifier
 * @param ip Output: IP address buffer (at least MAX_IP_LEN bytes)
 * @param port Output: port number
 * @return 0 on success, -1 if not found
 */
int comm_get_connection_info(const char* entity_id, char* ip, uint16_t* port);

/**
 * List all registered connections
 * @param entity_ids Output: array of entity IDs
 * @param max_entries Maximum entries to return
 * @return Number of entries returned
 */
int comm_list_connections(char entity_ids[][MAX_USERNAME_LEN], int max_entries);

// ============ Utilities ============

/**
 * Create a message structure (convenience wrapper)
 * @param op_id Operation ID
 * @param username Username
 * @param payload Payload data (can be NULL)
 * @param payload_len Payload length
 * @param msg Output: initialized message
 */
void comm_create_message(OperationID op_id, const char* username, 
                        const char* payload, size_t payload_len, Message* msg);

/**
 * Get network statistics
 * @param msgs_sent Output: messages sent
 * @param msgs_received Output: messages received
 * @param bytes_sent Output: bytes sent
 * @param bytes_received Output: bytes received
 * @param active_connections Output: active connection count
 */
void comm_get_stats(uint64_t* msgs_sent, uint64_t* msgs_received,
                    uint64_t* bytes_sent, uint64_t* bytes_received,
                    int* active_connections);

/**
 * Enable/disable debug logging
 * @param enable 1 to enable, 0 to disable
 */
void comm_set_debug(int enable);

/**
 * Set log file path
 * @param filepath Path to log file
 * @return 0 on success, -1 on error
 */
int comm_set_log_file(const char* filepath);

/**
 * Check if communication layer is initialized
 * @return 1 if initialized, 0 otherwise
 */
int comm_is_initialized(void);

#endif // COMM_H
