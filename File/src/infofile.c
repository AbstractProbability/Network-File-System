#include "../include/infofile.h"

// does 'username' have 'access_type' access to 'file_name'?
int query_user_info(char *user_name, char *file_name, char access_type) {
    // 1. Read the info file into the struct.
    // This function already handles chdir and file existence checks.
    info_file *info = read_info_file(file_name);
    
    // 2. Handle error (file not found or malformed)
    if (info == NULL) {
        return -1;
    }

    // 3. Select the correct access list based on access_type
    char **target_list = NULL;
    switch (access_type) {
    case 'r':
        target_list = info->r_access_users;
        break;
    case 'w':
        target_list = info->w_access_users;
        break;
    case 'x':
        target_list = info->x_access_users;
        break;
    default:
        fprintf(stderr, "query_user_info error: Invalid access type '%c'\n", access_type);
        free_info_file(info); // Clean up memory
        return -1; // Return error
    }

    // 4. Iterate through the list and check for the username
    int has_access = 0; // Assume no access by default
    for (int i = 0; target_list[i] != NULL; i++) {
        if (strcmp(target_list[i], user_name) == 0) {
            has_access = 1; // User found!
            break;          // Stop searching
        }
    }

    // 5. Clean up and return the result
    free_info_file(info);
    return has_access;
}

void delete_info_file(char *file_name) {

}

void create_info_file(char *file_name, char *user_name) {
    chdir(home_dir);
    chdir("info_dir");
    if (access(file_name, F_OK) == 0) {
        printf("create_info_file error: info file already exists\n");
    } else {
        FILE *fptr = fopen(file_name, "w");
        fprintf(fptr, "wc=0;cc=0;last_access_time=0;owner=%s;r_access_users=%s,;w_access_users=%s,x_access_users=%s,;",
                user_name, user_name, user_name, user_name);
        fclose(fptr);
    }
}

char** parse_user_list(const char* list_str) {
    // Count commas to determine how many pointers to allocate
    int user_count = 0;
    for (int i = 0; list_str[i] != '\0'; i++) {
        if (list_str[i] == ',') {
            user_count++;
        }
    }

    // Allocate space for the array of char pointers, plus one for the NULL terminator
    char** users = malloc((user_count + 1) * sizeof(char*));
    if (users == NULL) {
        perror("malloc failed for user list");
        return NULL;
    }

    // strtok modifies the string it parses, so we must work on a copy
    char* list_copy = strdup(list_str);
    if (list_copy == NULL) {
        free(users);
        return NULL;
    }

    int i = 0;
    char* token = strtok(list_copy, ",");
    while (token != NULL) {
        users[i++] = strdup(token); // Allocate space and copy the username
        token = strtok(NULL, ",");
    }
    users[i] = NULL; // Add the NULL terminator to mark the end of the list

    free(list_copy); // Free the temporary copy
    return users;
}

char* join_user_list(char** users) {
    if (users == NULL || users[0] == NULL) {
        // Return an empty but valid, allocated string if the list is empty
        char* empty_list = malloc(1);
        empty_list[0] = '\0';
        return empty_list;
    }

    // Calculate the total length needed for the final string
    size_t total_len = 0;
    for (int i = 0; users[i] != NULL; i++) {
        total_len += strlen(users[i]) + 1; // +1 for the comma
    }
    total_len += 1; // For the final null terminator

    char* result = malloc(total_len);
    if (result == NULL) return NULL;
    result[0] = '\0'; // Start with an empty string

    // Concatenate each user and a comma
    for (int i = 0; users[i] != NULL; i++) {
        strcat(result, users[i]);
        strcat(result, ",");
    }

    return result;
}

info_file *read_info_file(char *file_name) {
    chdir(home_dir);
    chdir("info_dir");

    if (access(file_name, F_OK) != 0) {
        printf("read_info_file error: info file doesnt exist\n");
        return NULL;
    }

    FILE *fptr = fopen(file_name, "r");
    if (fptr == NULL) {
        perror("read_info_file error: could not open file");
        return NULL;
    }

    info_file *ret = malloc(sizeof(info_file));
    if (ret == NULL) {
        fclose(fptr);
        perror("malloc failed for info_file struct");
        return NULL;
    }

    // Temporary buffers to hold the string data read from the file.
    // The numbers like %255[^;] prevent buffer overflows.
    char owner_buf[256];
    char r_users_buf[1024];
    char w_users_buf[1024];
    char x_users_buf[1024];

    // Use a single fscanf call to parse the entire file based on its format
    int items_scanned = fscanf(fptr,
        "wc=%d;cc=%d;last_access_time=%d;owner=%255[^;];r_access_users=%1023[^;];w_access_users=%1023[^;];x_access_users=%1023[^;];",
        &ret->wc, &ret->cc, &ret->last_access_time,
        owner_buf, r_users_buf, w_users_buf, x_users_buf);

    fclose(fptr); // We are done with the file, so close it immediately.

    if (items_scanned != 7) {
        fprintf(stderr, "Error: Malformed info file '%s'. Expected 7 fields, found %d.\n", file_name, items_scanned);
        free(ret);
        return NULL;
    }

    // Now, properly allocate memory for the struct members and copy/parse the data
    ret->owner = strdup(owner_buf);

    // Use our helper function to parse the user lists
    ret->r_access_users = parse_user_list(r_users_buf);
    ret->w_access_users = parse_user_list(w_users_buf);
    ret->x_access_users = parse_user_list(x_users_buf);

    return ret;
}

int write_info_file(char* file_name, info_file* info) {
    // Navigate to the correct directory first
    chdir(home_dir);
    chdir("info_dir");

    FILE* fptr = fopen(file_name, "w");
    if (fptr == NULL) {
        perror("write_info_file failed to open file");
        return -1;
    }

    // Join the user lists back into strings
    char* r_users_str = join_user_list(info->r_access_users);
    char* w_users_str = join_user_list(info->w_access_users);
    char* x_users_str = join_user_list(info->x_access_users);

    // Write the formatted data to the file
    fprintf(fptr, "wc=%d;cc=%d;last_access_time=%d;owner=%s;r_access_users=%s;w_access_users=%s;x_access_users=%s;",
            info->wc, info->cc, info->last_access_time,
            info->owner, r_users_str, w_users_str, x_users_str);

    // Clean up
    fclose(fptr);
    free(r_users_str);
    free(w_users_str);
    free(x_users_str);

    return 0;
}

void free_info_file(info_file *info) {
    if (info == NULL) return;

    free(info->owner);

    // Free each string in the user list, then the list itself
    if (info->r_access_users != NULL) {
        for (int i = 0; info->r_access_users[i] != NULL; i++) {
            free(info->r_access_users[i]);
        }
        free(info->r_access_users);
    }
    
    if (info->w_access_users != NULL) {
        for (int i = 0; info->w_access_users[i] != NULL; i++) {
            free(info->w_access_users[i]);
        }
        free(info->w_access_users);
    }

    if (info->x_access_users != NULL) {
        for (int i = 0; info->x_access_users[i] != NULL; i++) {
            free(info->x_access_users[i]);
        }
        free(info->x_access_users);
    }

    // Finally, free the struct itself
    free(info);
}

void update_access(char* file_name, char* username, char access_type) {
    info_file* info = read_info_file(file_name);
    if (info == NULL) return; // Error is printed by read_info_file

    char*** target_list_ptr = NULL; // Pointer to the list we want to modify
    switch (access_type) {
        case 'r': target_list_ptr = &info->r_access_users; break;
        case 'w': target_list_ptr = &info->w_access_users; break;
        case 'x': target_list_ptr = &info->x_access_users; break;
        default:
            fprintf(stderr, "update_access error: Invalid access type '%c'\n", access_type);
            goto cleanup; // Go to the cleanup section at the end
    }

    // Count existing users and check for duplicates
    int user_count = 0;
    for (int i = 0; (*target_list_ptr)[i] != NULL; i++) {
        if (strcmp((*target_list_ptr)[i], username) == 0) {
            printf("User '%s' already has '%c' access.\n", username, access_type);
            goto cleanup; // User already exists, no changes needed
        }
        user_count++;
    }

    // Reallocate the array to make space for one more user + NULL terminator
    *target_list_ptr = realloc(*target_list_ptr, (user_count + 2) * sizeof(char*));
    if (*target_list_ptr == NULL) {
        perror("realloc failed");
        goto cleanup;
    }

    // Add the new user
    (*target_list_ptr)[user_count] = strdup(username);
    (*target_list_ptr)[user_count + 1] = NULL; // Add the new NULL terminator

    // Write the updated data back to the file
    write_info_file(file_name, info);

cleanup:
    free_info_file(info); // Free all memory allocated by read_info_file
}

void remove_access(char* file_name, char* username, char access_type) {
    info_file* info = read_info_file(file_name);
    if (info == NULL) return;

    char** target_list = NULL;
    switch (access_type) {
        case 'r': target_list = info->r_access_users; break;
        case 'w': target_list = info->w_access_users; break;
        case 'x': target_list = info->x_access_users; break;
        default:
            fprintf(stderr, "remove_access error: Invalid access type '%c'\n", access_type);
            goto cleanup;
    }
    
    int user_index = -1;
    int user_count = 0;
    for(int i = 0; target_list[i] != NULL; i++) {
        if (strcmp(target_list[i], username) == 0) {
            user_index = i;
        }
        user_count++;
    }

    if (user_index == -1) {
        printf("User '%s' not found for '%c' access.\n", username, access_type);
        goto cleanup; // User not in list, nothing to do
    }
    
    // Free the memory for the username we are removing
    free(target_list[user_index]);

    // Shift all subsequent elements one position to the left
    for (int i = user_index; i < user_count; i++) {
        target_list[i] = target_list[i + 1]; // The last element will be NULL
    }

    // Write the modified data back
    write_info_file(file_name, info);

cleanup:
    free_info_file(info);
}

void update_wc(char* file_name, int new_wc) {
    info_file* info = read_info_file(file_name);
    if (info == NULL) return;

    info->wc = new_wc; // Modify the struct in memory

    write_info_file(file_name, info); // Write it back
    free_info_file(info);             // Clean up
}

void update_cc(char* file_name, int new_cc) {
    info_file* info = read_info_file(file_name);
    if (info == NULL) return;

    info->cc = new_cc; // Modify the struct in memory

    write_info_file(file_name, info); // Write it back
    free_info_file(info);             // Clean up
}

void update_last_access_time(char* file_name, int new_last_access_time) {
    info_file* info = read_info_file(file_name);
    if (info == NULL) return;

    info->last_access_time = new_last_access_time; // Modify the struct in memory

    write_info_file(file_name, info); // Write it back
    free_info_file(info);             // Clean up
}