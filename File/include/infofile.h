#ifndef INFOFILE_H
#define INFOFILE_H

#include "../../common.h"

// this should have functions to interact with and use
// the info files.

typedef struct info_file {
    int wc;
    int size;               // file size/ character count
    int last_access_time;
    // int creation_time;
    char *owner;
    char **r_access_users; // viewing
    char **w_access_users; // editing
    char **x_access_users; // executing
} info_file;

void create_info_file(char *file_name, char *user_name);
void delete_info_file(char *file_name);
void update_access(char *file_name, char *username, char access_type);
void remove_access(char *file_name, char *username, char access_type);
void update_wc(char *file_name, int new_wc);
void update_size(char* file_name, int new_size);
void update_last_access_time(char *file_name, int new_last_access_time);
// void update_creation_time(char *file_name); // adds the creation time when called

info_file *read_info_file(char *file_name);
void free_info_file(info_file *info);
int query_user_info(char *username, char *file_name, char access_type);

#endif