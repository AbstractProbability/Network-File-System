#include "../include/file.h"
#include "../include/undofile.h"
#include "../include/checkpointfile.h"
#include "../include/infofile.h"

// access must be checked before calling these
void delete_file(char *file_path);
void create_file(char *file_path);