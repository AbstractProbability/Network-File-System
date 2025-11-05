#include "../include/checkpointfile.h"

char* get_checkpoint_file_name(char *file_name, int tag) {
    int file_name_len = strlen(file_name);
    char *checkpoint_file_name = malloc(sizeof(char) * (file_name_len+15));
    snprintf(checkpoint_file_name, file_name_len+14, "%s%d", file_name, tag);

    return checkpoint_file_name;
}

void create_checkpoint_file(char *file_name, int tag) {
    chdir(home_dir);
    chdir("checkpoint_dir");
    if(tag < 1 || tag > MAX_CHECKPOINTS) {
        printf("create_checkpoint_file: tags must be between 1 and %d", MAX_CHECKPOINTS);
        return;
    }

    // string manipulation to create <file_name><tag>
    char *checkpoint_file_name = get_checkpoint_file_name(file_name, tag);
    if(checkpoint_file_name == NULL) {
        printf("checkpoint file name generation failed\n");
        return;
    }

    // writing contents of filename to checkpoint file
    FILE *out_fptr = fopen(checkpoint_file_name, "w");

    chdir(home_dir);
    chdir("file_dir");
    char *write_string = file_to_string(file_name);

    if(out_fptr == NULL) {
        printf("create_checkpoint_file error: undo file couldnt open\n");
        goto cleanup;
    }
    if(write_string == NULL) {
        printf("create_checkpoint_file error: write string was null\n");
        goto cleanup;
    }

    fprintf(out_fptr, "%s", write_string);

cleanup:
    free(checkpoint_file_name);
    if(write_string != NULL) {
        free(write_string);
    }
    if(out_fptr != NULL) {
        fclose(out_fptr);
    }
}

void delete_checkpoint_file(char *file_name, int tag) {
    chdir(home_dir);
    chdir("checkpoint_dir");
    char *checkpoint_file_name = get_checkpoint_file_name(file_name, tag);
    remove(checkpoint_file_name);
    free(checkpoint_file_name);
}

char* view_checkpoint_file(char *file_name, int tag) {
    chdir(home_dir);
    chdir("checkpoint_dir");
    char *checkpoint_file_name = get_checkpoint_file_name(file_name, tag);
    char *out = file_to_string(checkpoint_file_name);
    free(checkpoint_file_name);
    return out;
}

void list_checkpoints(char *file_name) {
    chdir(home_dir);
    chdir("checkpoint_dir");

    for (int i = 1; i <= MAX_CHECKPOINTS; i++) {
        char *current_file_name = get_checkpoint_file_name(file_name, i);
        FILE *fptr = fopen(current_file_name, "r");

        if (fptr != NULL) {
            printf("Checkpoint %d: %s\n", i, current_file_name);
            fclose(fptr);
        }
        free(current_file_name);
    }
}

// this also needs to change back to file_dir to put back the file
void do_revert(char *file_name, int tag) {
    chdir(home_dir);
    chdir("checkpoint_dir");
    char *checkpoint_file_name = get_checkpoint_file_name(file_name, tag);
    char *write_string = file_to_string(checkpoint_file_name);
    
    chdir(home_dir);
    chdir("file_dir");
    FILE *out_fptr = fopen(file_name, "w");
    if(out_fptr == NULL) {
        printf("do_revert error: actual file couldnt open\n");
        goto cleanup;
    }
    if(write_string == NULL) {
        printf("do_revert error: write string was null\n");
        goto cleanup;
    }
    
    fprintf(out_fptr, "%s", write_string);

cleanup:
    free(checkpoint_file_name);
    if(write_string != NULL) {
        free(write_string);
    }
    if(out_fptr != NULL) {
        fclose(out_fptr);
    }
}