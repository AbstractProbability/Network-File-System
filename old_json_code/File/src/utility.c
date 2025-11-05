#include "../include/file.h"

int is_delimiter(char c) {
    if (c == '.' || c == '?' || c == '!') {
        return 1; 
    }
    return 0;
}

int is_whitespace(char c) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\b' ||
        c == '\r' || c == '\f' || c == '\v') {
        return 1;
    }
    return 0;
}

/* LLM START */
// Function to read an entire file into a dynamically allocated string.
char* file_to_string(const char* filepath) {
    FILE *file_ptr = fopen(filepath, "r");
    if (!file_ptr) {
        perror("Error opening file");
        return NULL;
    }

    // 1. Find the size of the file
    fseek(file_ptr, 0, SEEK_END);
    long file_size = ftell(file_ptr);
    rewind(file_ptr);

    // 2. Allocate memory for the string
    char *buffer = (char*) malloc(file_size + 1);
    if (!buffer) {
        fprintf(stderr, "Error allocating memory.\n");
        fclose(file_ptr);
        return NULL;
    }

    // 3. Read the file into the buffer
    size_t bytes_read = fread(buffer, 1, file_size, file_ptr);
    if (bytes_read != file_size) {
        fprintf(stderr, "Error reading file.\n");
        fclose(file_ptr);
        free(buffer);
        return NULL;
    }
    buffer[file_size] = '\0';

    fclose(file_ptr);

    return buffer;
}
/* LLM END */

int number_of_words(word_package words) {
    word_node *word_head = words.word_sequence_head;
    word_node *temp = word_head;
    int num_words = 0;

    while (temp != NULL) {
        num_words++;
        temp = temp->next_word_node;
    }
    
    return num_words;
}

int number_of_sentences(sentence_package sentences) {
    sentence_node *sentence_head = sentences.sentences_head;
    sentence_node *temp = sentence_head;
    int num_sentences = 0;

    while (temp != NULL) {
        num_sentences++;
        temp = temp->next_sentence_node;
    }

    return num_sentences;
}

int character_count_word(word_node *word) {
    int cc = 0;
    if (word->word != NULL) {
        cc += strlen(word->word);
    }
    if (word->post_whitespace != NULL) {
        cc += strlen(word->post_whitespace);
    }
    return cc;
}

int character_count_sentence(sentence_node **sentence) {
    int cc = 0;
    // pre_whitespace part
    if ((*sentence)->pre_whitespace != NULL) {
        cc += strlen((*sentence)->pre_whitespace);
    }
    // whole sentence
    word_node *temp = (*sentence)->words.word_sequence_head;
    while (temp != NULL) {
        cc += character_count_word(temp);
        temp = temp->next_word_node;
    }

    return cc;
}