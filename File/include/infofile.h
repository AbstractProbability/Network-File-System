#include "../../common.h"

// this should have functions to interact with ans use
// the info files.

typedef struct info_file {
    int wc;
    int cc;
    int last_access_time;
    char *owner;
    char **r_access_users;
    char **w_access_users;
    char **x_access_users;
} info_file;

void create_info_file(char *file_name, char *user_name);
void update_access(char *file_name, char *username, char access_type);
void remove_access(char *file_name, char *username, char access_type);
void update_wc(char *file_name, int new_wc);
void update_cc(char *file_name, int new_cc);
void update_last_access_time(char *file_name, int new_last_access_time);

info_file *read_info_file(char *file_name);
void free_info_file(info_file *info);
int query_user_info(char *username, char *file_name, char access_type);