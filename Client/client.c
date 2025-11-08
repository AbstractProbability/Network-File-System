#include "../Comm/communication.h"

// Global Variables

int ns_socket = -1;
char username[100];

// Client Registration Message Creation

char* create_client_registration_message() {
    Message *msg = create_message(); // Initialize a Message object
    
    add_string_field(msg, "type", "CLIENT_REGISTER"); // Add type field
    add_string_field(msg, "username", username); // Add username field
    
    char *result = serialize_message(msg); // Convert Message to string
    free_message(msg); // Free Message object
    
    return result; // Return the message string
}

// Command Message Creation

char* create_command_message(const char *cmd_type, const char *filename) {
    Message *msg = create_message(); // Initialize Message object
    
    add_string_field(msg, "type", cmd_type); // Add command type field
    add_string_field(msg, "username", username); // Add username field
    
    if (filename) {
        add_string_field(msg, "filename", filename); // If filename is provided, add it
    }
    
    char *result = serialize_message(msg); // Convert Message to string
    free_message(msg); // Free Message object

    return result; // Return the message string
}

// Command Loop

void command_loop() { // handles user input and sends commands to NS
    char input[256]; // max input size is 256
    char command[50]; // max command size is 50 
    char argument[200]; // max argument size is 200
    
    while (1) { // until break is encoutered run the loop infinitely
        printf("\n%s@docs++> ", username); // prompt for user input
        fflush(stdout); // flush output buffer
        
        if (!fgets(input, sizeof(input), stdin)) {
            break; // exit loop on input error
        }
        
        // Remove newline
        input[strcspn(input, "\n")] = 0; // strcspn finds the first occurrence of '\n' and replaces it with null terminator
        
        // Skip empty input
        if (strlen(input) == 0) continue; // if string length is 0 continue to next iteration
        
        // Parse command
        int num_args = sscanf(input, "%s %[^\n]", command, argument);
        
        if (num_args < 1) continue;

// [IMP1] Command Handling functions part starts here , extend and modify as needed 
        
        // Handle EXIT command
        if (strcasecmp(command, "EXIT") == 0) {
            printf("Goodbye!\n");
            break;
        }
        
        // Handle LIST command
        else if (strcasecmp(command, "LIST") == 0) {
            char *msg = create_command_message("LIST", NULL);
            
            
            if (send_message(ns_socket, msg) < 0) {
                printf("Error: Failed to send command\n");
                free(msg);
                continue;
            }
            free(msg);
            
            char *response = receive_message(ns_socket);
            if (response) {
                printf("Response: %s\n", response);
                free(response);
            } else {
                printf("Error: No response from server\n");
            }
        }
        
        // Handle CREATE command
        else if (strcasecmp(command, "CREATE") == 0) {
            if (num_args < 2) {
                printf("Usage: CREATE <filename>\n");
                continue;
            }
            
            char *msg = create_command_message("CREATE", argument);
            
            if (send_message(ns_socket, msg) < 0) {
                printf("Error: Failed to send command\n");
                free(msg);
                continue;
            }
            free(msg);
            
            char *response = receive_message(ns_socket);
            if (response) {
                printf("Response: %s\n", response);
                free(response);
            } else {
                printf("Error: No response from server\n");
            }
        }
        
        // Handle READ command
        else if (strcasecmp(command, "READ") == 0) {
            if (num_args < 2) {
                printf("Usage: READ <filename>\n");
                continue;
            }
            
            char *msg = create_command_message("READ", argument);
            
            if (send_message(ns_socket, msg) < 0) {
                printf("Error: Failed to send command\n");
                free(msg);
                continue;
            }
            free(msg);
            
            char *response = receive_message(ns_socket);
            if (response) {
                printf("Response: %s\n", response);
                free(response);
            } else {
                printf("Error: No response from server\n");
            }
        }
        
        // Handle DELETE command
        else if (strcasecmp(command, "DELETE") == 0) {
            if (num_args < 2) {
                printf("Usage: DELETE <filename>\n");
                continue;
            }
            
            char *msg = create_command_message("DELETE", argument);
            
            if (send_message(ns_socket, msg) < 0) {
                printf("Error: Failed to send command\n");
                free(msg);
                continue;
            }
            free(msg);
            
            char *response = receive_message(ns_socket);
            if (response) {
                printf("Response: %s\n", response);
                free(response);
            } else {
                printf("Error: No response from server\n");
            }
        }
        
        // Handle WRITE command
        else if (strcasecmp(command, "WRITE") == 0) {
            if (num_args < 2) {
                printf("Usage: WRITE <filename>\n");
                continue;
            }
            
            char *msg = create_command_message("WRITE", argument);
            
            if (send_message(ns_socket, msg) < 0) {
                printf("Error: Failed to send command\n");
                free(msg);
                continue;
            }
            free(msg);
            
            char *response = receive_message(ns_socket);
            if (response) {
                printf("Response: %s\n", response);
                free(response);
            } else {
                printf("Error: No response from server\n");
            }
        }
        
        // Unknown command
        else {
            printf("Unknown command: %s\n", command);
            printf("Available commands:\n");
            printf("  CREATE <filename>\n");
            printf("  READ <filename>\n");
            printf("  WRITE <filename>\n");
            printf("  DELETE <filename>\n");
            printf("  LIST\n");
            printf("  EXIT\n");
        }
    }
}


// [IMP1] Command Handling part ends here  
        

// MAIN FUNCTION

int main() {
    printf("=== Docs++ Client ===\n\n"); // [REVIEW] Added extra new line for better readability
    
    // Get username
    printf("Username: ");
    fflush(stdout);
    if (!fgets(username, sizeof(username), stdin)) {
        fprintf(stderr, "\nError: Failed to read username\n");
        return 1;
    }
    username[strcspn(username, "\n")] = 0; // Remove newline character
    
    if (strlen(username) == 0) {
        fprintf(stderr, "Error: Username cannot be empty\n"); // Check if username is empty, if empty raise error and exit
        return 1;
    }
    
    printf("\nConnecting to Name Server...\n");
    fflush(stdout);
    
    // Connect to Name Server
    ns_socket = create_client_socket("127.0.0.1", 5000); //[IMP] connect to NS at localhost:5000 and IP 127.0.0.1
    // all clients will have same hardcoded ip (127.0.0.1) and port (5000), but os will provide each with an unique socket fd and ephemeral port
    
    // Even with the same IP, each client gets a unique ephemeral port assigned by the OS:
    // Client 1: 127.0.0.1:54321 → NS (5000)
    // Client 2: 127.0.0.1:54322 → NS (5000)
    // Client 3: 127.0.0.1:54323 → NS (5000)
    //       ↑same IP  ↑different port!

    // Each connection gets a unique socket FD:
    // Connection conn1: socket_fd = 4, ip = 127.0.0.1, port = 54321
    // Connection conn2: socket_fd = 5, ip = 127.0.0.1, port = 54322
    // Connection conn3: socket_fd = 6, ip = 127.0.0.1, port = 54323
    //                 ↑ Different FD identifies each connection uniquely


    
    if (ns_socket < 0) {
        fprintf(stderr, "Error: Failed to connect to Name Server\n"); // if ns_socket is negative the socket creation failed, exit with error
        return 1; // [IMP] returning 1 to indicate error
    }
    
    printf("✓ Connected to Name Server\n"); // if connected successfully print message
    fflush(stdout);
    
    // Send registration
    char *reg_msg = create_client_registration_message();
    if (send_message(ns_socket, reg_msg) < 0) {
        fprintf(stderr, "Failed to send registration\n");
        free(reg_msg);
        close_client_socket(ns_socket);
        return 1;
    }
    free(reg_msg);
    
    // Receive acknowledgment
    char *ack = receive_message(ns_socket);
    if (ack) {
        printf("Registration successful\n");
        free(ack);
    } else {
        fprintf(stderr, "No registration acknowledgment\n");
        close_client_socket(ns_socket);
        return 1;
    }
    
    // Enter command loop
    command_loop();
    
    // Cleanup
    close_client_socket(ns_socket);
    
    return 0;
}
