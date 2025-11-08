#include "../include/file.h"

word_node* new_word_node(char *string) {
    word_node *new_node = malloc(sizeof(word_node));
    new_node->word = string;

    new_node->is_valid = 0;
    
    new_node->prev_word_node = NULL;
    new_node->next_word_node = NULL;
    return new_node;
}

word_package new_word_package() {
    word_package new_package = {NULL, NULL, 0};
    return new_package;
}

sentence_node* new_sentence_node() {
    sentence_node *new_node = malloc(sizeof(sentence_node));
    new_node->words = new_word_package();

    new_node->punct = '\0';
    new_node->is_valid = 0;
    new_node->pre_whitespace = NULL;
    
    new_node->prev_sentence_node = NULL;
    new_node->next_sentence_node = NULL;
    
    pthread_mutex_init(&new_node->lock, NULL);
    new_node->lock_holder_username = NULL;
    return new_node;
}

sentence_package new_sentence_package() {
    sentence_package new_package;
    new_package.sc = 0;
    new_package.sentences_head = NULL;
    new_package.sentences_tail = NULL;
    pthread_mutex_init(&new_package.global_lock, NULL);
    return new_package;
}

word_node* get_word_head(word_node *sen_node) {
    word_node *temp = sen_node;
    if (temp == NULL) {
        return temp;
    }
    
    while (temp->prev_word_node != NULL) {
        temp = temp->prev_word_node;
    }
    return temp;
}

word_node* get_word_tail(word_node *sen_node) {
    word_node *temp = sen_node;
    if (temp == NULL) {
        return temp;
    }
    
    while (temp->next_word_node != NULL) {
        temp = temp->next_word_node;
    }
    return temp;
}

// gets the word node at certain index (0-indexed)
// appends a word node if index is one more than total words
// error otherwise
word_node* get_word_at_index(word_node **p_head, word_node **p_tail, int index) {
    word_node *temp = *p_head;
    while (temp != NULL && index) {
        temp = temp->next_word_node;
        index--;
    }

    if (temp == NULL) {
        if (index == 1) {
            // No need to LOCK HERE, it changes the sentence that has 
            // already been locked.
            word_node *new_node = new_word_node(NULL);
            append_word_node(p_head, p_tail, &new_node);
            return new_node;
        } else {
            // bad index
            printf("Bad index\n");
            return NULL;
        }
    } else {
        return temp;
    }
}

void append_word_node(word_node **p_head, word_node **p_tail, word_node **p_to_be_added) {
    if (*p_head == NULL) {
        *p_head = *p_to_be_added;
        *p_tail = *p_head;
        return;
    }
    (*(p_tail))->next_word_node = *p_to_be_added;
    ((*(p_tail))->next_word_node)->prev_word_node = *p_tail;
    *p_tail = *p_to_be_added;
    return;
}

void delete_word_node(word_node **p_head, word_node **p_tail, word_node **p_to_be_deleted) {
    if (*p_to_be_deleted == NULL) {
        printf("p_to_be_deleted: NULL: Shouldnt have happened\n");
        return;
    }

    if (*p_to_be_deleted == *p_head) {
        *p_head = (*p_head)->next_word_node;
        if (*p_head != NULL) {
            (*p_head)->prev_word_node = NULL;
        }
    } else if (*p_to_be_deleted == *p_tail) {
        *p_tail = (*p_tail)->prev_word_node;
        if (*p_tail != NULL) {
            (*p_tail)->next_word_node = NULL;
        }
    } else {
        word_node *temp = *p_to_be_deleted;
        temp->prev_word_node->next_word_node = temp->next_word_node;
        temp->next_word_node->prev_word_node = temp->prev_word_node;

        // so that user segfaults if he tries to use p_to_be_deleted
        temp->next_word_node = NULL;
        temp->prev_word_node = NULL;
    }
    /* TODO: free the stuff in the to_be_deleted, then free to_be_deleted */
    return;
}

void free_word_node(word_node *word) {
    if (word->post_whitespace != NULL) {
        free(word->post_whitespace);
    }
    if (word->word != NULL) {
        free(word->word);
    }
}

sentence_node* get_sentence_head(sentence_node *sen_node) {
    sentence_node *temp = sen_node;
    if (temp == NULL) {
        return temp;
    }

    while (temp->prev_sentence_node != NULL) {
        temp = temp->prev_sentence_node;
    }
    return temp;
}

sentence_node* get_sentence_tail(sentence_node *sen_node) {
    sentence_node *temp = sen_node;
    if (temp == NULL) {
        return temp;
    }

    while (temp->next_sentence_node != NULL) {
        temp = temp->next_sentence_node;
    }
    return temp;
}

// gets the word node at certain index (0-indexed)
// appends a word node if index is one more than total words
// error otherwise
// This thing needs to be atomic in the appending sentence node case
sentence_node* get_sentence_at_index(sentence_node **p_head, sentence_node **p_tail, int index) {
    sentence_node *temp = *p_head;
    while (temp != NULL && index) {
        temp = temp->next_sentence_node;
        index--;
    }
    
    if (temp == NULL) {
        if (index == 1) {
            // Need to GLOBAL LOCK HERE, it changes the sentence_package.
            // A global lock will be enabled whenever append takes place.
            sentence_node *new_node = new_sentence_node();
            append_sentence_node(p_head, p_tail, &new_node);         // crit sec access
            return new_node;
        } else {
            // bad index
            printf("Bad index\n");
            return NULL;
        }
    } else {
        return temp;
    }
}

void append_sentence_node(sentence_node **p_head, sentence_node **p_tail, sentence_node **p_to_be_added) {
    if (*p_head == NULL) {
        *p_head = *p_to_be_added;
        *p_tail = *p_head;
        return;
    }
    (*(p_tail))->next_sentence_node = *p_to_be_added;
    ((*(p_tail))->next_sentence_node)->prev_sentence_node = *p_tail;
    *p_tail = *p_to_be_added;
    return;
}

void delete_sentence_node(sentence_node **p_head, sentence_node **p_tail, sentence_node **p_to_be_deleted) {
    if (*p_to_be_deleted == NULL) {
        printf("p_to_be_deleted: NULL: Shouldnt have happened\n");
        return;
    }

    if (*p_to_be_deleted == *p_head) {
        *p_head = (*p_head)->next_sentence_node;
        if (*p_head != NULL) {
            (*p_head)->prev_sentence_node = NULL;
        }
    } else if (*p_to_be_deleted == *p_tail) {
        *p_tail = (*p_tail)->prev_sentence_node;
        if (*p_tail != NULL) {
            (*p_tail)->next_sentence_node = NULL;
        }
    } else {
        sentence_node *temp = *p_to_be_deleted;
        temp->prev_sentence_node->next_sentence_node = temp->next_sentence_node;
        temp->next_sentence_node->prev_sentence_node = temp->prev_sentence_node;

        // so that user segfaults if he tries to use p_to_be_deleted
        temp->next_sentence_node = NULL;
        temp->prev_sentence_node = NULL;
    }
    /* TODO: free the stuff in the to_be_deleted, then free to_be_deleted */
    return;
}

void free_sentence_node(sentence_node *sentence) {
    if (sentence->lock_holder_username != NULL) {
        free(sentence->lock_holder_username);
    }
    if (sentence->pre_whitespace != NULL) {
        free(sentence->pre_whitespace);
    }
    if (sentence->words.word_sequence_head != NULL) {
        word_node *temp = sentence->words.word_sequence_head;
        while (temp != NULL) {
            free_word_node(temp);
            word_node *temp2 = temp;
            temp = temp->next_word_node;
            free(temp2);
        }
    }
}

void free_sentence_package(sentence_package sentences) {
    sentence_node *sentence_head = sentences.sentences_head;
    sentence_node *sentence_tail = sentences.sentences_tail;
    sentence_node *temp = sentence_head;
    while (temp != NULL) {
        free_sentence_node(temp);
        sentence_node *temp2 = temp;
        temp = temp->next_sentence_node;
        free(temp2);
    }
}

/*
// put the to_be_added at idx. So idxth word is the to_be_added
void insert_word_node_at(word_node **p_head, word_node **p_tail, word_node **p_to_be_added, int idx) {

}

// put the to_be_added at idx. So idxth sentence is the to_be_added
void insert_sentence_node_at(sentence_node **p_head, sentence_node **p_tail, sentence_node **p_to_be_added, int idx) {
    if (*p_head == NULL) {
        // empty linked list

    }
}
*/

void print_word(word_node *word, FILE *fptr) {
    fprintf(fptr, "%s", word->word);
    fprintf(fptr, "%s", word->post_whitespace);
}

void print_sentence(sentence_node *sentence, FILE *fptr) {
    fprintf(fptr, "%s", sentence->pre_whitespace);
    word_node *temp = sentence->words.word_sequence_head;
    while (temp != NULL) {
        print_word(temp, fptr);
        temp = temp->next_word_node;
    }
    if (sentence->punct != '\0') {
        fprintf(fptr, "%c", sentence->punct);
    }
}

void print_sentence_pack(sentence_package sentences, FILE *fptr) {
    sentence_node *temp = sentences.sentences_head;
    while (temp != NULL) {
        print_sentence(temp, fptr);
        temp = temp->next_sentence_node;
    }
}

// print the file by converting it into tokens
// only for testing the tokeniser and printers
void print_file(char *in_file_name, char *out_file_name) {
    FILE *fptr= fopen(out_file_name, "w");
    sentence_package file_as_tokens = tokenise_file(in_file_name);
    print_sentence_pack(file_as_tokens, fptr);
    fclose(fptr);
}
