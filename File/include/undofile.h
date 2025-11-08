#ifndef UNDOFILE_H
#define UNDOFILE_H
#include "../../common.h"
#include "./infofile.h"

void create_undofile(char *file_name);
void update_undofile(char *file_name);
void delete_undo_file(char *file_name);
void do_undo(char *file_name);

#endif