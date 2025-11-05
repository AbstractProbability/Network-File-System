#include "../include/file.h"

// Each word preserves whitespace after it
// this assumes that string[start_idx] is not a whitespace
word_package split_into_words(char *string, int start_idx, int end_idx) {
    int idx = start_idx, num_words = 0;
    word_node *head = NULL;
    word_node *tail = head;
    word_package ret_pack = new_word_package();

    while (idx < end_idx && !is_delimiter(string[idx])) {
        // make a new node at the end
        word_node *new_node = new_word_node(NULL);
        append_word_node(&head, &tail, &new_node);

        // get word
        while (idx < end_idx && !is_whitespace(string[idx])) {
            idx++;
        }
        // word: string[start_idx, idx]
        int size = idx-start_idx;
        tail->is_valid = 1;
        tail->word = malloc(sizeof(char) * (size+1));
        while (start_idx < idx) {
            tail->word[size - (idx-start_idx)] = string[start_idx];
            start_idx++;
        }
        tail->word[size] = '\0';

        // get following whitespace
        while (idx < end_idx && is_whitespace(string[idx])) {
            idx++;
        }
        // following whitespace: string[start_idx, idx]
        size = idx-start_idx;
        tail->post_whitespace = malloc(sizeof(char) * (size+1));
        while (start_idx < idx) {
            tail->post_whitespace[size - (idx-start_idx)] = string[start_idx];
            start_idx++;
        }
        tail->post_whitespace[size] = '\0';

        num_words++;
    }

    ret_pack.word_sequence_head = head;
    ret_pack.word_sequence_tail = tail;
    ret_pack.wc = num_words;

    return ret_pack;
}

sentence_package tokenise(char *string) {
    int idx = 0, start_idx = 0, string_len = strlen(string), num_sentences = 0;
    sentence_node *head = NULL;
    sentence_node *tail = head;
    sentence_package ret_pack = new_sentence_package();

    while (idx < string_len) {
        // make new sentence node
        sentence_node *new_node = new_sentence_node();
        append_sentence_node(&head, &tail, &new_node);

        // find sentence
        while (string[idx] != '\0' && !is_delimiter(string[idx])) {
            idx++;
        }

        // preserve leading whitespace
        int temp = start_idx;
        while (string[temp] != '\0' && is_whitespace(string[temp])) {
            temp++;
        }

        int size = temp-start_idx;
        tail->pre_whitespace = malloc(sizeof(char) * (size+1));
        while (start_idx < temp) {
            tail->pre_whitespace[size - (temp-start_idx)] = string[start_idx];
            start_idx++;
        }
        tail->pre_whitespace[size] = '\0';

        // put the words, rest of fields
        tail->punct = string[idx];
        tail->is_valid = 1;
        tail->words = split_into_words(string, start_idx, idx);
        num_sentences++;

        idx++;           // skip delimiter
        start_idx = idx; // re-init start_idx;
    }

    ret_pack.sentences_head = head;
    ret_pack.sentences_tail = tail;
    ret_pack.sc = num_sentences;

    return ret_pack;
}

sentence_package tokenise_file(char *file_name) {
    char *string = file_to_string(file_name);
    sentence_package ret = tokenise(string);
    // free(string);
    return ret;
}