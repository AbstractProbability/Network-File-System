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
static char g_current_write_path[MAX_PATH_LEN]; // Track path during write session
static SentenceNode* g_local_sentence_list = NULL;
static SentenceNode* g_editable_sentence = NULL; 
static ClientMode g_mode = MODE_NS;
static char g_ns_ip[INET_ADDRSTRLEN] = "127.0.0.1";
static int g_ns_port = NS_LISTEN_PORT;

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
            if (word_index > 0) {
                word_buffer[word_index] = '\0';
                WordNode* new_word = (WordNode*)calloc(1, sizeof(WordNode));
                strcpy(new_word->word, word_buffer);
                new_word->trailing_whitespace = strdup(" ");
                if (head == NULL) head = new_word;
                else tail->next = new_word;
                tail = new_word;
                word_index = 0;
            }
            if (c == '\0') break; 
        } else {
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
    printf("Connecting to NS at %s:%d...\n", g_ns_ip, g_ns_port);
    ns_sock = connect_to_server(g_ns_ip, g_ns_port);
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

void do_view(char* flags_str) {
    ClientRequest req;
    memset(&req, 0, sizeof(ClientRequest));
    req.op = OP_VIEW;
    strncpy(req.username, g_username, MAX_USERNAME_LEN);
    
    // Parse flags: -a (all files), -l (long format with details)
    int show_all = 0;
    int show_long = 0;
    
    if (flags_str != NULL) {
        // Handle flags like -a, -l, -al, -la, -aalll, etc.
        for (int i = 0; flags_str[i] != '\0'; i++) {
            if (flags_str[i] == 'a') show_all = 1;
            else if (flags_str[i] == 'l') show_long = 1;
            // Ignore other characters like '-'
        }
    }
    
    // Pack flags into int: bit 0 = show_all, bit 1 = show_long
    req.flags = (show_all ? 1 : 0) | (show_long ? 2 : 0);
    
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

void do_delete(char* path) {
    ClientRequest req;
    memset(&req, 0, sizeof(ClientRequest));
    req.op = OP_DELETE;
    strncpy(req.username, g_username, MAX_USERNAME_LEN);
    strncpy(req.path, path, MAX_PATH_LEN);
    
    send(ns_sock, &req, sizeof(ClientRequest), 0);
    
    ServerResponse res;
    recv(ns_sock, &res, sizeof(ServerResponse), 0);
    printf("[NS Response]: %s\n", res.message);
}

void do_undo(char* path) {
    ClientRequest req;
    memset(&req, 0, sizeof(ClientRequest));
    req.op = OP_UNDO;
    strncpy(req.username, g_username, MAX_USERNAME_LEN);
    strncpy(req.path, path, MAX_PATH_LEN);
    
    send(ns_sock, &req, sizeof(ClientRequest), 0);
    
    ServerResponse res;
    recv(ns_sock, &res, sizeof(ServerResponse), 0);
    printf("[NS Response]: %s\n", res.message);
}

void do_checkpoint(char* path, char* tag) {
    ClientRequest req;
    memset(&req, 0, sizeof(ClientRequest));
    req.op = OP_CHECKPOINT;
    strncpy(req.username, g_username, MAX_USERNAME_LEN);
    strncpy(req.path, path, MAX_PATH_LEN);
    strncpy(req.arg1, tag, MAX_PATH_LEN);
    
    send(ns_sock, &req, sizeof(ClientRequest), 0);
    
    ServerResponse res;
    recv(ns_sock, &res, sizeof(ServerResponse), 0);
    printf("[NS Response]: %s\n", res.message);
}

void do_viewcheckpoint(char* path, char* tag) {
    ClientRequest req;
    memset(&req, 0, sizeof(ClientRequest));
    req.op = OP_VIEWCHECKPOINT;
    strncpy(req.username, g_username, MAX_USERNAME_LEN);
    strncpy(req.path, path, MAX_PATH_LEN);
    strncpy(req.arg1, tag, MAX_PATH_LEN);
    
    send(ns_sock, &req, sizeof(ClientRequest), 0);
    
    ServerResponse res;
    if (recv(ns_sock, &res, sizeof(ServerResponse), 0) <= 0 || res.status != ERR_OK) {
        printf("[NS Response]: %s\n", res.message);
        return;
    }
    
    printf("[NS Response]: Connecting to SS %s:%d\n", res.ss_ip, res.ss_port);
    
    // Connect to SS directly
    int temp_ss_sock = connect_to_server(res.ss_ip, res.ss_port);
    if (temp_ss_sock < 0) return;
    
    // Use arg1 for checkpoint tag from NS response
    strncpy(req.arg1, res.message, MAX_PATH_LEN);
    send(temp_ss_sock, &req, sizeof(ClientRequest), 0);
    
    char buffer[MAX_BUFFER_LEN];
    printf("--- Checkpoint Content (tag: %s) ---\n", tag);
    while (1) {
        memset(buffer, 0, MAX_BUFFER_LEN);
        int n = recv(temp_ss_sock, buffer, MAX_BUFFER_LEN, 0);
        if (n <= 0 || strcmp(buffer, "STOP") == 0) break;
        printf("%s", buffer);
    }
    printf("\n--- End of Checkpoint ---\n");
    close(temp_ss_sock);
}

void do_revert(char* path, char* tag) {
    ClientRequest req;
    memset(&req, 0, sizeof(ClientRequest));
    req.op = OP_REVERT;
    strncpy(req.username, g_username, MAX_USERNAME_LEN);
    strncpy(req.path, path, MAX_PATH_LEN);
    strncpy(req.arg1, tag, MAX_PATH_LEN);
    
    send(ns_sock, &req, sizeof(ClientRequest), 0);
    
    ServerResponse res;
    recv(ns_sock, &res, sizeof(ServerResponse), 0);
    printf("[NS Response]: %s\n", res.message);
}

void do_listcheckpoints(char* path) {
    ClientRequest req;
    memset(&req, 0, sizeof(ClientRequest));
    req.op = OP_LISTCHECKPOINTS;
    strncpy(req.username, g_username, MAX_USERNAME_LEN);
    strncpy(req.path, path, MAX_PATH_LEN);
    
    send(ns_sock, &req, sizeof(ClientRequest), 0);
    
    ServerResponse res;
    if (recv(ns_sock, &res, sizeof(ServerResponse), 0) <= 0 || res.status != ERR_OK) {
        printf("[NS Response]: %s\n", res.message);
        return;
    }
    
    printf("[NS Response]: Connecting to SS %s:%d\n", res.ss_ip, res.ss_port);
    
    // Connect to SS directly
    int temp_ss_sock = connect_to_server(res.ss_ip, res.ss_port);
    if (temp_ss_sock < 0) return;
    
    send(temp_ss_sock, &req, sizeof(ClientRequest), 0);
    
    char buffer[MAX_BUFFER_LEN];
    printf("--- Checkpoints for '%s' ---\n", path);
    int count = 0;
    while (1) {
        memset(buffer, 0, MAX_BUFFER_LEN);
        int n = recv(temp_ss_sock, buffer, MAX_BUFFER_LEN, 0);
        if (n <= 0 || strcmp(buffer, "STOP") == 0) break;
        
        // Remove trailing newline from buffer if present
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
        }
        
        printf("%d. %s\n", ++count, buffer);
    }
    if (count == 0) {
        printf("No checkpoints found.\n");
    }
    printf("--- End of List ---\n");
    close(temp_ss_sock);
}

void do_createfolder(char* path) {
    ClientRequest req;
    memset(&req, 0, sizeof(ClientRequest));
    req.op = OP_CREATEFOLDER;
    strncpy(req.username, g_username, MAX_USERNAME_LEN);
    strncpy(req.path, path, MAX_PATH_LEN);
    
    send(ns_sock, &req, sizeof(ClientRequest), 0);
    
    ServerResponse res;
    recv(ns_sock, &res, sizeof(ServerResponse), 0);
    printf("[NS Response]: %s\n", res.message);
}

void do_move(char* old_path, char* new_path) {
    ClientRequest req;
    memset(&req, 0, sizeof(ClientRequest));
    req.op = OP_MOVE;
    strncpy(req.username, g_username, MAX_USERNAME_LEN);
    strncpy(req.path, old_path, MAX_PATH_LEN);
    strncpy(req.arg1, new_path, MAX_PATH_LEN);
    
    send(ns_sock, &req, sizeof(ClientRequest), 0);
    
    ServerResponse res;
    recv(ns_sock, &res, sizeof(ServerResponse), 0);
    printf("[NS Response]: %s\n", res.message);
}

void do_viewfolder(char* path) {
    ClientRequest req;
    memset(&req, 0, sizeof(ClientRequest));
    req.op = OP_VIEWFOLDER;
    strncpy(req.username, g_username, MAX_USERNAME_LEN);
    strncpy(req.path, path, MAX_PATH_LEN);
    
    send(ns_sock, &req, sizeof(ClientRequest), 0);
    
    ServerResponse res;
    recv(ns_sock, &res, sizeof(ServerResponse), 0);
    printf("[NS Response]:\n%s\n", res.message);
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

void do_stream(char* path) {
    ClientRequest req;
    memset(&req, 0, sizeof(ClientRequest));
    req.op = OP_STREAM;
    strncpy(req.username, g_username, MAX_USERNAME_LEN);
    strncpy(req.path, path, MAX_PATH_LEN);
    
    // Ask NS for SS info
    send(ns_sock, &req, sizeof(ClientRequest), 0);
    
    ServerResponse res;
    if (recv(ns_sock, &res, sizeof(ServerResponse), 0) <= 0 || res.status != ERR_OK) {
        printf("[NS Response]: %s\n", res.message);
        return;
    }
    
    printf("[NS Response]: %s\n", res.message);
    
    // Connect to SS
    int temp_ss_sock = connect_to_server(res.ss_ip, res.ss_port);
    if (temp_ss_sock < 0) {
        printf("Error: Failed to connect to storage server.\n");
        return;
    }
    
    // Send request to SS
    send(temp_ss_sock, &req, sizeof(ClientRequest), 0);
    
    // First receive the ServerResponse from SS
    ServerResponse ss_res;
    if (recv(temp_ss_sock, &ss_res, sizeof(ServerResponse), 0) <= 0) {
        printf("Error: Storage server disconnected.\n");
        close(temp_ss_sock);
        return;
    }
    
    if (ss_res.status != ERR_OK) {
        printf("[SS Response]: %s\n", ss_res.message);
        close(temp_ss_sock);
        return;
    }
    
    // Now receive the streamed content
    printf("--- Streaming File Content ---\n");
    char word[MAX_WORD_LEN];
    int total_received = 0;
    
    while (1) {
        int bytes = recv(temp_ss_sock, word, MAX_WORD_LEN - 1, 0);
        if (bytes <= 0) {
            printf("\nError: Storage server disconnected mid-streaming.\n");
            break;
        }
        
        word[bytes] = '\0';
        
        // Check for STOP marker
        if (strcmp(word, "STOP") == 0) {
            break;
        }
        
        // Display the word/character immediately
        printf("%s", word);
        fflush(stdout);
        total_received++;
    }
    
    printf("\n--- End of Stream ---\n");
    close(temp_ss_sock);
}

void do_info(char* path) {
    ClientRequest req;
    memset(&req, 0, sizeof(ClientRequest));
    req.op = OP_INFO;
    strncpy(req.username, g_username, MAX_USERNAME_LEN);
    strncpy(req.path, path, MAX_PATH_LEN);
    
    send(ns_sock, &req, sizeof(ClientRequest), 0);
    
    ServerResponse res;
    recv(ns_sock, &res, sizeof(ServerResponse), 0);
    printf("[File Info]:\n%s\n", res.message);
}

void do_addaccess(char* path, char* username, char* type) {
    ClientRequest req;
    memset(&req, 0, sizeof(ClientRequest));
    req.op = OP_ADDACCESS;
    strncpy(req.username, g_username, MAX_USERNAME_LEN);
    strncpy(req.path, path, MAX_PATH_LEN);
    strncpy(req.arg1, username, MAX_PATH_LEN);
    req.arg2[0] = type[0];
    
    send(ns_sock, &req, sizeof(ClientRequest), 0);
    
    ServerResponse res;
    recv(ns_sock, &res, sizeof(ServerResponse), 0);
    printf("[NS Response]: %s\n", res.message);
}

void do_remaccess(char* path, char* username, char* type) {
    ClientRequest req;
    memset(&req, 0, sizeof(ClientRequest));
    req.op = OP_REMACCESS;
    strncpy(req.username, g_username, MAX_USERNAME_LEN);
    strncpy(req.path, path, MAX_PATH_LEN);
    strncpy(req.arg1, username, MAX_PATH_LEN);
    req.arg2[0] = type[0];
    
    send(ns_sock, &req, sizeof(ClientRequest), 0);
    
    ServerResponse res;
    recv(ns_sock, &res, sizeof(ServerResponse), 0);
    printf("[NS Response]: %s\n", res.message);
}

void do_reqaccess(char* path, char* type) {
    ClientRequest req;
    memset(&req, 0, sizeof(ClientRequest));
    req.op = OP_REQACCESS;
    strncpy(req.username, g_username, MAX_USERNAME_LEN);
    strncpy(req.path, path, MAX_PATH_LEN);
    req.arg2[0] = type[0];
    
    send(ns_sock, &req, sizeof(ClientRequest), 0);
    
    ServerResponse res;
    recv(ns_sock, &res, sizeof(ServerResponse), 0);
    printf("[NS Response]: %s\n", res.message);
}

void do_reqlist() {
    ClientRequest req;
    memset(&req, 0, sizeof(ClientRequest));
    req.op = OP_REQLIST;
    strncpy(req.username, g_username, MAX_USERNAME_LEN);
    
    send(ns_sock, &req, sizeof(ClientRequest), 0);
    
    ServerResponse res;
    recv(ns_sock, &res, sizeof(ServerResponse), 0);
    printf("%s\n", res.message);
}

void do_approve(char* request_id_str) {
    ClientRequest req;
    memset(&req, 0, sizeof(ClientRequest));
    req.op = OP_APPROVE;
    strncpy(req.username, g_username, MAX_USERNAME_LEN);
    req.index = atoi(request_id_str);
    
    send(ns_sock, &req, sizeof(ClientRequest), 0);
    
    ServerResponse res;
    recv(ns_sock, &res, sizeof(ServerResponse), 0);
    printf("[NS Response]: %s\n", res.message);
}

void do_reject(char* request_id_str) {
    ClientRequest req;
    memset(&req, 0, sizeof(ClientRequest));
    req.op = OP_REJECT;
    strncpy(req.username, g_username, MAX_USERNAME_LEN);
    req.index = atoi(request_id_str);
    
    send(ns_sock, &req, sizeof(ClientRequest), 0);
    
    ServerResponse res;
    recv(ns_sock, &res, sizeof(ServerResponse), 0);
    printf("[NS Response]: %s\n", res.message);
}

void do_write(char* path, char* index_str) {
    ClientRequest write_req;
    memset(&write_req, 0, sizeof(ClientRequest));
    write_req.op = OP_WRITE;
    strncpy(write_req.username, g_username, MAX_USERNAME_LEN);
    strncpy(write_req.path, path, MAX_PATH_LEN);
    write_req.index = atoi(index_str);
    
    // Store path for later use in commit
    strncpy(g_current_write_path, path, MAX_PATH_LEN);

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

    WordNode* new_words_head = NULL;
    WordNode* new_words_tail = NULL;
    new_words_head = parse_content_to_words(content, &new_words_tail);
    
    if (new_words_head == NULL) {
        printf("No words to insert.\n");
        return;
    }
    
    WordNode* prev_word = NULL;
    if (index == 0) {
        new_words_tail->next = g_editable_sentence->words;
        g_editable_sentence->words = new_words_head;
    } else {
        prev_word = g_editable_sentence->words;
        int i = 0;
        while (prev_word != NULL && i < index - 1) {
            prev_word = prev_word->next;
            i++;
        }
        
        if (prev_word == NULL) {
            printf("Error: Word index %d is out of bounds. No changes made.\n", index);
            
            WordNode* w = new_words_head;
            while (w != NULL) {
                WordNode* next = w->next;
                if (w->trailing_whitespace) free(w->trailing_whitespace);
                free(w);
                w = next;
            }
            return; 
        }
        
        new_words_tail->next = prev_word->next;
        prev_word->next = new_words_head;
    }
    
    char* temp_serialized = serialize_local_list();
    free_local_sentence_list();
    g_local_sentence_list = parse_local_string(temp_serialized);
    g_editable_sentence = g_local_sentence_list; 
    
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
    
    // Notify NS that write session is complete
    ClientRequest done_req;
    memset(&done_req, 0, sizeof(ClientRequest));
    done_req.op = OP_WRITER_DONE;
    strncpy(done_req.username, g_username, MAX_USERNAME_LEN);
    strncpy(done_req.path, g_current_write_path, MAX_PATH_LEN);
    send(ns_sock, &done_req, sizeof(ClientRequest), 0);
    
    printf("--- Exited WRITE mode ---\n");
}

void do_abort() {
    close(ss_sock); // This tells the SS to unlock (client disconnected)
    ss_sock = -1;
    free_local_sentence_list();
    
    // Notify NS that write session is complete (aborted)
    ClientRequest done_req;
    memset(&done_req, 0, sizeof(ClientRequest));
    done_req.op = OP_WRITER_DONE;
    strncpy(done_req.username, g_username, MAX_USERNAME_LEN);
    strncpy(done_req.path, g_current_write_path, MAX_PATH_LEN);
    send(ns_sock, &done_req, sizeof(ClientRequest), 0);
    
    g_mode = MODE_NS;
    printf("Aborted write. Local changes discarded.\n");
    printf("--- Exited WRITE mode ---\n");
}

void do_exec(char* path) {
    ClientRequest req;
    memset(&req, 0, sizeof(ClientRequest));
    req.op = OP_EXEC;
    strncpy(req.username, g_username, MAX_USERNAME_LEN);
    strncpy(req.path, path, MAX_PATH_LEN);
    
    send(ns_sock, &req, sizeof(ClientRequest), 0);
    
    ServerResponse res;
    if (recv(ns_sock, &res, sizeof(ServerResponse), 0) <= 0 || res.status != ERR_OK) {
        printf("[NS Response]: %s\n", res.message);
        return;
    }
    
    printf("[NS Response]: %s\n", res.message);
    printf("--- Script Output ---\n");
    
    // Stream output from NS
    char buffer[MAX_BUFFER_LEN];
    while (1) {
        memset(buffer, 0, MAX_BUFFER_LEN);
        int n = recv(ns_sock, buffer, MAX_BUFFER_LEN, 0);
        if (n <= 0) {
            printf("\n[Connection lost or exec completed]\n");
            break;
        }
        if (strncmp(buffer, "EXEC_STOP", 9) == 0) {
            break;
        }
        printf("%s", buffer);
        fflush(stdout);
    }
    printf("\n");
}

// --- Main REPL ---

int main(int argc, char* argv[]) {
    if (argc != 2 && argc != 4) {
        fprintf(stderr, "Usage: %s <username> [<ns_ip> <ns_port>]\n", argv[0]);
        fprintf(stderr, "\nArguments:\n");
        fprintf(stderr, "  username  : Username for this client\n");
        fprintf(stderr, "  ns_ip     : IP address of Name Server (default: 127.0.0.1)\n");
        fprintf(stderr, "  ns_port   : Port of Name Server (default: 8080)\n");
        fprintf(stderr, "\nExamples:\n");
        fprintf(stderr, "  %s alice                      # NS at localhost:8080\n", argv[0]);
        fprintf(stderr, "  %s alice 192.168.1.100 8080   # NS at 192.168.1.100:8080\n", argv[0]);
        exit(1);
    }

    // Parse arguments
    if (argc == 4) {
        // Full specification: username ns_ip ns_port
        strncpy(g_ns_ip, argv[2], INET_ADDRSTRLEN - 1);
        g_ns_ip[INET_ADDRSTRLEN - 1] = '\0';
        g_ns_port = atoi(argv[3]);
        if (g_ns_port <= 0 || g_ns_port > 65535) {
            fprintf(stderr, "Error: Invalid port number %s\n", argv[3]);
            exit(1);
        }
    }
    // If argc == 2, use defaults from global variables

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
            } else if (strcmp(args[0], "view") == 0) {
                // view, view -a, view -l, view -al, view -laalaa, etc.
                do_view((arg_count == 2) ? args[1] : NULL);
            } else if (strcmp(args[0], "create") == 0 && arg_count == 2) {
                do_create(args[1]);
            } else if (strcmp(args[0], "delete") == 0 && arg_count == 2) {
                do_delete(args[1]);
            } else if (strcmp(args[0], "read") == 0 && arg_count == 2) {
                do_read(args[1]);
            } else if (strcmp(args[0], "stream") == 0 && arg_count == 2) {
                do_stream(args[1]);
            } else if (strcmp(args[0], "info") == 0 && arg_count == 2) {
                do_info(args[1]);
            } else if (strcmp(args[0], "write") == 0 && arg_count == 3) {
                do_write(args[1], args[2]);
            } else if (strcmp(args[0], "undo") == 0 && arg_count == 2) {
                do_undo(args[1]);
            } else if (strcmp(args[0], "checkpoint") == 0 && arg_count == 3) {
                do_checkpoint(args[1], args[2]);
            } else if (strcmp(args[0], "viewcheckpoint") == 0 && arg_count == 3) {
                do_viewcheckpoint(args[1], args[2]);
            } else if (strcmp(args[0], "revert") == 0 && arg_count == 3) {
                do_revert(args[1], args[2]);
            } else if (strcmp(args[0], "listcheckpoints") == 0 && arg_count == 2) {
                do_listcheckpoints(args[1]);
            } else if (strcmp(args[0], "createfolder") == 0 && arg_count == 2) {
                do_createfolder(args[1]);
            } else if (strcmp(args[0], "move") == 0 && arg_count == 3) {
                do_move(args[1], args[2]);
            } else if (strcmp(args[0], "viewfolder") == 0 && arg_count == 2) {
                do_viewfolder(args[1]);
            } else if (strcmp(args[0], "exec") == 0 && arg_count == 2) {
                do_exec(args[1]);
            } else if (strcmp(args[0], "addaccess") == 0 && arg_count == 4) {
                do_addaccess(args[1], args[2], args[3]);
            } else if (strcmp(args[0], "remaccess") == 0 && arg_count == 4) {
                do_remaccess(args[1], args[2], args[3]);
            } else if (strcmp(args[0], "reqaccess") == 0 && arg_count == 3) {
                do_reqaccess(args[1], args[2]);
            } else if (strcmp(args[0], "reqlist") == 0) {
                do_reqlist();
            } else if (strcmp(args[0], "approve") == 0 && arg_count == 2) {
                do_approve(args[1]);
            } else if (strcmp(args[0], "reject") == 0 && arg_count == 2) {
                do_reject(args[1]);
            } else if (strcmp(args[0], "exit") == 0) {
                break;
            } else {
                printf("Unknown command or wrong args.\n");
                printf("Commands:\n");
                printf("  list | view [-a] [-l] | create <path> | delete <path>\n");
                printf("  read <path> | stream <path> | info <path> | write <path> <index>\n");
                printf("  undo <path> | checkpoint <path> <tag> | viewcheckpoint <path> <tag>\n");
                printf("  revert <path> <tag> | listcheckpoints <path> | exec <path>\n");
                printf("  createfolder <path> | move <old_path> <new_path> | viewfolder <path>\n");
                printf("  addaccess <path> <username> <type> | remaccess <path> <username> <type>\n");
                printf("  reqaccess <path> <type> | reqlist | approve <id> | reject <id>\n");
                printf("  exit\n");
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