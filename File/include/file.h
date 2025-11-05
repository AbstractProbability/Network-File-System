#ifndef FILE_H
#define FILE_H
#include "../../common.h"
#include <ctype.h>

typedef struct word_node {
    char *word;
    char *post_whitespace;                      // whitespace after the word
    int is_valid;
    struct word_node *next_word_node;
    struct word_node *prev_word_node;
} word_node;

// All words in a sentence, including word_head and word_tail;
typedef struct word_package {
    struct word_node *word_sequence_head;
    struct word_node *word_sequence_tail;
    int wc;                                     // number of words in this sentence
} word_package;

typedef struct sentence_node {
    struct word_package words;
    char *pre_whitespace;                       // whitespace at the beginning of a sentence
    char punct;                                 // \0 for no punct, otherwise ./?/! 
    int is_valid;                               // is this sentence a placeholder (to append) or has content?
    struct sentence_node *next_sentence_node;
    struct sentence_node *prev_sentence_node;
    pthread_mutex_t lock;                       // write lock for this sentence
    char *lock_holder_username;
} sentence_node;

// All sentences, including sentence_head and sentence_tail.
// The whole file is represented by one sentence_package
typedef struct sentence_package {
    struct sentence_node *sentences_head;
    struct sentence_node *sentences_tail;
    int sc;                                     // number of sentences in this package
    pthread_mutex_t global_lock;
} sentence_package;

// utility.c
int     is_delimiter(char c);
int     is_whitespace(char c);
char*   file_to_string(const char* filepath);
int     number_of_words(word_package words);
int     number_of_sentences(sentence_package sentences);
int     character_count_word(word_node *word);
int     character_count_sentence(sentence_node **sentence);

// ll_functions.c
/* init */
word_node*          new_word_node(char *string);
word_package        new_word_package();
sentence_node*      new_sentence_node();
sentence_package    new_sentence_package();

/* word_node ll functions */
word_node*          get_word_head(word_node *sen_node);
word_node*          get_word_tail(word_node *sen_node);
word_node*          get_word_at_index(word_node **p_head, int index);
void                append_word_node(word_node **p_head, word_node **p_tail, word_node **p_to_be_added);
void                delete_word_node(word_node **p_head, word_node **p_tail, word_node **p_to_be_deleted);
// void                insert_word_node_at(word_node **p_head, word_node **p_tail, word_node **p_to_be_added, int idx);
void                free_word_node(word_node *word);

/* sentence_node ll functions */
sentence_node*      get_sentence_head(sentence_node *sen_node);
sentence_node*      get_sentence_tail(sentence_node *sen_node);
sentence_node*      get_sentence_at_index(sentence_node **p_head, int index);
void                append_sentence_node(sentence_node **p_head, sentence_node **p_tail, sentence_node **p_to_be_added);
void                delete_sentence_node(sentence_node **p_head, sentence_node **p_tail, sentence_node **p_to_be_deleted);
// void                insert_sentence_node_at(sentence_node **p_head, sentence_node **p_tail, sentence_node **p_to_be_added, int idx);
void                free_sentence_node(sentence_node *sentence);
void                free_sentence_package(sentence_package sentences);

/* print functions */
void                print_word(word_node *word, FILE *fptr);
void                print_sentence(sentence_node *sentence, FILE *fptr);
void                print_sentence_pack(sentence_package sentences, FILE *fptr);
void                print_file(char *in_file_name, char *out_file_name);

// tokeniser.c
word_package        split_into_words(char *string, int start_idx, int end_idx);
sentence_package    tokenise(char *string);
sentence_package    tokenise_file(char *file_name);

// file_write.c
int                 character_count_sentence_and_string(sentence_node **sentence, char *string, int word_index);
char*               join_string_at_index(sentence_node **sentence, char *string, int word_index);
void                join_sentence_with_sentence_package(sentence_node **sentence, sentence_package *p_new_pack);
int                 update_sentence_at_index(sentence_node **sentence, char *string, int word_index);

//file_exec.c
char*               pipeoutput(int fd);
char*               get_output(char *line);
char**              exec_file(char *file_name);

// file_create_delete.c
void                delete_file(char *file_name);
void                create_file(char *file_name, char *user_name);

#endif