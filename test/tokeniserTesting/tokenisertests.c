#include "../../include/common.h" // For MAX_WORD_LEN, etc.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <ctype.h>

// --- Copied Structs from ss/ss.h ---
typedef struct WordNode {
    char word[MAX_WORD_LEN];
    char* trailing_whitespace;
    struct WordNode* next;
} WordNode;

typedef struct SentenceNode {
    char* leading_whitespace;
    WordNode* words;
    char punctuation; // '.', '?', '!', or '\0'
    struct SentenceNode* next;
    pthread_mutex_t lock;
} SentenceNode;

typedef struct OpenFile {
    char path[MAX_PATH_LEN];
    SentenceNode* sentences;
    pthread_rwlock_t global_lock;
    struct OpenFile* next;
} OpenFile;

// --- Copied Helpers from ss/ss_ops.c ---
void str_append(char** str, char c) {
    if (*str == NULL) {
        *str = (char*)malloc(2);
        (*str)[0] = c;
        (*str)[1] = '\0';
    } else {
        size_t len = strlen(*str);
        *str = (char*)realloc(*str, len + 2);
        (*str)[len] = c;
        (*str)[len + 1] = '\0';
    }
}

typedef enum {
    STATE_PRE_SENTENCE,
    STATE_IN_WORD,
    STATE_POST_WORD
} ParserState;

// --- Copied 'load_file_to_memory' from ss/ss_ops.c ---
OpenFile* load_file_to_memory(const char* path) {
    printf("TEST: Loading file '%s' into memory.\n", path);
    
    FILE* f = fopen(path, "r");
    if (f == NULL) {
        perror("Error opening input file");
        return NULL;
    }

    OpenFile* file = (OpenFile*)calloc(1, sizeof(OpenFile));
    strncpy(file->path, path, MAX_PATH_LEN);
    pthread_rwlock_init(&file->global_lock, NULL);
    
    SentenceNode* current_sentence = (SentenceNode*)calloc(1, sizeof(SentenceNode));
    pthread_mutex_init(&current_sentence->lock, NULL);
    file->sentences = current_sentence;
    
    WordNode* current_word = NULL;
    char word_buffer[MAX_WORD_LEN];
    int word_index = 0;
    
    ParserState state = STATE_PRE_SENTENCE;
    int c;

    while ((c = fgetc(f)) != EOF) {
        int is_punc = (c == '.' || c == '?' || c == '!');
        int is_space = isspace(c);

        if (state == STATE_PRE_SENTENCE) {
            if (is_space) {
                str_append(&current_sentence->leading_whitespace, c);
            } else if (is_punc) {
                current_sentence->punctuation = c;
                SentenceNode* new_s = (SentenceNode*)calloc(1, sizeof(SentenceNode));
                pthread_mutex_init(&new_s->lock, NULL);
                current_sentence->next = new_s;
                current_sentence = new_s;
            } else {
                word_index = 0;
                word_buffer[word_index++] = c;
                state = STATE_IN_WORD;
            }
        } else if (state == STATE_IN_WORD) {
            if (is_space) {
                word_buffer[word_index] = '\0';
                current_word = (WordNode*)calloc(1, sizeof(WordNode));
                strcpy(current_word->word, word_buffer);
                
                WordNode* tail = current_sentence->words;
                if (tail == NULL) current_sentence->words = current_word;
                else { while (tail->next != NULL) tail = tail->next; tail->next = current_word; }
                
                str_append(&current_word->trailing_whitespace, c);
                state = STATE_POST_WORD;
            } else if (is_punc) {
                word_buffer[word_index] = '\0';
                current_word = (WordNode*)calloc(1, sizeof(WordNode));
                strcpy(current_word->word, word_buffer);
                
                WordNode* tail = current_sentence->words;
                if (tail == NULL) current_sentence->words = current_word;
                else { while (tail->next != NULL) tail = tail->next; tail->next = current_word; }
                
                current_sentence->punctuation = c;
                SentenceNode* new_s = (SentenceNode*)calloc(1, sizeof(SentenceNode));
                pthread_mutex_init(&new_s->lock, NULL);
                current_sentence->next = new_s;
                current_sentence = new_s;
                state = STATE_PRE_SENTENCE;
            } else {
                if (word_index < MAX_WORD_LEN - 1) {
                    word_buffer[word_index++] = c;
                }
            }
        } else if (state == STATE_POST_WORD) {
            if (is_space) {
                str_append(&current_word->trailing_whitespace, c);
            } else if (is_punc) {
                current_sentence->punctuation = c;
                SentenceNode* new_s = (SentenceNode*)calloc(1, sizeof(SentenceNode));
                pthread_mutex_init(&new_s->lock, NULL);
                current_sentence->next = new_s;
                current_sentence = new_s;
                state = STATE_PRE_SENTENCE;
            } else {
                word_index = 0;
                word_buffer[word_index++] = c;
                state = STATE_IN_WORD;
            }
        }
    }
    
    if (state == STATE_IN_WORD && word_index > 0) {
        word_buffer[word_index] = '\0';
        current_word = (WordNode*)calloc(1, sizeof(WordNode));
        strcpy(current_word->word, word_buffer);
        WordNode* tail = current_sentence->words;
        if (tail == NULL) current_sentence->words = current_word;
        else { while (tail->next != NULL) tail = tail->next; tail->next = current_word; }
    }

    fclose(f);
    return file;
}


// --- NEW: Simplified serializer (based on commit_file_to_disk) ---
int save_struct_to_file(OpenFile* file, const char* output_path) {
    printf("TEST: Writing in-memory struct to '%s'.\n", output_path);

    FILE* f_out = fopen(output_path, "w");
    if (f_out == NULL) {
        perror("Error opening output file");
        return -1;
    }
    
    SentenceNode* s = file->sentences;
    while (s != NULL) {
        // Don't write the trailing empty node
        if (s->next == NULL && s->words == NULL && s->leading_whitespace == NULL) {
            break;
        }
        
        if (s->leading_whitespace) {
            fprintf(f_out, "%s", s->leading_whitespace);
        }
        
        WordNode* w = s->words;
        while (w != NULL) {
            fprintf(f_out, "%s", w->word);
            if (w->trailing_whitespace) {
                fprintf(f_out, "%s", w->trailing_whitespace);
            }
            w = w->next;
        }
        
        if (s->punctuation != '\0') {
            fputc(s->punctuation, f_out);
        }
        
        s = s->next;
    }
    fclose(f_out);
    return 0;
}

// --- Main Test Function ---
int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input_file> <output_file>\n", argv[0]);
        return 1;
    }

    char* infile = argv[1];
    char* outfile = argv[2];

    // 1. Tokenize
    OpenFile* file = load_file_to_memory(infile);
    if (file == NULL) {
        fprintf(stderr, "Failed to parse file.\n");
        return 1;
    }

    // 2. Serialize
    if (save_struct_to_file(file, outfile) != 0) {
        fprintf(stderr, "Failed to write file.\n");
        return 1;
    }

    printf("Roundtrip test complete. Compare '%s' and '%s'.\n", infile, outfile);

    // TODO: Free memory (free_sentence_list)
    return 0;
}