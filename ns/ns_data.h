#ifndef NS_DATA_H
#define NS_DATA_H

#include "../include/common.h"
#include <time.h>

// --- Storage Server Management ---
typedef struct SSInfo {
    int cmd_sock;   // For NS→SS commands
    int update_sock;  // For SS→NS async updates
    pthread_mutex_t cmd_lock;  // Protects cmd_sock
    char ip[INET_ADDRSTRLEN];
    int client_port;
    int backup_port;  // Port for partner sync
    int is_active;
    time_t last_heartbeat;
    
    char name[MAX_PATH_LEN];        // SS identifier
    int backed_up;                  // 0=needs partner, 1=has partner
    char partner_name[MAX_PATH_LEN]; // Current partner's name
    
    int active_writers;             // Number of active write sessions on this SS
    pthread_mutex_t writers_lock;   // Protects active_writers counter
    
    struct SSInfo* next;
} SSInfo;

// --- File & Metadata Management ---
typedef struct FileInfo {
    char path[MAX_PATH_LEN];
    char owner[MAX_USERNAME_LEN];
    
    // Metadata from info_dir
    time_t created;
    time_t modified;
    time_t last_accessed;
    char last_accessed_by[MAX_USERNAME_LEN];
    long size; // in bytes
    int word_count;
    int char_count;
    
    // Access lists
    char** read_access;
    int read_count;
    char** write_access;
    int write_count;
    char** exec_access; 
    int exec_count;
    
    SSInfo** ss_list; // List of SSs that have this file
    int ss_count;

    struct FileInfo* next; // For hash map collision
} FileInfo;

// Simple hash map for file index
#define FILE_INDEX_SIZE 1024
typedef struct {
    FileInfo* table[FILE_INDEX_SIZE];
    pthread_mutex_t lock;
} FileIndex;

// --- LRU Cache for File Metadata ---
#define LRU_CACHE_SIZE 2
typedef struct LRUNode {
    char path[MAX_PATH_LEN];
    FileInfo* file_info;  // Cached copy of FileInfo
    struct LRUNode* prev;
    struct LRUNode* next;
} LRUNode;

typedef struct {
    LRUNode* head;  // Most recently used
    LRUNode* tail;  // Least recently used
    int size;
    pthread_mutex_t lock;
} LRUCache;

// --- Client Management ---
typedef struct ClientInfo {
    char username[MAX_USERNAME_LEN];
    int sock;
    struct ClientInfo* next;
} ClientInfo;

typedef struct {
    ClientInfo* head;
    pthread_mutex_t lock;
} ClientList;

// --- Access Request Management ---
typedef struct AccessRequest {
    int id;
    char username[MAX_USERNAME_LEN];
    char path[MAX_PATH_LEN];
    char type; // 'R', 'W', 'X'
    struct AccessRequest* next;
} AccessRequest;

typedef struct {
    AccessRequest* head;
    int next_id;
    pthread_mutex_t lock;
} AccessRequestList;


// --- Global NS State ---
typedef struct {
    ClientList* current_clients; 
    ClientList* all_clients;     
    SSInfo* ss_list_head;        
    pthread_mutex_t ss_list_lock;
    FileIndex* file_index;
    LRUCache* lru_cache;  // LRU cache for file metadata
    AccessRequestList* access_requests; 
} NameServer;

// --- GLOBAL VARIABLE DECLARATION ---
extern NameServer ns; 

// --- Function Prototypes ---
void ns_init(NameServer* ns);
void ns_add_client(ClientList* list, const char* username, int sock);
int ns_is_client_in_list(ClientList* list, const char* username);
void ns_remove_client(ClientList* list, const char* username);

// --- Index Function Prototypes ---
FileInfo* ns_file_index_get(const char* path);
FileInfo* ns_file_get_cached(const char* path);  // Cache-aware file lookup
void free_file_info_copy(FileInfo* file);        // Free cached file copy
ErrorCode ns_file_index_add(const char* path, const char* owner, SSInfo* ss);
ErrorCode ns_file_index_add_with_ss(const char* path, const char* owner, SSInfo** ss_list, int ss_count);
void ns_file_index_remove(const char* path);
void ns_cleanup_ss_from_index(SSInfo* dead_ss);

void ns_parse_and_index_file(const char* path, const char* info_content, SSInfo* ss);
void ns_file_add_ss(FileInfo* file, SSInfo* ss);
void ns_add_partner_to_files(SSInfo* source_ss, SSInfo* partner_ss);
void ns_update_file_metadata(FileInfo* file, const char* info_content);
void ns_broadcast_info_update(FileInfo* file);

// --- LRU Cache Function Prototypes ---
LRUCache* lru_cache_init();
FileInfo* lru_cache_get(LRUCache* cache, const char* path);
void lru_cache_put(LRUCache* cache, const char* path, FileInfo* file);
void lru_cache_invalidate(LRUCache* cache, const char* path);
void lru_cache_clear(LRUCache* cache);  // Clear all cached entries
void lru_cache_free(LRUCache* cache);

// --- Access Control Function Prototypes ---
int ns_check_permission(FileInfo* file, const char* username, char type);
int ns_has_read_access(FileInfo* file, const char* username);
int ns_has_write_access(FileInfo* file, const char* username);
ErrorCode ns_add_access(FileInfo* file, const char* username, char type);
ErrorCode ns_remove_access(FileInfo* file, const char* username, char type);
int ns_add_access_request(const char* username, const char* path, char type);
AccessRequest* ns_get_access_request(int request_id);
void ns_remove_access_request(int request_id);

// --- Persistence Function Prototypes ---
void ns_save_all_users();
void ns_load_all_users();
void ns_save_access_requests();
void ns_load_access_requests();

// --- Backup Helper Function Prototypes ---
SSInfo* find_unbacked_ss();
void pair_storage_servers(SSInfo* empty_ss, SSInfo* regular_ss);
void send_pair_command(SSInfo* ss, const char* partner_name, const char* partner_ip, int partner_port, int should_send_sync);

#endif // NS_DATA_H