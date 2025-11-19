#include "ss.h"
#include "../include/serialize.h"
#include <time.h>
#include <ctype.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>

// --- Externs from ss.c ---
extern OpenFile* g_open_files_list;
extern pthread_mutex_t g_open_files_mutex;
extern SS_Config g_config;

// --- Forward Declarations for STREAM ---
static void stream_content_to_client(int sock, const char* content);
static char* serialize_all_sentences(SentenceNode* head);

// --- Helper Functions ---
void get_full_path(const char* dir_type, const char* path, char* out_buf) {
    sprintf(out_buf, "%s/%s/%s", g_config.root_dir, dir_type, path);
}

// Helper to append a char to a dynamic string
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

// --- Helper to count words and characters ---
void count_words_and_chars(const char* content, int* word_count, int* char_count) {
    *word_count = 0;
    *char_count = 0;
    
    int in_word = 0;
    int i = 0;
    
    while (content[i] != '\0') {
        char c = content[i];
        (*char_count)++;
        
        if (isspace(c)) {
            in_word = 0;
        } else {
            if (!in_word) {
                (*word_count)++;
                in_word = 1;
            }
        }
        i++;
    }
}

// --- Helper to update info file with current metadata ---
// update_modified: if true, updates the 'modified' timestamp (use for WRITE, UNDO, CHECKPOINT)
// If false, only updates access time and file metrics (use for READ)
void update_info_file(const char* path, const char* username, int update_modified) {
    char file_path[MAX_PATH_LEN * 2];
    char info_path[MAX_PATH_LEN * 2];
    
    get_full_path("file_dir", path, file_path);
    get_full_path("info_dir", path, info_path);
    
    // Read existing info file to preserve owner, created time, and access lists
    char owner[MAX_USERNAME_LEN] = "unknown";
    time_t created = time(NULL);
    time_t modified = time(NULL);
    time_t old_last_accessed = 0;
    char old_last_accessed_by[MAX_USERNAME_LEN] = "";
    char read_access[1024] = "";
    char write_access[1024] = "";
    char exec_access[1024] = "";
    
    FILE* old_info = fopen(info_path, "r");
    if (old_info != NULL) {
        char line[1024];
        while (fgets(line, sizeof(line), old_info)) {
            if (sscanf(line, "owner: %s", owner) == 1) continue;
            if (sscanf(line, "created: %ld", &created) == 1) continue;
            if (sscanf(line, "modified: %ld", &modified) == 1) continue;
            if (sscanf(line, "last_accessed: %ld", &old_last_accessed) == 1) continue;
            if (sscanf(line, "last_accessed_by: %s", old_last_accessed_by) == 1) continue;
            
            // Parse access lists
            if (strncmp(line, "read_access: ", 13) == 0) {
                strncpy(read_access, line + 13, sizeof(read_access) - 1);
                read_access[strcspn(read_access, "\n")] = 0; // Remove newline
                continue;
            }
            if (strncmp(line, "write_access: ", 14) == 0) {
                strncpy(write_access, line + 14, sizeof(write_access) - 1);
                write_access[strcspn(write_access, "\n")] = 0;
                continue;
            }
            if (strncmp(line, "exec_access: ", 13) == 0) {
                strncpy(exec_access, line + 13, sizeof(exec_access) - 1);
                exec_access[strcspn(exec_access, "\n")] = 0;
                continue;
            }
        }
        fclose(old_info);
    } else {
        // New file, use username as owner if provided
        if (username != NULL) {
            strncpy(owner, username, MAX_USERNAME_LEN - 1);
            owner[MAX_USERNAME_LEN - 1] = '\0';
        }
    }
    
    // Get file size and count words/chars
    struct stat st;
    long size = 0;
    int word_count = 0, char_count = 0;
    
    if (stat(file_path, &st) == 0) {
        size = st.st_size;
        
        // Read file content to count words
        FILE* f = fopen(file_path, "r");
        if (f != NULL) {
            fseek(f, 0, SEEK_END);
            long fsize = ftell(f);
            fseek(f, 0, SEEK_SET);
            
            char* content = (char*)malloc(fsize + 1);
            if (content != NULL) {
                fread(content, 1, fsize, f);
                content[fsize] = '\0';
                
                count_words_and_chars(content, &word_count, &char_count);
                free(content);
            }
            fclose(f);
        }
    }
    
    // Write updated info file
    FILE* info_file = fopen(info_path, "w");
    if (info_file != NULL) {
        time_t now = time(NULL);
        
        fprintf(info_file, "owner: %s\n", owner);
        fprintf(info_file, "created: %ld\n", created);
        
        // Update modified time only if requested (for WRITE/UNDO/CHECKPOINT operations)
        if (update_modified) {
            fprintf(info_file, "modified: %ld\n", now);
        } else {
            // Preserve existing modified time (for READ operations)
            fprintf(info_file, "modified: %ld\n", modified);
        }
        
        // ALWAYS update last_accessed for any operation
        // Use current username if provided, otherwise preserve old value
        if (username != NULL && strlen(username) > 0) {
            fprintf(info_file, "last_accessed: %ld\n", now);
            fprintf(info_file, "last_accessed_by: %s\n", username);
            printf("SS: INFO UPDATE - Setting last_accessed=%ld, last_accessed_by=%s\n", now, username);
        } else if (old_last_accessed > 0) {
            // Preserve old values if no new username provided
            fprintf(info_file, "last_accessed: %ld\n", old_last_accessed);
            if (strlen(old_last_accessed_by) > 0) {
                fprintf(info_file, "last_accessed_by: %s\n", old_last_accessed_by);
            }
            printf("SS: INFO UPDATE - Preserving old last_accessed=%ld, last_accessed_by=%s (username was NULL/empty)\n", 
                   old_last_accessed, old_last_accessed_by);
        }
        
        fprintf(info_file, "size: %ld\n", size);
        fprintf(info_file, "word_count: %d\n", word_count);
        fprintf(info_file, "char_count: %d\n", char_count);
        
        // Write access lists if they exist
        if (strlen(read_access) > 0) {
            fprintf(info_file, "read_access: %s\n", read_access);
        }
        if (strlen(write_access) > 0) {
            fprintf(info_file, "write_access: %s\n", write_access);
        }
        if (strlen(exec_access) > 0) {
            fprintf(info_file, "exec_access: %s\n", exec_access);
        }
        
        fprintf(info_file, "\n");
        fclose(info_file);
        
        printf("SS: Updated info file for '%s' (size: %ld, words: %d, chars: %d, modified: %d)\n", 
               path, size, word_count, char_count, update_modified);
    }
}

// Send updated info file content to NS
void send_info_update_to_ns(const char* path) {
    char info_path[MAX_PATH_LEN * 2];
    get_full_path("info_dir", path, info_path);
    
    FILE* f = fopen(info_path, "r");
    if (f == NULL) {
        printf("SS: Could not open info file to send to NS: %s\n", info_path);
        return;
    }
    
    // Read the entire info file
    char info_content[MAX_INFO_LEN];
    memset(info_content, 0, sizeof(info_content));
    size_t len = fread(info_content, 1, MAX_INFO_LEN - 1, f);
    fclose(f);
    
    if (len == 0) {
        printf("SS: Info file is empty for '%s'\n", path);
        return;
    }
    
    // Send update to NS on update socket
    SSOpCode opcode = SS_NS_UPDATE_INFO;
    SEND_OPCODE(g_config.update_sock, opcode);
    
    // Send path
    char path_buf[MAX_PATH_LEN];
    memset(path_buf, 0, sizeof(path_buf));
    strncpy(path_buf, path, MAX_PATH_LEN - 1);
    send_full(g_config.update_sock, path_buf, MAX_PATH_LEN);
    
    // Send info content
    send_full(g_config.update_sock, info_content, MAX_INFO_LEN);
    
    printf("SS: Sent info update to NS for '%s'\n", path);
    printf("SS: Info content sent:\n%s\n", info_content);
}


// --- Helper to serialize a *single* sentence to a string ---
char* serialize_sentence(SentenceNode* s) {
    // Calculate size first
    size_t total_len = 1; // For null terminator
    if (s->leading_whitespace) total_len += strlen(s->leading_whitespace);
    
    WordNode* w = s->words;
    while (w != NULL) {
        total_len += strlen(w->word);
        if (w->trailing_whitespace) total_len += strlen(w->trailing_whitespace);
        w = w->next;
    }
    
    if (s->punctuation != '\0') total_len++;

    // Allocate and build
    char* str = (char*)calloc(1, total_len); // Use calloc for safety
    if (str == NULL) return NULL;
    
    if (s->leading_whitespace) strcat(str, s->leading_whitespace);
    w = s->words;
    while (w != NULL) {
        strcat(str, w->word);
        if (w->trailing_whitespace) strcat(str, w->trailing_whitespace);
        w = w->next;
    }
    if (s->punctuation != '\0') {
        char punc[2] = {s->punctuation, '\0'};
        strcat(str, punc);
    }
    
    return str;
}


// --- Helper to free a sentence list ---
void free_sentence_list(SentenceNode* head) {
    SentenceNode* current_s = head;
    while (current_s != NULL) {
        if (current_s->leading_whitespace) free(current_s->leading_whitespace);
        if (current_s->lock_holder_username) free(current_s->lock_holder_username);
        
        WordNode* current_w = current_s->words;
        while (current_w != NULL) {
            if (current_w->trailing_whitespace) free(current_w->trailing_whitespace);
            
            WordNode* next_w = current_w->next;
            free(current_w);
            current_w = next_w;
        }
        
        SentenceNode* next_s = current_s->next;
        pthread_mutex_destroy(&current_s->lock);
        free(current_s);
        current_s = next_s;
    }
}

// --- Robust Parser ---

typedef enum {
    STATE_PRE_SENTENCE,  // Accumulating leading whitespace
    STATE_IN_WORD,       // Accumulating a word
    STATE_POST_WORD      // Accumulating trailing whitespace
} ParserState;

// This helper parses the client's new content string
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

// This helper parses a full file from disk
SentenceNode* parse_string_to_sentences(const char* str, SentenceNode** out_last_sentence) {
    SentenceNode* head_sentence = (SentenceNode*)calloc(1, sizeof(SentenceNode));
    pthread_mutex_init(&head_sentence->lock, NULL);
    
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
            if (is_space) {
                str_append(&current_sentence->leading_whitespace, c);
            } else if (is_punc) {
                current_sentence->punctuation = c;
                SentenceNode* new_s = (SentenceNode*)calloc(1, sizeof(SentenceNode));
                pthread_mutex_init(&new_s->lock, NULL);
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
                pthread_mutex_init(&new_s->lock, NULL);
                current_sentence->next = new_s;
                current_sentence = new_s;
                state = STATE_PRE_SENTENCE;
            } else {
                if (word_index < MAX_WORD_LEN - 1) {
                    word_buffer[word_index++] = c;
                }
            }
        } else if (state == STATE_POST_WORD) {
            if (is_space) {
                str_append(&current_word->trailing_whitespace, c);
            } else if (is_punc) {
                current_sentence->punctuation = c;
                SentenceNode* new_s = (SentenceNode*)calloc(1, sizeof(SentenceNode));
                pthread_mutex_init(&new_s->lock, NULL);
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

    if (out_last_sentence != NULL) {
        *out_last_sentence = current_sentence;
    }
    return head_sentence;
}

OpenFile* load_file_to_memory(const char* path) {
    printf("SS: Loading file '%s' into memory.\n", path);
    char file_path[MAX_PATH_LEN * 2];
    get_full_path("file_dir", path, file_path);

    FILE* f = fopen(file_path, "r");
    if (f == NULL) return NULL; 

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *str_content = (char*)malloc(fsize + 1);
    fread(str_content, 1, fsize, f);
    fclose(f);
    str_content[fsize] = '\0';

    OpenFile* file = (OpenFile*)calloc(1, sizeof(OpenFile));
    strncpy(file->path, path, MAX_PATH_LEN);
    pthread_rwlock_init(&file->global_lock, NULL);
    
    file->sentences = parse_string_to_sentences(str_content, NULL);
    
    free(str_content);
    return file;
}
// --- END PARSER ---

// --- Helper to get (and load if needed) an open file ---
OpenFile* get_open_file(const char* path) {
    pthread_mutex_lock(&g_open_files_mutex);
    
    OpenFile* current = g_open_files_list;
    while (current != NULL) {
        if (strcmp(current->path, path) == 0) {
            pthread_mutex_unlock(&g_open_files_mutex);
            return current;
        }
        current = current->next;
    }
    
    OpenFile* new_file = load_file_to_memory(path);
    if (new_file == NULL) {
        pthread_mutex_unlock(&g_open_files_mutex);
        return NULL; 
    }
    
    new_file->next = g_open_files_list;
    g_open_files_list = new_file;
    
    pthread_mutex_unlock(&g_open_files_mutex);
    return new_file;
}

// --- Helper to remove and free an OpenFile from the global list ---
void remove_open_file(const char* path) {
    pthread_mutex_lock(&g_open_files_mutex);
    
    OpenFile* current = g_open_files_list;
    OpenFile* prev = NULL;
    
    while (current != NULL) {
        if (strcmp(current->path, path) == 0) {
            // Found the file to remove
            if (prev == NULL) {
                // First node in list
                g_open_files_list = current->next;
            } else {
                // Middle or end of list
                prev->next = current->next;
            }
            
            // Free all memory associated with this OpenFile
            printf("SS: Removing in-memory representation of '%s'\n", path);
            
            // Destroy the global lock
            pthread_rwlock_destroy(&current->global_lock);
            
            // Free the sentence list (includes all words, whitespace, and sentence locks)
            free_sentence_list(current->sentences);
            
            // Free the OpenFile structure itself
            free(current);
            
            printf("SS: In-memory file '%s' cleaned up successfully\n", path);
            break;
        }
        prev = current;
        current = current->next;
    }
    
    pthread_mutex_unlock(&g_open_files_mutex);
}

// --- Helper to get a specific sentence node ---
SentenceNode* get_sentence_by_index(OpenFile* file, int index) {
    SentenceNode* current = file->sentences;
    int i = 0;
    while (current != NULL && i < index) {
        // Don't count the trailing, empty sentence
        if (current->next == NULL && current->words == NULL && current->leading_whitespace == NULL) {
            return NULL;
        }
        current = current->next;
        i++;
    }
    
    // Check again for the trailing empty node
    if (current != NULL && i == index) {
         if (current->next == NULL && current->words == NULL && current->leading_whitespace == NULL) {
            // This is the *empty* trailing node, which only counts
            // as an append target, not a valid get()
            return NULL;
         }
    }

    return (i == index) ? current : NULL;
}

// --- Atomic commit function ---
ErrorCode commit_file_to_disk(OpenFile* file, const char* username) {
    printf("SS: Committing '%s' to disk.\n", file->path);
    
    // NOTE: This function assumes the caller (handle_op_write)
    // is holding the global_lock (write mode).
    
    char file_path[MAX_PATH_LEN * 2];
    char temp_path[MAX_PATH_LEN * 2];
    char undo_path[MAX_PATH_LEN * 2];
    
    get_full_path("file_dir", file->path, file_path);
    get_full_path("file_dir", "temp_write.tmp", temp_path);
    get_full_path("undo_dir", file->path, undo_path);

    FILE* f_temp = fopen(temp_path, "w");
    if (f_temp == NULL) {
        return ERR_INVALID_PATH;
    }
    
    SentenceNode* s = file->sentences;
    while (s != NULL) {
        if (s->next == NULL && s->words == NULL && s->leading_whitespace == NULL) {
            break;
        }
        
        if (s->leading_whitespace) {
            fprintf(f_temp, "%s", s->leading_whitespace);
        }
        
        WordNode* w = s->words;
        while (w != NULL) {
            fprintf(f_temp, "%s", w->word);
            if (w->trailing_whitespace) {
                fprintf(f_temp, "%s", w->trailing_whitespace);
            }
            w = w->next;
        }
        
        if (s->punctuation != '\0') {
            fputc(s->punctuation, f_temp);
        }
        
        s = s->next;
    }
    fclose(f_temp);

    // Atomic rename
    rename(file_path, undo_path);
    if (rename(temp_path, file_path) != 0) {
        perror("commit rename failed");
        return ERR_INVALID_PATH;
    }

    // Update info file after successful commit (updates modified time)
    update_info_file(file->path, username, 1);
    
    // Send updated info to NS
    send_info_update_to_ns(file->path);

    printf("SS: Commit for '%s' complete.\n", file->path);
    return ERR_OK;
}


// --- Type 3 Operations ---

// --- READ Operation (with Locking) ---
void handle_op_read(int sock, ClientRequest* req) {
    char file_path[MAX_PATH_LEN * 2];
    get_full_path("file_dir", req->path, file_path);
    
    printf("SS: READ operation for '%s' by user '%s'\n", req->path, req->username);
    
    OpenFile* file = get_open_file(req->path);
    if (file == NULL) {
        char* err_msg = "Error: File not found on this SS.";
        send(sock, err_msg, strlen(err_msg), 0);
        send(sock, "STOP", 5, 0); 
        return;
    }

    printf("SS: Client acquiring read lock for '%s'\n", req->path);
    pthread_rwlock_rdlock(&file->global_lock);
    printf("SS: Client acquired read lock.\n");
    
    FILE* f = fopen(file_path, "r");
    if (f == NULL) {
        pthread_rwlock_unlock(&file->global_lock); 
        char* err_msg = "Error: Could not open file on SS.";
        send(sock, err_msg, strlen(err_msg), 0);
        send(sock, "STOP", 5, 0); 
        return;
    }

    char buffer[MAX_BUFFER_LEN];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, MAX_BUFFER_LEN, f)) > 0) {
        if (send(sock, buffer, bytes_read, 0) < 0) {
            break; 
        }
    }
    
    fclose(f);
    
    pthread_rwlock_unlock(&file->global_lock);
    printf("SS: Client released read lock.\n");
    
    send(sock, "STOP", 5, 0);
    
    // Update access time in info file (but not modified time)
    update_info_file(req->path, req->username, 0);
    
    // Send updated info to NS
    send_info_update_to_ns(req->path);
}
// --- END READ Operation ---

// --- WRITE Operation (Corrected Logic) ---
void handle_op_write(int sock, ClientRequest* req) {
    ServerResponse res;
    memset(&res, 0, sizeof(ServerResponse));
    
    OpenFile* file = get_open_file(req->path);
    if (file == NULL) {
        res.status = ERR_FILE_NOT_FOUND;
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }

    // --- PHASE 1: LOCK AND SEND SENTENCE ---
    
    pthread_rwlock_rdlock(&file->global_lock);
    int sentence_index = req->index;
    SentenceNode* s_locked = get_sentence_by_index(file, sentence_index);
    
    if (s_locked == NULL) {
        // --- Append or Empty File case ---
        if (sentence_index == 0) {
            // Writing to empty file at index 0
            s_locked = file->sentences; // This is the empty head node
            printf("SS: Writing to empty file at index 0\n");
        } else {
            // This is the "just out of bounds" append case
            SentenceNode* s0 = get_sentence_by_index(file, sentence_index - 1);
            
            if (s0 == NULL || s0->punctuation == '\0') {
                pthread_rwlock_unlock(&file->global_lock);
                res.status = (s0 == NULL) ? ERR_INDEX_OUT_OF_BOUNDS : ERR_ACCESS_DENIED;
                sprintf(res.message, "Error: Cannot append to sentence %d.", sentence_index);
                SEND_SERVER_RESPONSE(sock, &res);
                return;
            }
            
            // It's a valid append. s_locked is the empty trailing node.
            s_locked = s0->next;
            printf("SS: Appending new content at index %d\n", sentence_index);
        }
    }
    pthread_rwlock_unlock(&file->global_lock); // Release global lock

    // 2. Lock the sentence
    pthread_mutex_lock(&s_locked->lock); 
    if (s_locked->lock_holder_username != NULL) {
        // Locked by someone else
        pthread_mutex_unlock(&s_locked->lock);
        res.status = ERR_SENTENCE_LOCKED;
        sprintf(res.message, "Error: Sentence %d is locked by %s", sentence_index, s_locked->lock_holder_username);
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }
    s_locked->lock_holder_username = strdup(req->username);
    pthread_mutex_unlock(&s_locked->lock);

    // 3. Serialize sentence content and send ACK
    char* sentence_content = serialize_sentence(s_locked);
    res.status = ERR_OK;
    snprintf(res.message, MAX_BUFFER_LEN, "Sentence %d locked. Content: %s", sentence_index, sentence_content);
    SEND_SERVER_RESPONSE(sock, &res);
    free(sentence_content);

    // --- PHASE 2: WAIT FOR COMMIT ---
    
    ClientWritePacket write_pkt;
    if (RECV_CLIENT_WRITE_PACKET(sock, &write_pkt) <= 0) {
        printf("SS: Client disconnected before committing.\n");
        // Release the lock
        pthread_mutex_lock(&s_locked->lock);
        free(s_locked->lock_holder_username);
        s_locked->lock_holder_username = NULL;
        pthread_mutex_unlock(&s_locked->lock);
        return;
    }
    printf("SS: Received ETIRW content block from client.\n");

    // 4. Parse the new content
    SentenceNode* new_s_head = NULL;
    SentenceNode* new_s_tail = NULL;
    new_s_head = parse_string_to_sentences(write_pkt.content, &new_s_tail);
    
    // 5. --- THIS IS THE FIX ---
    // Perform the graft (atomically)
    printf("SS: Acquiring global lock for commit...\n");
    pthread_rwlock_wrlock(&file->global_lock);
    
    // Find the node to replace *by username*
    SentenceNode* s0 = NULL; // Node before
    SentenceNode* s_curr = file->sentences;
    while (s_curr != NULL) {
        pthread_mutex_lock(&s_curr->lock);
        int found = 0;
        if (s_curr->lock_holder_username != NULL && strcmp(s_curr->lock_holder_username, req->username) == 0) {
            found = 1;
        }
        pthread_mutex_unlock(&s_curr->lock);

        if (found) {
            break; // Found it
        }
        s0 = s_curr;
        s_curr = s_curr->next;
    }
    
    if (s_curr == NULL) {
        // This should not happen if lock was acquired
        pthread_rwlock_unlock(&file->global_lock);
        res.status = ERR_ACCESS_DENIED;
        sprintf(res.message, "Error: Could not find locked sentence. Commit aborted.");
        SEND_SERVER_RESPONSE(sock, &res);
        return;
    }

    // Now, s0, s_curr (s_locked), and s2 are all correct
    SentenceNode* s_locked_correct = s_curr;
    SentenceNode* s2 = s_locked_correct->next; // The node *after* the one we replace
    
    if (s0 == NULL) { // Grafting at the head of the file
        file->sentences = new_s_head;
    } else {
        s0->next = new_s_head;
    }
    
    // Find the actual tail of the new list
    SentenceNode* real_new_tail = new_s_head;
    if (real_new_tail) {
        while(real_new_tail->next != NULL) {
            SentenceNode* next = real_new_tail->next;
            if (next->next == NULL && next->words == NULL && next->leading_whitespace == NULL) {
                break;
            }
            real_new_tail = real_new_tail->next;
        }
    }
    
    real_new_tail->next = s2; // Link the end of the new list to s2
    
    // 6. Free the old, replaced sentence(s)
    s_locked_correct->next = NULL; // Decouple it first
    free_sentence_list(s_locked_correct); // This also frees the lock_holder_username

    // 7. Commit to disk (this no longer releases the lock)
    ErrorCode commit_status = commit_file_to_disk(file, req->username);
    
    // 8. Release the global lock
    pthread_rwlock_unlock(&file->global_lock);
    printf("SS: Commit and global lock release complete.\n");
    
    // 9. Send final ACK to client
    if (commit_status == ERR_OK) {
        res.status = ERR_OK;
        sprintf(res.message, "Write complete.");
        
        // Sync file to backup partner after successful write
        sync_file_to_partner(req->path);
    } else {
        res.status = commit_status;
        sprintf(res.message, "Error: Commit to disk failed.");
    }
    SEND_SERVER_RESPONSE(sock, &res);
}
// --- END WRITE Operation ---

void handle_op_stream(int sock, ClientRequest* req) {
    ServerResponse res;
    memset(&res, 0, sizeof(ServerResponse));
    
    char file_path[MAX_PATH_LEN * 2];
    get_full_path("file_dir", req->path, file_path);
    
    // 1. Get the OpenFile structure (or load from disk if not open)
    OpenFile* of = get_open_file(req->path);
    
    if (of == NULL) {
        // File not in memory - load it from disk
        FILE* f = fopen(file_path, "r");
        if (f == NULL) {
            res.status = ERR_FILE_NOT_FOUND;
            sprintf(res.message, "Error: File not found.");
            SEND_SERVER_RESPONSE(sock, &res);
            return;
        }
        
        // Read the entire file into a buffer
        fseek(f, 0, SEEK_END);
        long file_size = ftell(f);
        fseek(f, 0, SEEK_SET);
        
        char* content = (char*)malloc(file_size + 1);
        fread(content, 1, file_size, f);
        content[file_size] = '\0';
        fclose(f);
        
        // Send success status first
        res.status = ERR_OK;
        SEND_SERVER_RESPONSE(sock, &res);
        
        // Stream the content word by word
        stream_content_to_client(sock, content);
        
        free(content);
    } else {
        // File is in memory - acquire read lock to make a copy
        pthread_rwlock_rdlock(&of->global_lock);
        
        // Serialize the sentence structure to a string
        char* content = serialize_all_sentences(of->sentences);
        
        pthread_rwlock_unlock(&of->global_lock);
        
        // Send success status
        res.status = ERR_OK;
        SEND_SERVER_RESPONSE(sock, &res);
        
        // Now stream the copied content (lock is released)
        stream_content_to_client(sock, content);
        
        free(content);
    }
    
    // Update access time
    update_info_file(req->path, req->username, 0);
    
    // Send updated info to NS
    send_info_update_to_ns(req->path);
}

// Helper function to stream content word-by-word with 0.1s delay
static void stream_content_to_client(int sock, const char* content) {
    char word[MAX_WORD_LEN];
    int word_idx = 0;
    int i = 0;
    
    printf("SS: Starting stream of content (%zu chars)\n", strlen(content));
    
    while (content[i] != '\0') {
        char c = content[i];
        
        // Check if we hit whitespace or punctuation
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
            c == '.' || c == '?' || c == '!') {
            
            // Send the word if we have one
            if (word_idx > 0) {
                word[word_idx] = '\0';
                
                // Send the word
                if (send(sock, word, strlen(word) + 1, 0) <= 0) {
                    printf("SS: Client disconnected during stream\n");
                    return;
                }
                
                // Wait 0.1 seconds (100,000 microseconds)
                usleep(100000);
                
                word_idx = 0;
            }
            
            // Send whitespace/punctuation as-is
            char ws[2] = {c, '\0'};
            if (send(sock, ws, 2, 0) <= 0) {
                printf("SS: Client disconnected during stream\n");
                return;
            }
            
            // Small delay for whitespace too
            usleep(10000); // 0.01s for whitespace
            
        } else {
            // Build up the word
            if (word_idx < MAX_WORD_LEN - 1) {
                word[word_idx++] = c;
            }
        }
        
        i++;
    }
    
    // Send any remaining word
    if (word_idx > 0) {
        word[word_idx] = '\0';
        if (send(sock, word, strlen(word) + 1, 0) <= 0) {
            printf("SS: Client disconnected during stream\n");
            return;
        }
        usleep(100000);
    }
    
    // Send STOP marker
    send(sock, "STOP", 5, 0);
    printf("SS: Stream complete\n");
}

// Helper function to serialize all sentences to a single string
static char* serialize_all_sentences(SentenceNode* head) {
    // Calculate total size needed
    size_t total_size = 0;
    SentenceNode* s = head;
    while (s != NULL) {
        if (s->leading_whitespace) {
            total_size += strlen(s->leading_whitespace);
        }
        
        WordNode* w = s->words;
        while (w != NULL) {
            total_size += strlen(w->word);
            if (w->trailing_whitespace) {
                total_size += strlen(w->trailing_whitespace);
            }
            w = w->next;
        }
        
        if (s->punctuation) {
            total_size += 1;
        }
        
        s = s->next;
    }
    
    // Allocate buffer
    char* result = (char*)malloc(total_size + 1);
    result[0] = '\0';
    
    // Build the string
    s = head;
    while (s != NULL) {
        if (s->leading_whitespace) {
            strcat(result, s->leading_whitespace);
        }
        
        WordNode* w = s->words;
        while (w != NULL) {
            strcat(result, w->word);
            if (w->trailing_whitespace) {
                strcat(result, w->trailing_whitespace);
            }
            w = w->next;
        }
        
        if (s->punctuation) {
            char punct[2] = {s->punctuation, '\0'};
            strcat(result, punct);
        }
        
        s = s->next;
    }
    
    return result;
}


// --- Type 2 Operations (Stubs) ---

ErrorCode handle_op_create(const char* path, const char* owner) {
    char file_path[MAX_PATH_LEN * 2];
    char info_path[MAX_PATH_LEN * 2];
    char undo_path[MAX_PATH_LEN * 2];
    
    get_full_path("file_dir", path, file_path);
    get_full_path("info_dir", path, info_path);
    get_full_path("undo_dir", path, undo_path);

    // Check if parent directory exists (if path contains directories)
    char* last_slash = strrchr(file_path, '/');
    if (last_slash != NULL && last_slash != file_path) {
        char parent_dir[MAX_PATH_LEN * 2];
        strcpy(parent_dir, file_path);
        char* slash_pos = strrchr(parent_dir, '/');
        *slash_pos = '\0';  // Remove filename
        
        // Check if parent directory exists
        struct stat st;
        if (stat(parent_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
            printf("SS: Parent directory does not exist for '%s'\n", path);
            return ERR_INVALID_PATH;
        }
    }

    // Create empty file
    FILE* f = fopen(file_path, "w");
    if (f == NULL) return ERR_INVALID_PATH;
    fclose(f);

    // Create empty undo file
    f = fopen(undo_path, "w");
    if (f == NULL) return ERR_INVALID_PATH; 
    fclose(f);

    // Create info file using update_info_file (new file, so update modified)
    update_info_file(path, owner, 1);
    
    // NOTE: Don't send info update for newly created files - NS will request it
    // if needed. Sending it here causes a race where the update arrives before
    // NS adds the file to its index.
    
    // Sync to backup partner
    sync_operation_to_partner(SS_SYNC_CREATE, path, owner);
    
    return ERR_OK;
}

ErrorCode handle_op_delete(const char* path) {
    char file_path[MAX_PATH_LEN * 2];
    char info_path[MAX_PATH_LEN * 2];
    char undo_path[MAX_PATH_LEN * 2];
    
    get_full_path("file_dir", path, file_path);
    get_full_path("info_dir", path, info_path);
    get_full_path("undo_dir", path, undo_path);
    
    // Check if file is currently in memory (being accessed)
    OpenFile* of = get_open_file(path);
    if (of != NULL) {
        // File is in memory - need to acquire exclusive write lock
        // This will block if there are active readers or writers
        printf("SS: DELETE waiting for active readers/writers to finish for '%s'...\n", path);
        pthread_rwlock_wrlock(&of->global_lock);
        printf("SS: DELETE acquired exclusive lock for '%s', proceeding with deletion.\n", path);
        
        // Check if any sentence is currently locked by a writer
        SentenceNode* s = of->sentences;
        while (s != NULL) {
            pthread_mutex_lock(&s->lock);
            if (s->lock_holder_username != NULL) {
                // Active write session on this sentence
                pthread_mutex_unlock(&s->lock);
                pthread_rwlock_unlock(&of->global_lock);
                printf("SS: Cannot delete - file has active write session by '%s'\n", s->lock_holder_username);
                return ERR_SENTENCE_LOCKED;
            }
            pthread_mutex_unlock(&s->lock);
            s = s->next;
        }
        
        // No active write sessions, proceed with deletion
        // Keep the lock held while deleting to prevent new operations
    }
    
    // Delete the main file
    if (remove(file_path) != 0) {
        if (of != NULL) {
            pthread_rwlock_unlock(&of->global_lock);
        }
        printf("SS: Failed to delete file '%s'\n", path);
        return ERR_FILE_NOT_FOUND;
    }
    
    // Delete the info file (metadata: owner, access lists, etc.)
    if (remove(info_path) == 0) {
        printf("SS: Deleted info file for '%s'\n", path);
    } else {
        printf("SS: Warning: Info file for '%s' not found or couldn't be deleted\n", path);
    }
    
    // Delete the undo file (for undo operation)
    if (remove(undo_path) == 0) {
        printf("SS: Deleted undo file for '%s'\n", path);
    } else {
        printf("SS: Warning: Undo file for '%s' not found or couldn't be deleted\n", path);
    }
    
    // Delete all checkpoint files for this file
    char checkpoint_dir[MAX_PATH_LEN * 2];
    snprintf(checkpoint_dir, sizeof(checkpoint_dir), "%s/checkpoint_dir", g_config.root_dir);
    
    printf("SS: Checking for checkpoints in '%s' for file '%s'\n", checkpoint_dir, path);
    
    DIR* dir = opendir(checkpoint_dir);
    if (dir != NULL) {
        struct dirent* entry;
        char prefix[MAX_PATH_LEN];
        snprintf(prefix, sizeof(prefix), "%s.", path);
        int prefix_len = strlen(prefix);
        int checkpoint_count = 0;
        
        printf("SS: Looking for checkpoint files matching prefix '%s'\n", prefix);
        
        while ((entry = readdir(dir)) != NULL) {
            // Skip . and ..
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            
            printf("SS: Checking file '%s' against prefix '%s'\n", entry->d_name, prefix);
            
            if (strncmp(entry->d_name, prefix, prefix_len) == 0) {
                // This is a checkpoint for the file being deleted
                char checkpoint_path[MAX_PATH_LEN * 3];
                snprintf(checkpoint_path, sizeof(checkpoint_path), "%s/%s", 
                         checkpoint_dir, entry->d_name);
                printf("SS: Attempting to delete checkpoint file '%s'\n", checkpoint_path);
                if (remove(checkpoint_path) == 0) {
                    checkpoint_count++;
                    printf("SS: Deleted checkpoint '%s'\n", entry->d_name);
                } else {
                    perror("SS: Failed to delete checkpoint");
                    printf("SS: Warning: Failed to delete checkpoint '%s'\n", entry->d_name);
                }
            }
        }
        closedir(dir);
        
        if (checkpoint_count > 0) {
            printf("SS: Deleted %d checkpoint(s) for '%s'\n", checkpoint_count, path);
        } else {
            printf("SS: No checkpoints found for '%s'\n", path);
        }
    } else {
        perror("SS: Failed to open checkpoint directory");
        printf("SS: Warning: Could not open checkpoint directory '%s'\n", checkpoint_dir);
    }
    
    // Release the lock if we acquired it
    if (of != NULL) {
        pthread_rwlock_unlock(&of->global_lock);
        printf("SS: DELETE released lock for '%s'\n", path);
        
        // CRITICAL FIX: Remove the file from in-memory list
        // This must be done AFTER releasing the lock to avoid deadlock
        // (remove_open_file acquires g_open_files_mutex)
        remove_open_file(path);
    }
    
    // Sync deletion to backup partner
    sync_operation_to_partner(SS_SYNC_DELETE, path, NULL);
    
    printf("SS: Successfully deleted all files for '%s'\n", path);
    return ERR_OK;
}

ErrorCode handle_op_undo(const char* path, const char* username) {
    char file_path[MAX_PATH_LEN * 2];
    char undo_path[MAX_PATH_LEN * 2];
    char temp_path[MAX_PATH_LEN * 2];
    
    get_full_path("file_dir", path, file_path);
    get_full_path("undo_dir", path, undo_path);
    get_full_path("file_dir", "temp_undo.tmp", temp_path);
    
    printf("SS: Processing UNDO for '%s'\n", path);
    
    // Check if undo file exists
    FILE* undo_file = fopen(undo_path, "r");
    if (undo_file == NULL) {
        printf("SS: No undo file found for '%s'\n", path);
        return ERR_FILE_NOT_FOUND;
    }
    fclose(undo_file);
    
    // Check if file is currently open (has active write sessions)
    OpenFile* of = get_open_file(path);
    if (of != NULL) {
        // File is in memory - need to acquire global write lock
        pthread_rwlock_wrlock(&of->global_lock);
        
        // Check if any sentence is locked
        SentenceNode* s = of->sentences;
        while (s != NULL) {
            pthread_mutex_lock(&s->lock);
            if (s->lock_holder_username != NULL) {
                pthread_mutex_unlock(&s->lock);
                pthread_rwlock_unlock(&of->global_lock);
                printf("SS: Cannot undo - file has active write session\n");
                return ERR_SENTENCE_LOCKED;
            }
            pthread_mutex_unlock(&s->lock);
            s = s->next;
        }
        
        // Atomic swap: save current file to temp, restore from undo, save temp to undo
        // Step 1: Copy current file to temp (this will become new undo)
        if (rename(file_path, temp_path) != 0) {
            pthread_rwlock_unlock(&of->global_lock);
            perror("SS: Failed to save current file to temp");
            return ERR_INVALID_PATH;
        }
        
        // Step 2: Restore from undo to file
        if (rename(undo_path, file_path) != 0) {
            // Rollback: restore original file
            rename(temp_path, file_path);
            pthread_rwlock_unlock(&of->global_lock);
            perror("SS: Failed to restore from undo");
            return ERR_INVALID_PATH;
        }
        
        // Step 3: Move temp to undo (new undo is the old current)
        if (rename(temp_path, undo_path) != 0) {
            pthread_rwlock_unlock(&of->global_lock);
            perror("SS: Warning - failed to save new undo");
            // Don't fail the operation - undo was successful
        }
        
        // Step 4: Reload the file into memory
        free_sentence_list(of->sentences);
        of->sentences = NULL;
        
        FILE* f = fopen(file_path, "r");
        if (f != NULL) {
            fseek(f, 0, SEEK_END);
            long file_size = ftell(f);
            fseek(f, 0, SEEK_SET);
            
            char* content = (char*)malloc(file_size + 1);
            if (content != NULL) {
                fread(content, 1, file_size, f);
                content[file_size] = '\0';
                
                SentenceNode* tail = NULL;
                of->sentences = parse_string_to_sentences(content, &tail);
                free(content);
            }
            fclose(f);
        }
        
        pthread_rwlock_unlock(&of->global_lock);
    } else {
        // File not in memory - simpler atomic swap
        // Step 1: Save current to temp
        if (rename(file_path, temp_path) != 0) {
            perror("SS: Failed to save current file");
            return ERR_INVALID_PATH;
        }
        
        // Step 2: Restore from undo
        if (rename(undo_path, file_path) != 0) {
            // Rollback
            rename(temp_path, file_path);
            perror("SS: Failed to restore from undo");
            return ERR_INVALID_PATH;
        }
        
        // Step 3: Save temp as new undo
        if (rename(temp_path, undo_path) != 0) {
            perror("SS: Warning - failed to save new undo");
        }
    }
    
    // Update info file (update modified time)
    update_info_file(path, username, 1);
    
    // Send info update to NS
    send_info_update_to_ns(path);
    
    // Sync undo operation to partner
    sync_file_to_partner(path);
    
    printf("SS: UNDO complete for '%s'\n", path);
    return ERR_OK;
}

// --- CHECKPOINT Operation ---
ErrorCode handle_op_checkpoint(const char* path, const char* checkpoint_tag) {
    char file_path[MAX_PATH_LEN * 2];
    char checkpoint_path[MAX_PATH_LEN * 2];
    
    get_full_path("file_dir", path, file_path);
    
    // Create checkpoint filename: checkpoint_dir/<path>.<tag>
    snprintf(checkpoint_path, sizeof(checkpoint_path), "%s/checkpoint_dir/%s.%s", 
             g_config.root_dir, path, checkpoint_tag);
    
    printf("SS: Creating checkpoint '%s' for '%s'\n", checkpoint_tag, path);
    
    // Ensure parent directory exists in checkpoint_dir (create if needed)
    // This handles cases where the directory structure exists in file_dir but not in checkpoint_dir yet
    if (strchr(path, '/') != NULL) {
        char build_path[MAX_PATH_LEN * 2];
        snprintf(build_path, sizeof(build_path), "%s/checkpoint_dir", g_config.root_dir);
        
        // Extract directory path (everything before the filename)
        char path_copy[MAX_PATH_LEN];
        strcpy(path_copy, path);
        char* last_slash = strrchr(path_copy, '/');
        if (last_slash != NULL) {
            *last_slash = '\0';  // Remove filename
            
            // Create each directory level
            char* token = strtok(path_copy, "/");
            while (token != NULL) {
                strcat(build_path, "/");
                strcat(build_path, token);
                mkdir(build_path, 0777);  // Create if doesn't exist, ignore error if exists
                token = strtok(NULL, "/");
            }
        }
    }
    
    // Check if file is in memory (being accessed)
    OpenFile* of = get_open_file(path);
    if (of != NULL) {
        // Acquire read lock to make a consistent copy
        pthread_rwlock_rdlock(&of->global_lock);
        
        // Copy current file to checkpoint location
        FILE* src = fopen(file_path, "r");
        if (src == NULL) {
            pthread_rwlock_unlock(&of->global_lock);
            printf("SS: Failed to open file '%s' for checkpoint\n", path);
            return ERR_FILE_NOT_FOUND;
        }
        
        FILE* dst = fopen(checkpoint_path, "w");
        if (dst == NULL) {
            fclose(src);
            pthread_rwlock_unlock(&of->global_lock);
            printf("SS: Failed to create checkpoint file\n");
            return ERR_INVALID_PATH;
        }
        
        // Copy content
        char buffer[MAX_BUFFER_LEN];
        size_t bytes;
        while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
            fwrite(buffer, 1, bytes, dst);
        }
        
        fclose(src);
        fclose(dst);
        pthread_rwlock_unlock(&of->global_lock);
    } else {
        // File not in memory, just copy from disk
        FILE* src = fopen(file_path, "r");
        if (src == NULL) {
            printf("SS: Failed to open file '%s' for checkpoint\n", path);
            return ERR_FILE_NOT_FOUND;
        }
        
        FILE* dst = fopen(checkpoint_path, "w");
        if (dst == NULL) {
            fclose(src);
            printf("SS: Failed to create checkpoint file\n");
            return ERR_INVALID_PATH;
        }
        
        // Copy content
        char buffer[MAX_BUFFER_LEN];
        size_t bytes;
        while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
            fwrite(buffer, 1, bytes, dst);
        }
        
        fclose(src);
        fclose(dst);
    }
    
    printf("SS: Checkpoint '%s' created successfully for '%s'\n", checkpoint_tag, path);
    
    // Sync checkpoint to partner
    sync_operation_to_partner(SS_SYNC_CHECKPOINT, path, checkpoint_tag);
    
    return ERR_OK;
}

// --- REVERT Operation (restore from checkpoint) ---
ErrorCode handle_op_revert(const char* path, const char* checkpoint_tag, const char* username) {
    char file_path[MAX_PATH_LEN * 2];
    char checkpoint_path[MAX_PATH_LEN * 2];
    char undo_path[MAX_PATH_LEN * 2];
    char temp_path[MAX_PATH_LEN * 2];
    
    get_full_path("file_dir", path, file_path);
    get_full_path("undo_dir", path, undo_path);
    get_full_path("file_dir", "temp_revert.tmp", temp_path);
    snprintf(checkpoint_path, sizeof(checkpoint_path), "%s/checkpoint_dir/%s.%s", 
             g_config.root_dir, path, checkpoint_tag);
    
    printf("SS: Reverting '%s' to checkpoint '%s'\n", path, checkpoint_tag);
    
    // Check if checkpoint exists
    FILE* checkpoint_file = fopen(checkpoint_path, "r");
    if (checkpoint_file == NULL) {
        printf("SS: Checkpoint '%s' not found for '%s'\n", checkpoint_tag, path);
        return ERR_FILE_NOT_FOUND;
    }
    fclose(checkpoint_file);
    
    // Check if file is currently open (has active write sessions)
    OpenFile* of = get_open_file(path);
    if (of != NULL) {
        // File is in memory - need to acquire global write lock
        pthread_rwlock_wrlock(&of->global_lock);
        
        // Check if any sentence is locked
        SentenceNode* s = of->sentences;
        while (s != NULL) {
            pthread_mutex_lock(&s->lock);
            if (s->lock_holder_username != NULL) {
                pthread_mutex_unlock(&s->lock);
                pthread_rwlock_unlock(&of->global_lock);
                printf("SS: Cannot revert - file has active write session\n");
                return ERR_SENTENCE_LOCKED;
            }
            pthread_mutex_unlock(&s->lock);
            s = s->next;
        }
        
        // Atomic replacement: save current to undo, replace with checkpoint
        // Step 1: Save current file to undo
        if (rename(file_path, undo_path) != 0) {
            pthread_rwlock_unlock(&of->global_lock);
            perror("SS: Failed to save current file to undo");
            return ERR_INVALID_PATH;
        }
        
        // Step 2: Copy checkpoint to file
        FILE* src = fopen(checkpoint_path, "r");
        FILE* dst = fopen(file_path, "w");
        if (src == NULL || dst == NULL) {
            // Rollback
            rename(undo_path, file_path);
            if (src) fclose(src);
            if (dst) fclose(dst);
            pthread_rwlock_unlock(&of->global_lock);
            printf("SS: Failed to restore from checkpoint\n");
            return ERR_INVALID_PATH;
        }
        
        char buffer[MAX_BUFFER_LEN];
        size_t bytes;
        while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
            fwrite(buffer, 1, bytes, dst);
        }
        
        fclose(src);
        fclose(dst);
        
        pthread_rwlock_unlock(&of->global_lock);
        
        // Remove from in-memory list to force reload with new content
        remove_open_file(path);
    } else {
        // File not in memory, simpler operation
        // Save current to undo
        if (rename(file_path, undo_path) != 0) {
            perror("SS: Failed to save current file to undo");
            return ERR_INVALID_PATH;
        }
        
        // Copy checkpoint to file
        FILE* src = fopen(checkpoint_path, "r");
        FILE* dst = fopen(file_path, "w");
        if (src == NULL || dst == NULL) {
            // Rollback
            rename(undo_path, file_path);
            if (src) fclose(src);
            if (dst) fclose(dst);
            printf("SS: Failed to restore from checkpoint\n");
            return ERR_INVALID_PATH;
        }
        
        char buffer[MAX_BUFFER_LEN];
        size_t bytes;
        while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
            fwrite(buffer, 1, bytes, dst);
        }
        
        fclose(src);
        fclose(dst);
    }
    
    // Update info file (update modified time)
    update_info_file(path, username, 1);
    
    // Send info update to NS
    send_info_update_to_ns(path);
    
    // Sync reverted file to partner
    sync_file_to_partner(path);
    
    printf("SS: REVERT complete for '%s' to checkpoint '%s'\n", path, checkpoint_tag);
    return ERR_OK;
}

// --- LIST CHECKPOINTS Operation ---
void handle_op_listcheckpoints(int sock, const char* path) {
    // Build full checkpoint path including directory structure
    char checkpoint_base_path[MAX_PATH_LEN * 3];  // Increased buffer size
    
    // For nested files like "dir/file.txt", we need to check in "checkpoint_dir/dir/"
    // Extract directory part if it exists
    char dir_part[MAX_PATH_LEN] = "";
    char file_part[MAX_PATH_LEN];
    strcpy(file_part, path);
    
    char* last_slash = strrchr(file_part, '/');
    if (last_slash != NULL) {
        *last_slash = '\0';  // Split into dir and file
        strcpy(dir_part, file_part);
        strcpy(file_part, last_slash + 1);
        
        // checkpoint dir is: root/checkpoint_dir/dir_part
        snprintf(checkpoint_base_path, sizeof(checkpoint_base_path), 
                 "%s/checkpoint_dir/%s", g_config.root_dir, dir_part);
    } else {
        // No directory, just use checkpoint_dir root
        snprintf(checkpoint_base_path, sizeof(checkpoint_base_path), 
                 "%s/checkpoint_dir", g_config.root_dir);
    }
    
    DIR* dir = opendir(checkpoint_base_path);
    if (dir == NULL) {
        char* err_msg = "Error: Failed to open checkpoint directory";
        send(sock, err_msg, strlen(err_msg), 0);
        send(sock, "STOP", 5, 0);
        return;
    }
    
    // Build prefix to match: filename.
    // For "dir/file.txt", we search for "file.txt." in checkpoint_dir/dir/
    char prefix[MAX_PATH_LEN + 1];
    snprintf(prefix, sizeof(prefix), "%s.", file_part);
    size_t prefix_len = strlen(prefix);
    
    struct dirent* entry;
    int found_count = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        // Check if filename starts with our prefix
        if (strncmp(entry->d_name, prefix, prefix_len) == 0) {
            // Extract tag (everything after the prefix)
            const char* tag = entry->d_name + prefix_len;
            char response[MAX_BUFFER_LEN];
            memset(response, 0, sizeof(response));
            snprintf(response, sizeof(response), "- %s\n", tag);
            send(sock, response, strlen(response), 0);
            found_count++;
        }
    }
    
    closedir(dir);
    
    if (found_count == 0) {
        char* msg = "No checkpoints found for this file.\n";
        send(sock, msg, strlen(msg), 0);
    }
    
    send(sock, "STOP", 5, 0);
}

// --- CREATEFOLDER Operation Handler ---
// Creates a directory in file_dir, info_dir, undo_dir, and checkpoint_dir
// NOTE: Only creates ONE level - parent directories must already exist
void handle_op_createfolder(const char* path, int sock) {
    char dir_paths[4][MAX_PATH_LEN * 2];
    const char* dir_types[] = {"file_dir", "info_dir", "undo_dir", "checkpoint_dir"};
    
    ServerResponse res;
    memset(&res, 0, sizeof(ServerResponse));
    
    // Build all directory paths
    for (int i = 0; i < 4; i++) {
        get_full_path(dir_types[i], path, dir_paths[i]);
    }
    
    // Check if parent directory exists (if path contains /)
    if (strchr(path, '/') != NULL) {
        // Extract parent path
        char parent_path[MAX_PATH_LEN];
        strcpy(parent_path, path);
        char* last_slash = strrchr(parent_path, '/');
        if (last_slash != NULL) {
            *last_slash = '\0';  // Remove the new directory name, leaving parent path
            
            // Check if parent exists in file_dir
            char parent_full_path[MAX_PATH_LEN * 2];
            get_full_path("file_dir", parent_path, parent_full_path);
            
            struct stat st;
            if (stat(parent_full_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
                res.status = ERR_INVALID_PATH;
                sprintf(res.message, "Error: Parent directory '%s' does not exist. Create it first.", parent_path);
                SEND_SERVER_RESPONSE(sock, &res);
                return;
            }
        }
    }
    
    // Create the directory in all 4 locations (only the final level)
    for (int i = 0; i < 4; i++) {
        if (mkdir(dir_paths[i], 0777) != 0 && errno != EEXIST) {
            res.status = ERR_INVALID_PATH;
            sprintf(res.message, "Error: Failed to create directory '%s' in %s: %s", 
                    path, dir_types[i], strerror(errno));
            SEND_SERVER_RESPONSE(sock, &res);
            return;
        }
    }
    
    res.status = ERR_OK;
    sprintf(res.message, "Folder '%s' created successfully.", path);
    printf("SS: Created folder '%s' in all directories.\n", path);
    SEND_SERVER_RESPONSE(sock, &res);
    
    // Sync folder creation to partner
    sync_operation_to_partner(SS_SYNC_CREATEFOLDER, path, NULL);
}

// --- MOVE Operation Handler ---
// Moves/renames a file in file_dir, info_dir, undo_dir, and checkpoints
void handle_op_move(const char* old_path, const char* new_path, int sock) {
    char old_paths[4][MAX_PATH_LEN * 2];
    char new_paths[4][MAX_PATH_LEN * 2];
    const char* dir_types[] = {"file_dir", "info_dir", "undo_dir", "checkpoint_dir"};
    
    ServerResponse res;
    memset(&res, 0, sizeof(ServerResponse));
    
    // Build all paths
    for (int i = 0; i < 4; i++) {
        get_full_path(dir_types[i], old_path, old_paths[i]);
        get_full_path(dir_types[i], new_path, new_paths[i]);
    }
    
    // First, ensure parent directories exist for new path
    for (int i = 0; i < 4; i++) {
        char parent_dir[MAX_PATH_LEN * 2];
        strcpy(parent_dir, new_paths[i]);
        char* last_slash = strrchr(parent_dir, '/');
        if (last_slash != NULL) {
            *last_slash = '\0';
            
            // Create parent directory structure if needed (recursive mkdir -p)
            char build_path[MAX_PATH_LEN * 2];
            snprintf(build_path, sizeof(build_path), "%s/%s", g_config.root_dir, dir_types[i]);
            
            // Extract relative path from new_path
            const char* slash = strchr(new_path, '/');
            if (slash != NULL) {
                char rel_parent[MAX_PATH_LEN];
                strncpy(rel_parent, new_path, slash - new_path);
                rel_parent[slash - new_path] = '\0';
                
                // Create each directory level
                char temp_rel[MAX_PATH_LEN];
                strcpy(temp_rel, new_path);
                
                char* last_component = strrchr(temp_rel, '/');
                if (last_component != NULL) {
                    *last_component = '\0';  // Remove filename
                    
                    char* token = strtok(temp_rel, "/");
                    while (token != NULL) {
                        strcat(build_path, "/");
                        strcat(build_path, token);
                        mkdir(build_path, 0777);  // Ignore errors if exists
                        token = strtok(NULL, "/");
                    }
                }
            }
        }
    }
    
    // Move files in file_dir, info_dir, and undo_dir
    for (int i = 0; i < 3; i++) {
        if (access(old_paths[i], F_OK) == 0) {
            if (rename(old_paths[i], new_paths[i]) != 0) {
                res.status = ERR_INVALID_PATH;
                sprintf(res.message, "Error: Failed to move file in %s: %s", 
                        dir_types[i], strerror(errno));
                SEND_SERVER_RESPONSE(sock, &res);
                return;
            }
        }
    }
    
    // Handle checkpoint files (they have tags: filename.tag)
    // We need to find all checkpoint files matching old_path and rename them
    char checkpoint_dir[MAX_PATH_LEN * 2];
    get_full_path("checkpoint_dir", "", checkpoint_dir);
    
    DIR* dir = opendir(checkpoint_dir);
    if (dir != NULL) {
        // Extract just the filename from old_path and new_path
        const char* old_filename = strrchr(old_path, '/');
        old_filename = old_filename ? old_filename + 1 : old_path;
        
        const char* new_filename = strrchr(new_path, '/');
        new_filename = new_filename ? new_filename + 1 : new_path;
        
        char old_prefix[MAX_PATH_LEN];
        snprintf(old_prefix, sizeof(old_prefix), "%s.", old_filename);
        size_t prefix_len = strlen(old_prefix);
        
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strncmp(entry->d_name, old_prefix, prefix_len) == 0) {
                // This is a checkpoint for the old file
                const char* tag = entry->d_name + prefix_len;
                
                char old_ckpt[MAX_PATH_LEN * 3];
                char new_ckpt[MAX_PATH_LEN * 3];
                snprintf(old_ckpt, sizeof(old_ckpt), "%s/%s", checkpoint_dir, entry->d_name);
                snprintf(new_ckpt, sizeof(new_ckpt), "%s/%s.%s", checkpoint_dir, new_filename, tag);
                
                rename(old_ckpt, new_ckpt);
            }
        }
        closedir(dir);
    }
    
    res.status = ERR_OK;
    sprintf(res.message, "File moved from '%s' to '%s'.", old_path, new_path);
    printf("SS: Moved file '%s' to '%s'.\n", old_path, new_path);
    SEND_SERVER_RESPONSE(sock, &res);
    
    // Sync move operation to partner
    sync_operation_to_partner(SS_SYNC_MOVE, old_path, new_path);
}
