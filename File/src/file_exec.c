#include "../include/file.h"
#include <sys/wait.h>

char* pipeoutput(int fd) {
    char *buffer = malloc(1025);
    ssize_t total_read = 0;
    ssize_t bytes_read;

    // Read into the buffer
    while (total_read < 1025 - 1) {
        bytes_read = read(fd, buffer + total_read, 1024 - total_read);
        
        if (bytes_read == 0) {
            break;
        } else if (bytes_read == -1) {
            printf("pipeout: read error from pipe\n");
            free(buffer);
            return NULL;
        }

        total_read += bytes_read;
    }
    buffer[total_read] = '\0';
    
    return buffer;
}

// get output after executing a line
char* get_output(char *line) {
    int pipefd[2];
    pipe(pipefd);
    wordexp_t commands;
    if(wordexp(line, &commands, 0)) {
        printf("get_output: wordexp error\n");
        return NULL;
    }

    int pid = fork();
    if(pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        execvp(commands.we_wordv[0], commands.we_wordv);
        printf("get_output: Exec failed???\n");
        exit(0);
    } else {
        close(pipefd[1]);
        char *out = pipeoutput(pipefd[0]);
        int status;
        waitpid(pid, &status, 0);
        wordfree(&commands);
        
        return out;
    }
}

// simply find a line and exec it.
// a line may be null, in that case dont send any 
// content in the packet being sent to client.
char** exec_file(char *file_name) {
    FILE *fptr = fopen(file_name, "r");
    if(fptr == NULL) {
        printf("Error: File not found\n");
        return NULL;
    }
    
    char c, last = '\n';
    int nlines = 0;
    while((c = fgetc(fptr)) != EOF) {
        last = c;
        if(c == '\n') {
            nlines++;
        }
    }
    if(last != '\n') {
        nlines++;
    }

    rewind(fptr);
    char **outputs, line[1025];
    outputs = malloc(nlines * sizeof(char *));
    for(int i = 0; i<nlines; i++) {
        fgets(line, 1025, fptr);
        
        int line_len = strlen(line);
        if(line[line_len-1] == '\n') {
            line[line_len-1] = '\0';
            line_len--;
        }

        outputs[i] = get_output(line);
    }
    fclose(fptr);
    return outputs;
}