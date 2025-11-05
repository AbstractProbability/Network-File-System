// test leading whitespaces, trailing whitespaces, multiple sentences, sentences ending without delims, sentences with no words, sentences with only whitespaces etc..
#include "../../File/include/file.h"
#define TESTS 7

int main() {
    char *in_file_name = malloc(sizeof(char) * 6);
    char *out_file_name = malloc(sizeof(char) * 10);
    in_file_name[0] = out_file_name[0] = 't';
    in_file_name[1] = out_file_name[1] = 'e';
    in_file_name[2] = out_file_name[2] = 's';
    in_file_name[3] = out_file_name[3] = 't';
    out_file_name[5] = '_';
    out_file_name[6] = 'o';
    out_file_name[7] = 'u';
    out_file_name[8] = 't';

    in_file_name[5] = '\0';
    out_file_name[9] = '\0';
    for (int i = 1; i<TESTS; i++) {
        in_file_name[4] = out_file_name[4] = i+48;
        print_file(in_file_name, out_file_name);
    }
    return 0;
}