#include "../include/file.h"

// helper function for join_string_at_index,
// returns number of characters in sentence + string
int character_count_sentence_and_string(sentence_node **sentence, char *string, int word_index) {
    int cc = character_count_sentence(sentence);
    //string
    if (string != NULL) {
        cc += strlen(string);
    }
    return cc;
}

// helper function for write_at, joins the string in the sentence
// at index and returns the joined total string
char* join_string_at_index(sentence_node **sentence, char *string, int word_index) {
    int cc = character_count_sentence_and_string(sentence, string, word_index);

    char *buffer = malloc(sizeof(char) * (cc+1));
    int off = 0;
    // pre_whitespace part
    if ((*sentence)->pre_whitespace != NULL) {
        memcpy(buffer+off, (*sentence)->pre_whitespace, strlen((*sentence)->pre_whitespace));
        off += strlen((*sentence)->pre_whitespace);
    }
    
    // before index part
    word_node *temp = (*sentence)->words.word_sequence_head;
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
    return buffer;
}

void join_sentence_with_sentence_package(sentence_node **sentence, sentence_package *p_new_pack) {
    sentence_package new_pack = *p_new_pack;
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
}

// write at a certain word index in the sentence
// note that 'string' may create new sentences etc. So
// handle appropriately. return number of sentences
// after addition of string. 1 means no new sentences have
// been added.

// 3 parts: part before the index in current sentence, part due to the
// new string and part after the index in the current sentence. Need to 
// join them.
int update_sentence_at_index(sentence_node **sentence, char *string, int word_index) {
    // This way is less space efficient because
    // this copies the whole sentence 
    // and then tries modding it. 
    // This is NOT an in-place join.
    if (word_index < 0 || word_index > (*sentence)->words.wc) {
        printf("Error: Bad word-index: must be [0, %d)\n", (*sentence)->words.wc);
        return -1;
    }

    // get the whole new sentence as a string
    char *buffer = join_string_at_index(sentence, string, word_index);

    // tokenise the whole thing
    sentence_package new_pack = tokenise(buffer);
    
    // now join it
    join_sentence_with_sentence_package(sentence, &new_pack);

    return new_pack.sc;
}

// HANDLE CASE: 3 GUYS LOOKING TO APPEND TO THE FILE. CREATION OF THE NEW SENTENCE NODE MUST BE ATOMIC
// PROBABLY NEED A LOCK IN SENTENCE_PACKAGE SPECIFICALLY FOR THIS PURPOSE
int lock_sentence_at_index_for_username(sentence_package sentences, int index, char *username) {

}