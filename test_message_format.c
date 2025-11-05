#include "Comm/communication.h"
#include <stdio.h>

int main() {
    printf("=== Testing Nested Array Message Format ===\n\n");
    
    // Test 1: Create a heartbeat response message
    printf("Test 1: Heartbeat Response Message\n");
    Message *msg1 = create_message();
    add_string_field(msg1, "type", "HEARTBEAT_RESPONSE");
    add_string_field(msg1, "name", "SS1");
    add_number_field(msg1, "timestamp", 1234567890);
    
    char *serialized1 = serialize_message(msg1);
    printf("Serialized: %s\n\n", serialized1);
    free_message(msg1);
    
    // Test 2: Parse the message back
    printf("Test 2: Parsing the message\n");
    Message *msg2 = parse_message(serialized1);
    if (msg2) {
        printf("Type: %s\n", get_string_field(msg2, "type"));
        printf("Name: %s\n", get_string_field(msg2, "name"));
        printf("Timestamp: %d\n", get_int_field(msg2, "timestamp"));
        free_message(msg2);
    }
    printf("\n");
    free(serialized1);
    
    // Test 3: Client registration message
    printf("Test 3: Client Registration Message\n");
    Message *msg3 = create_message();
    add_string_field(msg3, "type", "CLIENT_REGISTER");
    add_string_field(msg3, "username", "alice");
    
    char *serialized3 = serialize_message(msg3);
    printf("Serialized: %s\n\n", serialized3);
    free_message(msg3);
    free(serialized3);
    
    // Test 4: Storage Server registration with array
    printf("Test 4: Storage Server Registration Message\n");
    Message *msg4 = create_message();
    add_string_field(msg4, "type", "SS_REGISTER");
    add_string_field(msg4, "name", "SS1");
    add_string_field(msg4, "ip", "127.0.0.1");
    add_number_field(msg4, "nm_port", 5000);
    add_number_field(msg4, "client_port", 5001);
    add_array_field(msg4, "files");
    
    char *serialized4 = serialize_message(msg4);
    printf("Serialized: %s\n\n", serialized4);
    free_message(msg4);
    free(serialized4);
    
    // Test 5: Response message
    printf("Test 5: Response Message\n");
    char *response = create_response("SUCCESS", "Client registered", 0);
    printf("Response: %s\n\n", response);
    free(response);
    
    printf("=== All tests completed successfully! ===\n");
    
    return 0;
}
