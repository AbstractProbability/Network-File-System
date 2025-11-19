#ifndef SS_H
#define SS_H

#include "../include/common.h"
#include <sys/stat.h>

// Global configuration for the Storage Server
typedef struct {
    char root_dir[MAX_PATH_LEN];
    char ip[INET_ADDRSTRLEN];
    int ns_port;
    int client_port; // The port this SS listens on for clients
    int cmd_sock;    // Socket for receiving NS commands
    int update_sock; // Socket for sending async updates to NS
} SS_Config;

// Backup state for partner synchronization
typedef struct {
    char name[MAX_PATH_LEN];           // My name (from root dir)
    char partner_name[MAX_PATH_LEN];   // Partner's name (if paired)
    char partner_ip[INET_ADDRSTRLEN];  // Partner's IP
    int partner_backup_port;            // Partner's backup port
    int partner_sync_sock;              // Socket to partner (-1 if not connected)
    int backup_listen_sock;             // Listen socket for partner
    int backup_port;                    // My backup port
    int should_send_full_sync;          // Whether to send full sync on connection (set by NS)
    pthread_mutex_t sync_lock;          // Protects partner_sync_sock
} BackupState;

extern SS_Config g_config;
extern BackupState g_backup_state;

// --- MODIFIED: In-Memory File Structures ---

typedef struct WordNode {
    char word[MAX_WORD_LEN];
    char* trailing_whitespace; 
    struct WordNode* next;
} WordNode;

typedef struct SentenceNode {
    char* leading_whitespace;  
    WordNode* words;
    char punctuation;          // '.', '?', '!', or '\0'
    char* lock_holder_username; // NEW: NULL if unlocked, username if locked
    struct SentenceNode* next;
    pthread_mutex_t lock;      // Mutex to *protect this struct* (not the write lock)
} SentenceNode;

typedef struct OpenFile {
    char path[MAX_PATH_LEN];
    SentenceNode* sentences;
    pthread_rwlock_t global_lock; // Global lock for commit/append
    struct OpenFile* next;
} OpenFile;

// --- Function Prototypes ---

// ss_main.c
void ss_init_dirs();
void ss_register_with_ns();
void* client_listener_thread(void* arg);
void* handle_ns_commands(void* arg);
int connect_to_server(const char* ip, int port);
void extract_ss_name(const char* root_path, char* name_out);
void* backup_listener_thread(void* arg);
void* connect_and_sync_to_partner(void* arg);
void* handle_partner_sync(void* arg);
void sync_all_files_to_partner();
void sync_operation_to_partner(SSSyncOpCode opcode, const char* path, const char* arg2);
void sync_file_to_partner(const char* path); 

// ss_comms.c
void* handle_client_connection(void* arg); 

// ss_ops.c
void handle_op_read(int sock, ClientRequest* req);
void handle_op_write(int sock, ClientRequest* req);
void handle_op_stream(int sock, ClientRequest* req);
ErrorCode handle_op_create(const char* path, const char* owner);
ErrorCode handle_op_delete(const char* path);
ErrorCode handle_op_undo(const char* path, const char* username);
ErrorCode handle_op_checkpoint(const char* path, const char* checkpoint_tag);
ErrorCode handle_op_revert(const char* path, const char* checkpoint_tag, const char* username);
void handle_op_listcheckpoints(int sock, const char* path);
void handle_op_createfolder(const char* path, int sock);
void handle_op_move(const char* old_path, const char* new_path, int sock);

// ss_ops.c helpers
OpenFile* get_open_file(const char* path);
void remove_open_file(const char* path);
SentenceNode* get_sentence_by_index(OpenFile* file, int index);
ErrorCode commit_file_to_disk(OpenFile* file, const char* username);
SentenceNode* parse_string_to_sentences(const char* str, SentenceNode** out_last_sentence); 
void free_sentence_list(SentenceNode* head); 
void str_append(char** str, char c); 
char* serialize_sentence(SentenceNode* s); // NEW
WordNode* parse_content_to_words(const char* content, WordNode** out_last_word); // NEW

// Helper
void get_full_path(const char* dir_type, const char* path, char* out_buf);
int is_dir_empty(const char* path);
void update_info_file(const char* path, const char* username, int update_modified);
void count_words_and_chars(const char* content, int* word_count, int* char_count);
void send_info_update_to_ns(const char* path);

#endif // SS_H