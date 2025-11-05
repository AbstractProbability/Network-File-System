#include "../include/file.h"
#include "../include/undofile.h"
#include "../include/checkpointfile.h"
#include "../include/infofile.h"

// access must be checked before calling these
// lock must be held when this is happening
void delete_file(char *file_name) {
    chdir(home_dir);
    chdir("file_dir");
    if(remove(file_name) != 0) {
        printf("Couldnt delete file\n");
        return;
    }
    delete_info_file(file_name);
    delete_undo_file(file_name);
    for(int i = 1; i<=MAX_CHECKPOINTS; i++) {
        delete_checkpoint_file(file_name, i);
    }
}

void create_file(char *file_name, char *user_name) {
    chdir(home_dir);
    chdir("file_dir");
    FILE *fptr = fopen(file_name, "w");
    fclose(fptr);

    create_info_file(file_name, user_name);
    create_undofile(file_name);
}