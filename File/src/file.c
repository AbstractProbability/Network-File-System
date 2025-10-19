#include "../include/file.h"

/* 
TOKENIZATION IS TIGHT
EXACT NUMBER OF WORDS, SENTENCES
*/

int is_delimiter(char c) {
    if (c == '.' || c == '?' || c == '!') {
        return 1; 
    }
    return 0;
}

int is_whitespace(char c) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\b') {
        return 1;
    }
    return 0;
}

sentence_package new_sentence_package() {
    sentence_package new_package = {NULL, NULL, 0};
    return new_package;
}

word_package new_word_package() {
    word_package new_package = {NULL, NULL, 0};
    return new_package;
}

sentence_node *new_sentence_node() {
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

word_node *new_word_node(char *string) {
    word_node *new_node = malloc(sizeof(word_node));
    new_node->word = string;

    new_node->is_valid = 0;
    
    new_node->prev_word_node = NULL;
    new_node->next_word_node = NULL;
    return new_node;
}

sentence_node *get_sentence_head(sentence_node *sen_node) {
    sentence_node *temp = sen_node;
    if (temp == NULL) {
        return temp;
    }

    while (temp->prev_sentence_node != NULL) {
        temp = temp->prev_sentence_node;
    }
    return temp;
}

sentence_node *get_sentence_tail(sentence_node *sen_node) {
    sentence_node *temp = sen_node;
    if (temp == NULL) {
        return temp;
    }

    while (temp->next_sentence_node != NULL) {
        temp = temp->next_sentence_node;
    }
    return temp;
}

word_node *get_word_head(word_node *sen_node) {
    word_node *temp = sen_node;
    if (temp == NULL) {
        return temp;
    }
    
    while (temp->prev_word_node != NULL) {
        temp = temp->prev_word_node;
    }
    return temp;
}

word_node *get_word_tail(word_node *sen_node) {
    word_node *temp = sen_node;
    if (temp == NULL) {
        return temp;
    }
    
    while (temp->next_word_node != NULL) {
        temp = temp->next_word_node;
    }
    return temp;
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

// put the to_be_added at idx. So idxth sentence is the to_be_added
void insert_sentence_node_at(sentence_node **p_head, sentence_node **p_tail, sentence_node **p_to_be_added, int idx) {
    if (*p_head == NULL) {
        // empty linked list

    }
}

// put the to_be_added at idx. So idxth word is the to_be_added
void insert_word_node_at(word_node **p_head, word_node **p_tail, word_node **p_to_be_added, int idx) {

}

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

    while (string[idx] != '\0') {
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

/* LLM START */
// Function to read an entire file into a dynamically allocated string.
// The caller is responsible for freeing the returned string.
char* file_to_string(const char* filepath) {
    FILE *file_ptr = fopen(filepath, "r");
    if (!file_ptr) {
        perror("Error opening file");
        return NULL;
    }

    // 1. Find the size of the file
    fseek(file_ptr, 0, SEEK_END); // Go to the end of the file
    long file_size = ftell(file_ptr); // Get the current position (which is the size)
    rewind(file_ptr); // Go back to the beginning

    // 2. Allocate memory for the string
    // Add +1 for the null terminator '\0'
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

    // Null-terminate the string
    buffer[file_size] = '\0';

    // Clean up
    fclose(file_ptr);

    return buffer;
}
/* LLM END */

sentence_package tokenise_file(char *file_name) {
    char *string = file_to_string(file_name);
    sentence_package ret = tokenise(string);
    // free(string);
    return ret;
}

void print_word(word_node *word) {
    printf("%s", word->word);
    printf("%s", word->post_whitespace);
}

void print_sentence(sentence_node *sentence) {
    printf("%s", sentence->pre_whitespace);
    word_node *temp = sentence->words.word_sequence_head;
    while (temp != NULL) {
        print_word(temp);
        temp = temp->next_word_node;
    }
    if (sentence->punct != '\0') {
        printf("%c", sentence->punct);
    }
}

void print_sentence_pack(sentence_package sentences) {
    sentence_node *temp = sentences.sentences_head;
    while (temp != NULL) {
        print_sentence(temp);
        temp = temp->next_sentence_node;
    }
}

// print the file by converting it into tokens
// only for testing the tokeniser and printers
void print_file(char *file_name) {
    sentence_package file_as_tokens = tokenise_file(file_name);
    print_sentence_pack(file_as_tokens);
}

// write at a certain word index in the sentence
// note that `string` may create new sentences etc. So
// handle appropriately. return number of sentences
// after addition of string. 1 means no new sentences have
// been added. Hardest function

// 3 parts: part before the index in current sentence, part due to the
// new string and part after the index in the current sentence. Need to 
// join them. 
// Easy: convert the whole thing into one string, then tokenise
// it, then replace the original sentence with the new one.
// Hard: convert only the string into tokens, then try inserting it at
// the correct position.
int write_at(sentence_node **sentence, char *string, int word_index) {
#ifdef EFFEC
    // TODO: FILL THIS
    // This tokenises the string, then tries to join the part of the sentence
    // before word index with the tokenised string and the tokenised string
    // with the part of the sentence including and after the word index.
    printf("hi!!\n");
#else
    // less space efficient because this copies the whole sentence and then tries modding it
    if (word_index < 0 || word_index > (*sentence)->words.wc) {
        printf("Bad word-index: must be [0, %d)\n", (*sentence)->words.wc);
        return -1;
    }

    int cc = 0;
    word_node *temp = (*sentence)->words.word_sequence_head;
    if ((*sentence)->pre_whitespace != NULL) {
        cc = strlen((*sentence)->pre_whitespace);
    }
    while (temp != NULL) {
        if (temp->word != NULL) {
            cc += strlen(temp->word);
        }
        if (temp->post_whitespace != NULL) {
            cc += strlen(temp->post_whitespace);
        }
        temp = temp->next_word_node;
    }
    if (string != NULL) {
        cc += strlen(string);
    }

    char *buffer = malloc(sizeof(char) * (cc+1));
    int off = 0;
    if ((*sentence)->pre_whitespace != NULL) {
        memcpy(buffer+off, (*sentence)->pre_whitespace, strlen((*sentence)->pre_whitespace));
        off += strlen((*sentence)->pre_whitespace);
    }

    temp = (*sentence)->words.word_sequence_head;
    // before index part
    while (temp != NULL && word_index) {
        word_index--;
        if (temp->word != NULL) {
            memcpy(buffer+off, temp->word, strlen(temp->word));
            off += strlen(temp->word);
        }
        if (temp->post_whitespace != NULL) {
            memcpy(buffer+off, temp->post_whitespace, strlen(temp->post_whitespace));
            off += strlen(temp->post_whitespace);
        }
        temp = temp->next_word_node;
    }

    // string part
    if (string != NULL) {
        memcpy(buffer+off, string, strlen(string));
        off += strlen(string);
    }

    // after index part
    while (temp != NULL) {
        if (temp->word != NULL) {
            memcpy(buffer+off, temp->word, strlen(temp->word));
            off += strlen(temp->word);
        }
        if (temp->post_whitespace != NULL) {
            memcpy(buffer+off, temp->post_whitespace, strlen(temp->post_whitespace));
            off += strlen(temp->post_whitespace);
        }
        temp = temp->next_word_node;
    }
    buffer[off] = '\0';

    // free(buffer);

    // tokenise the whole thing
    sentence_package new_pack = tokenise(buffer);
    
    // now join it    
    // prev to orig sentence joining
    new_pack.sentences_head->prev_sentence_node = (*sentence)->prev_sentence_node;
    if ((*sentence)->prev_sentence_node != NULL) {
        (*sentence)->prev_sentence_node->next_sentence_node = new_pack.sentences_head;
    }

    // orig to next sentence joining
    new_pack.sentences_tail->next_sentence_node = (*sentence)->next_sentence_node;
    if ((*sentence)->next_sentence_node != NULL) {
        (*sentence)->next_sentence_node->prev_sentence_node = new_pack.sentences_tail;
    }

    // final pointer reassign
    (*sentence) = new_pack.sentences_head;

    return new_pack.sc;
#endif
}