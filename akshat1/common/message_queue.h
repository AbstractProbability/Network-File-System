#ifndef MESSAGE_QUEUE_H
#define MESSAGE_QUEUE_H

#include "message.h"
#include <pthread.h>

#define QUEUE_INITIAL_CAPACITY 100
#define QUEUE_MAX_CAPACITY 10000

// Thread-safe circular message queue
typedef struct {
    Message* messages;
    int capacity;
    int size;
    int head;
    int tail;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    int shutdown;
} MessageQueue;

// Initialize queue
int queue_init(MessageQueue* queue, int initial_capacity);

// Cleanup queue
void queue_destroy(MessageQueue* queue);

// Enqueue message (blocking if full)
int queue_enqueue(MessageQueue* queue, const Message* msg);

// Enqueue with timeout (ms)
int queue_enqueue_timeout(MessageQueue* queue, const Message* msg, int timeout_ms);

// Dequeue message (blocking if empty)
int queue_dequeue(MessageQueue* queue, Message* msg);

// Dequeue with timeout (ms), returns 0 on success, -1 on error, -2 on timeout
int queue_dequeue_timeout(MessageQueue* queue, Message* msg, int timeout_ms);

// Try dequeue (non-blocking), returns 0 on success, -1 if empty
int queue_try_dequeue(MessageQueue* queue, Message* msg);

// Get queue size
int queue_size(MessageQueue* queue);

// Check if queue is empty
int queue_is_empty(MessageQueue* queue);

// Check if queue is full
int queue_is_full(MessageQueue* queue);

// Signal shutdown (unblock waiting threads)
void queue_shutdown(MessageQueue* queue);

#endif // MESSAGE_QUEUE_H
