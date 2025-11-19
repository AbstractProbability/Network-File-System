#include "../include/common.h"
#include <stdio.h>
#include <ctype.h>
#include <string.h>

// --- LOCAL COPIES OF SS STRUCTS FOR CLIENT-SIDE PARSING ---

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
    // Client doesn't need the mutex/lock_holder
} SentenceNode;

// --- Global State for the Client ---
typedef enum {
    MODE_NS,
    MODE_SS_WRITE
} ClientMode;

static int ns_sock = -1;
static int ss_sock = -1; // Persistent socket for write session
static char g_username[MAX_USERNAME_LEN];
static SentenceNode* g_local_sentence_list = NULL;
static SentenceNode* g_editable_sentence = NULL; 
static ClientMode g_mode = MODE_NS;

// --- LOCAL COPIES OF SS HELPER FUNCTIONS ---

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

void free_local_sentence_list() {
    SentenceNode* current_s = g_local_sentence_list;
    while (current_s != NULL) {
        if (current_s->leading_whitespace) free(current_s->leading_whitespace);
        
        WordNode* current_w = current_s->words;
        while (current_w != NULL) {
            if (current_w->trailing_whitespace) free(current_w->trailing_whitespace);
            WordNode* next_w = current_w->next;
            free(current_w);
            current_w = next_w;
        }
        SentenceNode* next_s = current_s->next;
        free(current_s);
        current_s = next_s;
    }
    g_local_sentence_list = NULL;
    g_editable_sentence = NULL;
}

typedef enum {
    STATE_PRE_SENTENCE, STATE_IN_WORD, STATE_POST_WORD
} ParserState;

SentenceNode* parse_local_string(const char* str) {
    SentenceNode* head_sentence = (SentenceNode*)calloc(1, sizeof(SentenceNode));
    SentenceNode* current_sentence = head_sentence;
    WordNode* current_word = NULL;
    char word_buffer[MAX_WORD_LEN];
    int word_index = 0;
    ParserState state = STATE_PRE_SENTENCE;
    int c;
    int i = 0;

    while (str[i] != '\0') {
        c = str[i++];
        int is_punc = (c == '.' || c == '?' || c == '!');
        int is_space = isspace(c);

        if (state == STATE_PRE_SENTENCE) {
            if (is_space) str_append(&current_sentence->leading_whitespace, c);
            else if (is_punc) {
                current_sentence->punctuation = c;
                SentenceNode* new_s = (SentenceNode*)calloc(1, sizeof(SentenceNode));
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
                current_sentence->next = new_s;
                current_sentence = new_s;
                state = STATE_PRE_SENTENCE;
            } else {
                if (word_index < MAX_WORD_LEN - 1) word_buffer[word_index++] = c;
            }
        } else if (state == STATE_POST_WORD) {
            if (is_space) str_append(&current_word->trailing_whitespace, c);
            else if (is_punc) {
                current_sentence->punctuation = c;
                SentenceNode* new_s = (SentenceNode*)calloc(1, sizeof(SentenceNode));
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
    return head_sentence;
}

// --- This is the simple space-based parser ---
WordNode* parse_content_to_words(const char* content, WordNode** out_last_word) {
    WordNode* head = NULL;
    WordNode* tail = NULL;
    
    char word_buffer[MAX_WORD_LEN];
    int word_index = 0;
    int i = 0;
    char c;
    
    while (1) {
        c = content[i++];
        if (isspace(c) || c == '\0') {
            // End of a word
            if (word_index > 0) {
                word_buffer[word_index] = '\0';
                WordNode* new_word = (WordNode*)calloc(1, sizeof(WordNode));
                strcpy(new_word->word, word_buffer);
                
                // Add a default trailing space
                new_word->trailing_whitespace = strdup(" ");
                
                if (head == NULL) head = new_word;
                else tail->next = new_word;
                tail = new_word;
                word_index = 0;
            }
            if (c == '\0') break; // End of content string
        } else {
            // Part of a word
            if (word_index < MAX_WORD_LEN - 1) {
                word_buffer[word_index++] = c;
            }
        }
    }

    if (out_last_word != NULL) *out_last_word = tail;
    return head;
}

char* serialize_local_list() {
    size_t total_len = 1;
    SentenceNode* s = g_local_sentence_list;
    while (s != NULL) {
        if (s->next == NULL && s->words == NULL && s->leading_whitespace == NULL) break;
        if (s->leading_whitespace) total_len += strlen(s->leading_whitespace);
        WordNode* w = s->words;
        while (w != NULL) {
            total_len += strlen(w->word);
            if (w->trailing_whitespace) total_len += strlen(w->trailing_whitespace);
            w = w->next;
        }
        if (s->punctuation != '\0') total_len++;
        s = s->next;
    }

    char* str = (char*)calloc(1, total_len);
    if (str == NULL) return NULL;
    
    s = g_local_sentence_list;
    while (s != NULL) {
        if (s->next == NULL && s->words == NULL && s->leading_whitespace == NULL) break;
        if (s->leading_whitespace) strcat(str, s->leading_whitespace);
        WordNode* w = s->words;
        while (w != NULL) {
            strcat(str, w->word);
            if (w->trailing_whitespace) strcat(str, w->trailing_whitespace);
            w = w->next;
        }
        if (s->punctuation != '\0') {
            char punc[2] = {s->punctuation, '\0'};
            strcat(str, punc);
        }
        s = s->next;
    }
    return str;
}


// --- Client Network Helpers ---
int connect_to_server(const char* ip, int port) {
    int sock;
    struct sockaddr_in addr;
    
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return -1;
    }
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        perror("Invalid address");
        return -1;
    }
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Connection Failed");
        return -1;
    }
    return sock; 
}

int register_with_ns(const char* username) {
    ns_sock = connect_to_server("127.0.0.1", NS_LISTEN_PORT);
    if (ns_sock < 0) return -1;
    
    strncpy(g_username, username, MAX_USERNAME_LEN);

    InitialPacket init_pkt;
    init_pkt.type = CONN_CLIENT;
    strncpy(init_pkt.username, g_username, MAX_USERNAME_LEN);
    send(ns_sock, &init_pkt, sizeof(InitialPacket), 0);

    ServerResponse res;
    if (recv(ns_sock, &res, sizeof(ServerResponse), 0) <= 0) {
        printf("NS disconnected.\n");
        return -1;
    }

    printf("[NS Response]: %s\n", res.message);
    if (res.status != ERR_OK) {
        close(ns_sock);
        return -1;
    }
    return 0; 
}

int parse_command(char* line, char** args) {
    int i = 0;
    
    if (g_mode == MODE_SS_WRITE) {
        if (strncmp(line, "insert", 6) == 0 || strncmp(line, "l_insert", 8) == 0) {
            args[i++] = strtok(line, " \n\t"); // "insert" or "l_insert"
            if(args[0] == NULL) return 0;
            
            args[i++] = strtok(NULL, " \n\t"); // <index>
            if(args[1] == NULL) return i;

            char* content = strtok(NULL, "\n"); 
            if (content) {
                while (isspace((unsigned char)*content)) content++; 
                args[i++] = content;
            }
            return i;
        } 
    }
    
    char* token = strtok(line, " \n\t");
    while (token != NULL) {
        args[i++] = token;
        token = strtok(NULL, " \n\t");
    }
    return i; 
}

// --- Operation Handlers ---

void do_list() {
    ClientRequest req;
    memset(&req, 0, sizeof(ClientRequest));
    req.op = OP_LIST;
    strncpy(req.username, g_username, MAX_USERNAME_LEN);
    
    send(ns_sock, &req, sizeof(ClientRequest), 0);
    
    ServerResponse res;
    recv(ns_sock, &res, sizeof(ServerResponse), 0);
    printf("[NS Response]:\n%s\n", res.message);
}

void do_create(char* path) {
    ClientRequest req;
    memset(&req, 0, sizeof(ClientRequest));
    req.op = OP_CREATE;
    strncpy(req.username, g_username, MAX_USERNAME_LEN);
    strncpy(req.path, path, MAX_PATH_LEN);
    
    send(ns_sock, &req, sizeof(ClientRequest), 0);
    
    ServerResponse res;
    recv(ns_sock, &res, sizeof(ServerResponse), 0);
    printf("[NS Response]: %s\n", res.message);
}

void do_read(char* path) {
    ClientRequest req;
    memset(&req, 0, sizeof(ClientRequest));
    req.op = OP_READ;
    strncpy(req.username, g_username, MAX_USERNAME_LEN);
    strncpy(req.path, path, MAX_PATH_LEN);
    
    send(ns_sock, &req, sizeof(ClientRequest), 0);
    
    ServerResponse res;
    if (recv(ns_sock, &res, sizeof(ServerResponse), 0) <= 0 || res.status != ERR_OK) {
        printf("[NS Response]: %s\n", res.message);
        return;
    }
    
    printf("[NS Response]: %s\n", res.message);
    
    int temp_ss_sock = connect_to_server(res.ss_ip, res.ss_port);
    if (temp_ss_sock < 0) return;
    
    send(temp_ss_sock, &req, sizeof(ClientRequest), 0);
    
    char buffer[MAX_BUFFER_LEN];
    printf("--- File Content ---\n");
    while (1) {
        int bytes = recv(temp_ss_sock, buffer, MAX_BUFFER_LEN - 1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';
        if (strstr(buffer, "STOP") != NULL) {
            *strstr(buffer, "STOP") = '\0';
            printf("%s", buffer);
            break;
        }
        printf("%s", buffer);
    }
    printf("\n--- End of File ---\n");
    close(temp_ss_sock);
}

void do_write(char* path, char* index_str) {
    ClientRequest write_req;
    memset(&write_req, 0, sizeof(ClientRequest));
    write_req.op = OP_WRITE;
    strncpy(write_req.username, g_username, MAX_USERNAME_LEN);
    strncpy(write_req.path, path, MAX_PATH_LEN);
    write_req.index = atoi(index_str);

    send(ns_sock, &write_req, sizeof(ClientRequest), 0);
    
    ServerResponse res;
    if (recv(ns_sock, &res, sizeof(ServerResponse), 0) <= 0 || res.status != ERR_OK) {
        printf("[NS Response]: %s\n", res.message);
        return;
    }
    
    ss_sock = connect_to_server(res.ss_ip, res.ss_port);
    if (ss_sock < 0) {
        printf("Failed to connect to SS.\n");
        return;
    }
    
    send(ss_sock, &write_req, sizeof(ClientRequest), 0);
    
    if (recv(ss_sock, &res, sizeof(ServerResponse), 0) <= 0 || res.status != ERR_OK) {
        printf("[SS Response]: %s\n", res.message);
        close(ss_sock);
        ss_sock = -1;
        return;
    }
    
    const char* content_prefix = "Content: ";
    char* content_start = strstr(res.message, content_prefix);
    if (content_start) {
        content_start += strlen(content_prefix);
        if (g_local_sentence_list) free_local_sentence_list();
        g_local_sentence_list = parse_local_string(content_start);
        g_editable_sentence = g_local_sentence_list; 
    } else {
        if (g_local_sentence_list) free_local_sentence_list();
        g_local_sentence_list = parse_local_string(""); // Empty list
        g_editable_sentence = g_local_sentence_list;
    }
    
    printf("[SS Response]: Lock successful.\n");
    
    char* s_content = serialize_local_list();
    printf("Current Content: \"%s\"\n", s_content);
    free(s_content);
    
    printf("--- Now in WRITE mode ---\n");
    printf("Commands: l_insert <index> <content...> | l_view | commit | abort\n");
    g_mode = MODE_SS_WRITE; // Change state
}

void do_local_insert(char* index_str, char* content) {
    if (g_editable_sentence == NULL) {
        printf("Error: No sentence is being edited.\n");
        return;
    }
    if (content == NULL) content = ""; // Handle empty insert
    
    int index = atoi(index_str);
    
    if (strchr(content, '.') || strchr(content, '?') || strchr(content, '!')) {
        printf("Warning: Punctuation detected in content. Your editable session may be truncated after this.\n");
    }

    // 1. Parse the *new content* using the simple space-based parser
    WordNode* new_words_head = NULL;
    WordNode* new_words_tail = NULL;
    new_words_head = parse_content_to_words(content, &new_words_tail);
    
    if (new_words_head == NULL) {
        printf("No words to insert.\n");
        return;
    }
    
    // 2. Find the insertion point in the *local editable sentence*
    WordNode* prev_word = NULL;
    if (index == 0) {
        // Insert at head
        new_words_tail->next = g_editable_sentence->words;
        g_editable_sentence->words = new_words_head;
    } else {
        // Find (index - 1)th word
        prev_word = g_editable_sentence->words;
        int i = 0;
        while (prev_word != NULL && i < index - 1) {
            prev_word = prev_word->next;
            i++;
        }
        
        // --- THIS IS THE FIX ---
        if (prev_word == NULL) {
            // This is the out-of-bounds error
            printf("Error: Word index %d is out of bounds. No changes made.\n", index);
            
            // We must free the list we just parsed to prevent a memory leak
            WordNode* w = new_words_head;
            while (w != NULL) {
                WordNode* next = w->next;
                if (w->trailing_whitespace) free(w->trailing_whitespace);
                free(w);
                w = next;
            }
            return; // DO NOT PROCEED
        }
        // --- END OF FIX ---
        
        // This is the valid insertion case
        new_words_tail->next = prev_word->next;
        prev_word->next = new_words_head;
    }
    
    // 3. Re-tokenize to handle truncation
    char* temp_serialized = serialize_local_list();
    free_local_sentence_list();
    g_local_sentence_list = parse_local_string(temp_serialized);
    g_editable_sentence = g_local_sentence_list; // Point back to the first sentence
    
    printf("New content: \"%s\"\n", temp_serialized);
    free(temp_serialized);
}

void do_local_view() {
    char* content = serialize_local_list();
    printf("Current Content: \"%s\"\n", content);
    free(content);
}

void do_commit() {
    char* final_content = serialize_local_list();
    
    ClientWritePacket pkt;
    memset(&pkt, 0, sizeof(ClientWritePacket));
    strncpy(pkt.content, final_content, MAX_COMMIT_LEN);
    
    send(ss_sock, &pkt, sizeof(ClientWritePacket), 0);
    printf("Sent commit. Waiting for ACK...\n");
    free(final_content);
    
    ServerResponse res;
    if (recv(ss_sock, &res, sizeof(ServerResponse), 0) <= 0 || res.status != ERR_OK) {
        printf("[SS Response]: Commit failed: %s\n", res.message);
    } else {
        printf("[SS Response]: %s\n", res.message);
    }
    
    close(ss_sock);
    ss_sock = -1;
    free_local_sentence_list();
    g_mode = MODE_NS; // Change state back
    printf("--- Exited WRITE mode ---\n");
}

void do_abort() {
    close(ss_sock); // This tells the SS to unlock (client disconnected)
    ss_sock = -1;
    free_local_sentence_list();
    g_mode = MODE_NS;
    printf("Aborted write. Local changes discarded.\n");
    printf("--- Exited WRITE mode ---\n");
}

// --- Main REPL ---
int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <username>\n", argv[0]);
        exit(1);
    }

    if (register_with_ns(argv[1]) != 0) {
        exit(1);
    }

    char line[MAX_BUFFER_LEN];
    char* args[10];
    
    while(1) {
        if (g_mode == MODE_NS) {
            printf("> ");
        } else {
            printf("(writing)> ");
        }
        
        if (fgets(line, MAX_BUFFER_LEN, stdin) == NULL) {
            break; // EOF
        }
        
        int arg_count = parse_command(line, args);
        if (arg_count == 0) continue;
        
        // --- Handle NS Mode Commands ---
        if (g_mode == MODE_NS) {
            if (strcmp(args[0], "list") == 0) {
                do_list();
            } else if (strcmp(args[0], "create") == 0 && arg_count == 2) {
                do_create(args[1]);
            } else if (strcmp(args[0], "read") == 0 && arg_count == 2) {
                do_read(args[1]);
            } else if (strcmp(args[0], "write") == 0 && arg_count == 3) {
                do_write(args[1], args[2]);
            } else if (strcmp(args[0], "exit") == 0) {
                break;
            } else {
                printf("Unknown command or wrong args.\n");
                printf("Usage: list | create <path> | read <path> | write <path> <index> | exit\n");
            }
        }
        // --- Handle Write Mode Commands ---
        else if (g_mode == MODE_SS_WRITE) {
            if ((strcmp(args[0], "insert") == 0 || strcmp(args[0], "l_insert") == 0) && arg_count >= 2) {
                do_local_insert(args[1], (arg_count == 3) ? args[2] : "");
            } else if (strcmp(args[0], "view") == 0 || strcmp(args[0], "l_view") == 0) {
                do_local_view();
            } else if (strcmp(args[0], "commit") == 0) {
                do_commit();
            } else if (strcmp(args[0], "abort") == 0) {
                do_abort();
            } else {
                printf("Unknown command. You are in write mode.\n");
                printf("Usage: l_insert <index> <content...> | l_view | commit | abort\n");
            }
        }
    }
    
    // --- Cleanup ---
    if (ns_sock != -1) {
        ClientRequest req;
        req.op = OP_EXIT;
        send(ns_sock, &req, sizeof(ClientRequest), 0);
        close(ns_sock);
    }
    if (ss_sock != -1) {
        close(ss_sock);
    }
    free_local_sentence_list();
    
    return 0;
}