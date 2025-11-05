#ifndef CHECKPOINT_FILE_H
#define CHECKPOINT_FILE_H
#include "../../common.h"
#include "./infofile.h"

#define MAX_CHECKPOINTS 3

// checkpoint tags must be numbers 1-MAX_CHECKPOINTS

char* get_checkpoint_file_name(char *file_name, int tag);
void create_checkpoint_file(char *file_name, int tag);
void delete_checkpoint_file(char *file_name, int tag);

char* view_checkpoint_file(char *file_name, int tag);

void list_checkpoints(char *file_name);
void do_revert(char *file_name, int tag);

#endif