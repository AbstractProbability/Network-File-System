#include "ns_filemanager.h"
#include <stdio.h>

int main() {
    printf("=== Testing NS File Manager ===\n\n");
    
    // Test 1: Active Users List
    printf("Test 1: Active Users List\n");
    active_users_list *users = create_active_users_list();
    
    add_active_user(users, "alice");
    add_active_user(users, "bob");
    add_active_user(users, "charlie");
    
    print_active_users(users);
    
    printf("Is 'alice' active? %s\n", is_user_active(users, "alice") ? "YES" : "NO");
    printf("Is 'david' active? %s\n\n", is_user_active(users, "david") ? "YES" : "NO");
    
    remove_active_user(users, "bob");
    printf("After removing 'bob':\n");
    print_active_users(users);
    
    // Test 2: File Path List
    printf("\n\nTest 2: File Path List\n");
    file_path_list *files = create_file_path_list();
    
    // Add some files
    add_file_path(files, "/home/user/document.txt", "SS1", "127.0.0.1", 5001);
    add_file_path(files, "/home/user/notes.txt", "SS1", "127.0.0.1", 5001);
    add_file_path(files, "/home/user/notes.txt", "SS2", "127.0.0.1", 5002); // Same file on SS2
    add_file_path(files, "/home/user/report.txt", "SS2", "127.0.0.1", 5002);
    
    print_file_path_list(files);
    
    // Test 3: Get Active SS for File
    printf("\n\nTest 3: Get Active SS for File\n");
    
    file_request_result *result = get_active_ss_for_file(files, "/home/user/notes.txt");
    if (result) {
        printf("Found active SS for '/home/user/notes.txt':\n");
        printf("  SS Name: %s\n", result->ss_name);
        printf("  SS IP: %s\n", result->ss_ip);
        printf("  SS Port: %d\n", result->ss_client_port);
        
        if (result->file_info) {
            printf("  File Info:\n");
            printf("    Owner: %s\n", result->file_info->owner);
            printf("    Word Count: %d\n", result->file_info->wc);
            printf("    Size: %d bytes\n", result->file_info->size);
        } else {
            printf("  File Info: Not available (expected - info file doesn't exist in test)\n");
        }
        
        free_file_request_result(result);
    } else {
        printf("Failed to get SS for file\n");
    }
    
    // Test 4: Mark SS as DOWN and retry
    printf("\n\nTest 4: Mark SS1 as DOWN\n");
    mark_ss_status(files, "SS1", 0);
    print_file_path_list(files);
    
    printf("\nTrying to get SS for '/home/user/notes.txt' (SS1 is down, should return SS2):\n");
    result = get_active_ss_for_file(files, "/home/user/notes.txt");
    if (result) {
        printf("  SS Name: %s (Expected: SS2)\n", result->ss_name);
        printf("  SS IP: %s\n", result->ss_ip);
        printf("  SS Port: %d\n", result->ss_client_port);
        free_file_request_result(result);
    }
    
    // Test 5: Try to get file with no active SS
    printf("\n\nTest 5: Mark all SS as DOWN for '/home/user/document.txt'\n");
    mark_ss_status(files, "SS1", 0);
    result = get_active_ss_for_file(files, "/home/user/document.txt");
    if (result) {
        printf("  Unexpectedly found SS\n");
        free_file_request_result(result);
    } else {
        printf("  Correctly returned NULL (no active SS)\n");
    }
    
    // Cleanup
    free_active_users_list(users);
    free_file_path_list(files);
    
    printf("\n\n=== All tests completed ===\n");
    return 0;
}
