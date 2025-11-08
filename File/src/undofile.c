#include "../include/undofile.h"
#include "../include/file.h"

void create_undofile(char *file_name) {
    chdir(home_dir);
    chdir("undo_dir");
    if (access(file_name, F_OK) == 0) {
        printf("create_undo_file error: undo file already exists\n");
    } else {
        FILE *fptr = fopen(file_name, "w");
        fclose(fptr);
    }
}

// this is part of commit, so full file lock will ensure
// no one else can update undofile
void update_undofile(char *file_name) {
    chdir(home_dir);
    chdir("undo_dir");
    FILE *out_fptr = fopen(file_name, "w");

    chdir(home_dir);
    chdir("file_dir");
    char *write_string = file_to_string(file_name);

    if(out_fptr == NULL) {
        printf("update_undofile error: undo file couldnt open\n");
        goto cleanup;
    }
    if(write_string == NULL) {
        printf("update_undofile error: write string was null\n");
        goto cleanup;
    }

    fprintf(out_fptr, "%s", write_string);

cleanup:
    if(write_string != NULL) {
        free(write_string);
    }
    if(out_fptr != NULL) {
        fclose(out_fptr);
    }
}

// this will also be when the commit lock is activated.
void delete_undo_file(char *file_name) {
    chdir(home_dir);
    chdir("undo_dir");
    if(remove(file_name)) {
        printf("delete_undo_file: some error occurred.\n");
    }
}

// this also needs to change back to file_dir to put back the file
void do_undo(char *file_name) {
    chdir(home_dir);
    chdir("undo_dir");
    char *write_string = file_to_string(file_name);

    chdir(home_dir);
    chdir("file_dir");
    FILE *out_fptr = fopen(file_name, "w");
    if(out_fptr == NULL) {
        printf("do_undo error: actual file couldnt open\n");
        goto cleanup;
    }
    if(write_string == NULL) {
        printf("do_undo error: write string was null\n");
        goto cleanup;
    }
    
    fprintf(out_fptr, "%s", write_string);

cleanup:
    if(write_string != NULL) {
        free(write_string);
    }
    if(out_fptr != NULL) {
        fclose(out_fptr);
    }
}