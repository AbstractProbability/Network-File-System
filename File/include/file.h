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
} sentence_package;

// utility
int is_delimiter(char c);
int is_whitespace(char c);

// init
sentence_node *new_sentence_node();
sentence_package new_sentence_package();
word_node *new_word_node(char *string);
word_package new_word_package();

// sentence_node ll functions
sentence_node *get_sentence_head(sentence_node *sen_node);
sentence_node *get_sentence_tail(sentence_node *sen_node);
void delete_sentence_node(sentence_node **p_head, sentence_node **p_tail, sentence_node **p_to_be_deleted);
void append_sentence_node(sentence_node **p_head, sentence_node **p_tail, sentence_node **p_to_be_added);
void insert_sentence_node_at(sentence_node **p_head, sentence_node **p_tail, sentence_node **p_to_be_added, int idx);

// word_node ll functions
word_node *get_word_head(word_node *sen_node);
word_node *get_word_tail(word_node *sen_node);
void delete_word_node(word_node **p_head, word_node **p_tail, word_node **p_to_be_deleted);
void append_word_node(word_node **p_head, word_node **p_tail, word_node **p_to_be_added);
void insert_word_node_at(word_node **p_head, word_node **p_tail, word_node **p_to_be_added, int idx);

// tokeniser functions
word_package split_into_words(char *string, int start_idx, int end_idx);
sentence_package tokenise(char *string);
sentence_node *tokenise_file(char *file_name);
// print the tokens to file_name.
void token_printer();

// write at a certain word index in the sentence
void write_at(sentence_node **p_sentence, char *string, int word_index);


#endif