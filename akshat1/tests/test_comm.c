#include "../communications/comm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#define TEST_PORT 5555
#define TEST_IP "127.0.0.1"

void test_initialization(void) {
    printf("\n=== Test: Initialization ===\n");
    
    assert(comm_init(ENTITY_NS, TEST_PORT) == 0);
    assert(comm_is_initialized() == 1);
    
    printf("✓ Initialization successful\n");
}

void test_message_creation(void) {
    printf("\n=== Test: Message Creation ===\n");
    
    Message msg;
    comm_create_message(OP_CREATE, "testuser", "test.txt", 8, &msg);
    
    assert(msg.operation == OP_CREATE);
    assert(strcmp(msg.username, "testuser") == 0);
    assert(msg.payload_length == 8);
    assert(memcmp(msg.payload, "test.txt", 8) == 0);
    
    printf("✓ Message creation successful\n");
}

void test_listener_start(void) {
    printf("\n=== Test: Start Listener ===\n");
    
    assert(comm_start_listener(0) == 0);
    sleep(1);  // Give listener time to start
    
    printf("✓ Listener started successfully\n");
}

void test_connection_registry(void) {
    printf("\n=== Test: Connection Registry ===\n");
    
    assert(comm_register_connection("user1", "192.168.1.10", 4000, ENTITY_CLIENT) == 0);
    assert(comm_register_connection("ss1", "192.168.1.20", 5000, ENTITY_SS) == 0);
    assert(comm_register_connection("ss2", "192.168.1.21", 5001, ENTITY_SS) == 0);
    
    char ip[MAX_IP_LEN];
    uint16_t port;
    assert(comm_get_connection_info("user1", ip, &port) == 0);
    assert(strcmp(ip, "192.168.1.10") == 0);
    assert(port == 4000);
    
    char entity_ids[10][MAX_USERNAME_LEN];
    int count = comm_list_connections(entity_ids, 10);
    assert(count == 3);
    
    assert(comm_unregister_connection("user1") == 0);
    count = comm_list_connections(entity_ids, 10);
    assert(count == 2);
    
    printf("✓ Connection registry successful\n");
}

void test_message_helpers(void) {
    printf("\n=== Test: Message Helper Functions ===\n");
    
    // Test message type to string
    assert(strcmp(message_type_to_string(MSG_TYPE_REQUEST), "REQUEST") == 0);
    assert(strcmp(operation_to_string(OP_CREATE), "CREATE") == 0);
    assert(strcmp(status_to_string(STATUS_OK), "OK") == 0);
    
    // Test message validation
    Message msg;
    message_create_request(&msg, OP_READ, "user1", "file.txt", 8);
    assert(message_validate(&msg) == 1);
    
    // Test JSON serialization
    char json_buf[2048];
    assert(message_to_json(&msg, json_buf, sizeof(json_buf)) == 0);
    printf("JSON: %s\n", json_buf);
    
    printf("✓ Message helpers successful\n");
}

void test_statistics(void) {
    printf("\n=== Test: Statistics ===\n");
    
    uint64_t msgs_sent, msgs_received, bytes_sent, bytes_received;
    int active_connections;
    
    comm_get_stats(&msgs_sent, &msgs_received, &bytes_sent, &bytes_received, &active_connections);
    
    printf("Messages sent: %lu\n", msgs_sent);
    printf("Messages received: %lu\n", msgs_received);
    printf("Bytes sent: %lu\n", bytes_sent);
    printf("Bytes received: %lu\n", bytes_received);
    printf("Active connections: %d\n", active_connections);
    
    printf("✓ Statistics retrieval successful\n");
}

void test_send_receive(void) {
    printf("\n=== Test: Send and Receive (requires separate process) ===\n");
    printf("This test requires running a separate server/client.\n");
    printf("Run: ./test_comm server  (in one terminal)\n");
    printf("Run: ./test_comm client  (in another terminal)\n");
}

void run_server_mode(void) {
    printf("\n=== Running in SERVER mode ===\n");
    
    assert(comm_init(ENTITY_NS, TEST_PORT) == 0);
    comm_set_log_file("server_comm.log");
    assert(comm_start_listener(0) == 0);
    
    printf("Server listening on port %d\n", TEST_PORT);
    printf("Waiting for messages... (press Ctrl+C to stop)\n\n");
    
    while (1) {
        Message msg;
        int result = comm_receive_message(&msg, 5000);  // 5 second timeout
        
        if (result == 0) {
            printf("Received: %s from %s:%d [%s/%s]\n",
                   message_type_to_string(msg.type),
                   msg.source_ip,
                   msg.source_port,
                   operation_to_string(msg.operation),
                   msg.username);
            
            if (msg.payload_length > 0) {
                printf("Payload: %.*s\n", msg.payload_length, msg.payload);
            }
            
            // Send acknowledgment
            comm_send_ack(msg.source_ip, msg.source_port, msg.sequence_num);
            printf("Sent ACK\n\n");
            
        } else if (result == -2) {
            printf("Timeout (no messages received)\n");
        }
    }
}

void run_client_mode(void) {
    printf("\n=== Running in CLIENT mode ===\n");
    
    assert(comm_init(ENTITY_CLIENT, 0) == 0);  // Port 0 = client-only
    comm_set_log_file("client_comm.log");
    
    printf("Connecting to server at %s:%d\n", TEST_IP, TEST_PORT);
    
    // Send CREATE request
    Message msg;
    comm_create_message(OP_CREATE, "testuser", "hello.txt", 9, &msg);
    
    printf("Sending CREATE message...\n");
    if (comm_send_message(TEST_IP, TEST_PORT, &msg) == 0) {
        printf("Message sent successfully!\n");
    } else {
        printf("Failed to send message\n");
        return;
    }
    
    sleep(1);
    
    // Send READ request
    comm_create_message(OP_READ, "testuser", "hello.txt", 9, &msg);
    
    printf("Sending READ message...\n");
    if (comm_send_message(TEST_IP, TEST_PORT, &msg) == 0) {
        printf("Message sent successfully!\n");
    } else {
        printf("Failed to send message\n");
    }
    
    printf("\nClient done. Check server output.\n");
}

int main(int argc, char* argv[]) {
    printf("========================================\n");
    printf("  Communications Layer Test Program\n");
    printf("========================================\n");
    
    if (argc > 1) {
        if (strcmp(argv[1], "server") == 0) {
            run_server_mode();
            return 0;
        } else if (strcmp(argv[1], "client") == 0) {
            run_client_mode();
            comm_shutdown();
            return 0;
        }
    }
    
    // Run unit tests
    test_initialization();
    test_message_creation();
    test_listener_start();
    test_connection_registry();
    test_message_helpers();
    test_statistics();
    test_send_receive();
    
    printf("\n========================================\n");
    printf("  All tests passed! ✓\n");
    printf("========================================\n");
    
    comm_shutdown();
    
    return 0;
}
