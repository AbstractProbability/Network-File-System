#include "message_queue.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>

int queue_init(MessageQueue* queue, int initial_capacity) {
    if (!queue || initial_capacity <= 0) {
        return -1;
    }
    
    if (initial_capacity > QUEUE_MAX_CAPACITY) {
        initial_capacity = QUEUE_MAX_CAPACITY;
    }
    
    queue->messages = (Message*)malloc(initial_capacity * sizeof(Message));
    if (!queue->messages) {
        return -1;
    }
    
    queue->capacity = initial_capacity;
    queue->size = 0;
    queue->head = 0;
    queue->tail = 0;
    queue->shutdown = 0;
    
    if (pthread_mutex_init(&queue->lock, NULL) != 0) {
        free(queue->messages);
        return -1;
    }
    
    if (pthread_cond_init(&queue->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&queue->lock);
        free(queue->messages);
        return -1;
    }
    
    if (pthread_cond_init(&queue->not_full, NULL) != 0) {
        pthread_cond_destroy(&queue->not_empty);
        pthread_mutex_destroy(&queue->lock);
        free(queue->messages);
        return -1;
    }
    
    return 0;
}

void queue_destroy(MessageQueue* queue) {
    if (!queue) return;
    
    pthread_mutex_lock(&queue->lock);
    queue->shutdown = 1;
    pthread_cond_broadcast(&queue->not_empty);
    pthread_cond_broadcast(&queue->not_full);
    pthread_mutex_unlock(&queue->lock);
    
    pthread_cond_destroy(&queue->not_empty);
    pthread_cond_destroy(&queue->not_full);
    pthread_mutex_destroy(&queue->lock);
    
    if (queue->messages) {
        free(queue->messages);
        queue->messages = NULL;
    }
}

int queue_enqueue(MessageQueue* queue, const Message* msg) {
    if (!queue || !msg) return -1;
    
    pthread_mutex_lock(&queue->lock);
    
    // Wait while queue is full
    while (queue->size >= queue->capacity && !queue->shutdown) {
        pthread_cond_wait(&queue->not_full, &queue->lock);
    }
    
    if (queue->shutdown) {
        pthread_mutex_unlock(&queue->lock);
        return -1;
    }
    
    // Copy message to queue
    memcpy(&queue->messages[queue->tail], msg, sizeof(Message));
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->size++;
    
    // Signal waiting dequeue threads
    pthread_cond_signal(&queue->not_empty);
    
    pthread_mutex_unlock(&queue->lock);
    return 0;
}

int queue_enqueue_timeout(MessageQueue* queue, const Message* msg, int timeout_ms) {
    if (!queue || !msg) return -1;
    
    struct timespec ts;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    ts.tv_sec = tv.tv_sec + (timeout_ms / 1000);
    ts.tv_nsec = (tv.tv_usec * 1000) + ((timeout_ms % 1000) * 1000000);
    
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }
    
    pthread_mutex_lock(&queue->lock);
    
    // Wait while queue is full
    while (queue->size >= queue->capacity && !queue->shutdown) {
        int result = pthread_cond_timedwait(&queue->not_full, &queue->lock, &ts);
        if (result == ETIMEDOUT) {
            pthread_mutex_unlock(&queue->lock);
            return -2;  // Timeout
        }
    }
    
    if (queue->shutdown) {
        pthread_mutex_unlock(&queue->lock);
        return -1;
    }
    
    // Copy message to queue
    memcpy(&queue->messages[queue->tail], msg, sizeof(Message));
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->size++;
    
    // Signal waiting dequeue threads
    pthread_cond_signal(&queue->not_empty);
    
    pthread_mutex_unlock(&queue->lock);
    return 0;
}

int queue_dequeue(MessageQueue* queue, Message* msg) {
    if (!queue || !msg) return -1;
    
    pthread_mutex_lock(&queue->lock);
    
    // Wait while queue is empty
    while (queue->size == 0 && !queue->shutdown) {
        pthread_cond_wait(&queue->not_empty, &queue->lock);
    }
    
    if (queue->shutdown && queue->size == 0) {
        pthread_mutex_unlock(&queue->lock);
        return -1;
    }
    
    // Copy message from queue
    memcpy(msg, &queue->messages[queue->head], sizeof(Message));
    queue->head = (queue->head + 1) % queue->capacity;
    queue->size--;
    
    // Signal waiting enqueue threads
    pthread_cond_signal(&queue->not_full);
    
    pthread_mutex_unlock(&queue->lock);
    return 0;
}

int queue_dequeue_timeout(MessageQueue* queue, Message* msg, int timeout_ms) {
    if (!queue || !msg) return -1;
    
    struct timespec ts;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    ts.tv_sec = tv.tv_sec + (timeout_ms / 1000);
    ts.tv_nsec = (tv.tv_usec * 1000) + ((timeout_ms % 1000) * 1000000);
    
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }
    
    pthread_mutex_lock(&queue->lock);
    
    // Wait while queue is empty
    while (queue->size == 0 && !queue->shutdown) {
        int result = pthread_cond_timedwait(&queue->not_empty, &queue->lock, &ts);
        if (result == ETIMEDOUT) {
            pthread_mutex_unlock(&queue->lock);
            return -2;  // Timeout
        }
    }
    
    if (queue->shutdown && queue->size == 0) {
        pthread_mutex_unlock(&queue->lock);
        return -1;
    }
    
    // Copy message from queue
    memcpy(msg, &queue->messages[queue->head], sizeof(Message));
    queue->head = (queue->head + 1) % queue->capacity;
    queue->size--;
    
    // Signal waiting enqueue threads
    pthread_cond_signal(&queue->not_full);
    
    pthread_mutex_unlock(&queue->lock);
    return 0;
}

int queue_try_dequeue(MessageQueue* queue, Message* msg) {
    if (!queue || !msg) return -1;
    
    pthread_mutex_lock(&queue->lock);
    
    if (queue->size == 0) {
        pthread_mutex_unlock(&queue->lock);
        return -1;  // Empty
    }
    
    // Copy message from queue
    memcpy(msg, &queue->messages[queue->head], sizeof(Message));
    queue->head = (queue->head + 1) % queue->capacity;
    queue->size--;
    
    // Signal waiting enqueue threads
    pthread_cond_signal(&queue->not_full);
    
    pthread_mutex_unlock(&queue->lock);
    return 0;
}

int queue_size(MessageQueue* queue) {
    if (!queue) return 0;
    
    pthread_mutex_lock(&queue->lock);
    int size = queue->size;
    pthread_mutex_unlock(&queue->lock);
    
    return size;
}

int queue_is_empty(MessageQueue* queue) {
    return queue_size(queue) == 0;
}

int queue_is_full(MessageQueue* queue) {
    if (!queue) return 1;
    
    pthread_mutex_lock(&queue->lock);
    int full = (queue->size >= queue->capacity);
    pthread_mutex_unlock(&queue->lock);
    
    return full;
}

void queue_shutdown(MessageQueue* queue) {
    if (!queue) return;
    
    pthread_mutex_lock(&queue->lock);
    queue->shutdown = 1;
    pthread_cond_broadcast(&queue->not_empty);
    pthread_cond_broadcast(&queue->not_full);
    pthread_mutex_unlock(&queue->lock);
}
