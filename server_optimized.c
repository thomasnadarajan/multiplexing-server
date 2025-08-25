#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <netinet/tcp.h>
#include "message_handling.h"
#include "compression.h"
#include "multiplexlist.h"
#include "memory_pool.h"
#include "byteswap_compat.h"

// Global memory pool for optimized allocations
memory_pool *global_pool = NULL;

#define QUEUE_SIZE 1024
#define NUM_THREADS 20

// Optimized circular queue for thread pool
typedef struct {
    int *items[QUEUE_SIZE];
    int head;
    int tail;
    int count;
} circular_queue;

typedef struct {
    circular_queue queue;
    pthread_mutex_t mutex;
    pthread_cond_t cond_var;
    pthread_t threads[NUM_THREADS];
    int shutdown;
    int serversock;
    struct {
        m_node *dict;
        char *directory;
    } data;
    List *requests_list;
} thread_pool_opt;

// Circular queue operations
static int cq_enqueue(circular_queue *q, int *item) {
    if (q->count >= QUEUE_SIZE) return -1;
    q->items[q->tail] = item;
    q->tail = (q->tail + 1) % QUEUE_SIZE;
    q->count++;
    return 0;
}

static int* cq_dequeue(circular_queue *q) {
    if (q->count == 0) return NULL;
    int *item = q->items[q->head];
    q->head = (q->head + 1) % QUEUE_SIZE;
    q->count--;
    return item;
}

// Worker thread function
void* thread_worker_opt(void *arg) {
    thread_pool_opt *tp = (thread_pool_opt *)arg;
    
    while (1) {
        pthread_mutex_lock(&tp->mutex);
        
        int *clfd;
        while ((clfd = cq_dequeue(&tp->queue)) == NULL && !tp->shutdown) {
            pthread_cond_wait(&tp->cond_var, &tp->mutex);
        }
        
        if (tp->shutdown && clfd == NULL) {
            pthread_mutex_unlock(&tp->mutex);
            break;
        }
        
        pthread_mutex_unlock(&tp->mutex);
        
        if (clfd != NULL) {
            // Set socket options for better performance
            int tcp_nodelay = 1;
            setsockopt(*clfd, IPPROTO_TCP, TCP_NODELAY, &tcp_nodelay, sizeof(int));
            
            // Handle client with optimized message processing
            while (1) {
                if (tp->shutdown) {
                    close(*clfd);
                    free(clfd);
                    break;
                }
                
                message *msg = get_description(*clfd, &(tp->data.dict));
                
                if (msg == NULL) {
                    close(*clfd);
                    free(clfd);
                    break;
                }
                
                // Process message based on type
                switch (msg->main.type) {
                    case 0x0: // Echo
                        echo(*clfd, msg, &(tp->data.dict));
                        break;
                    case 0x2: // Directory
                        directory_send(*clfd, &msg, tp->data.directory, &(tp->data.dict));
                        break;
                    case 0x4: // File size
                        file_size_response(*clfd, &msg, tp->data.directory, &(tp->data.dict));
                        break;
                    case 0x6: { // File retrieval
                        file_request *req = dissect_file_request(msg);
                        file_request *curr = find(&(tp->requests_list), req);
                        
                        if (curr) {
                            pthread_mutex_lock(&curr->node_lock);
                            curr->num_connect++;
                            pthread_mutex_unlock(&curr->node_lock);
                            child_send(*clfd, msg->main.requires_compression, 
                                     tp->data.directory, &curr, &(tp->data.dict));
                        } else {
                            pipe(req->pipefd);
                            pthread_mutex_init(&req->node_lock, NULL);
                            req->num_connect = 0;
                            add(&(tp->requests_list), req);
                            parent_send(*clfd, msg->main.requires_compression,
                                      tp->data.directory, &req, &(tp->data.dict));
                            remove_node(&(tp->requests_list), req);
                        }
                        break;
                    }
                    case 0x8: // Shutdown
                        tp->shutdown = 1;
                        pthread_cond_broadcast(&tp->cond_var);
                        close(*clfd);
                        free(clfd);
                        free(msg->buffer);
                        free(msg);
                        shutdown(tp->serversock, SHUT_RDWR);
                        return NULL;
                    default:
                        error_send(*clfd);
                        close(*clfd);
                        free(clfd);
                        free(msg->buffer);
                        free(msg);
                        return NULL;
                }
                
                free(msg->buffer);
                free(msg);
            }
        }
    }
    
    return NULL;
}

// Create optimized thread pool
thread_pool_opt* tp_create_opt(char *config_name, struct sockaddr_in *sock) {
    thread_pool_opt *tp = calloc(1, sizeof(thread_pool_opt));
    if (!tp) return NULL;
    
    // Initialize memory pool
    global_pool = mp_create();
    
    pthread_cond_init(&tp->cond_var, NULL);
    pthread_mutex_init(&tp->mutex, NULL);
    
    tp->queue.head = 0;
    tp->queue.tail = 0;
    tp->queue.count = 0;
    tp->shutdown = 0;
    
    create_map(&(tp->data.dict));
    get_config(config_name, sock, &(tp->data.directory));
    tp->requests_list = create();
    
    // Start worker threads
    for (int i = 0; i < NUM_THREADS; i++) {
        if (pthread_create(&(tp->threads[i]), NULL, thread_worker_opt, tp) != 0) {
            perror("pthread_create failed");
            free(tp);
            return NULL;
        }
    }
    
    return tp;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <config_file>\n", argv[0]);
        return 1;
    }
    
    // Setup server address
    struct sockaddr_in server_addr, client;
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("Failed to create socket");
        return 1;
    }
    
    // Allow socket reuse
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Set socket buffers for better performance
    int sndbuf = 65536;
    int rcvbuf = 65536;
    setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(int));
    setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(int));
    
    server_addr.sin_family = AF_INET;
    
    // Create optimized thread pool
    thread_pool_opt *tp = tp_create_opt(argv[1], &server_addr);
    if (!tp) {
        close(sockfd);
        return 1;
    }
    
    // Bind socket
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr_in)) < 0) {
        perror("Bind failed");
        close(sockfd);
        free(tp);
        return 1;
    }
    
    tp->serversock = sockfd;
    
    // Listen with larger backlog
    listen(sockfd, 1024);
    
    printf("Optimized server running on port %d\n", ntohs(server_addr.sin_port));
    
    socklen_t addr_size = sizeof(struct sockaddr_in);
    
    // Main accept loop
    while (1) {
        int clfd = accept(sockfd, (struct sockaddr *)&client, &addr_size);
        
        if (clfd == -1) {
            if (tp->shutdown) break;
            continue;
        }
        
        int *cl = malloc(sizeof(int));
        if (!cl) {
            close(clfd);
            continue;
        }
        
        *cl = clfd;
        
        pthread_mutex_lock(&tp->mutex);
        if (cq_enqueue(&tp->queue, cl) == -1) {
            pthread_mutex_unlock(&tp->mutex);
            close(clfd);
            free(cl);
            continue;
        }
        pthread_cond_signal(&tp->cond_var);
        pthread_mutex_unlock(&tp->mutex);
        
        if (tp->shutdown) break;
    }
    
    // Cleanup
    close(sockfd);
    
    // Wait for threads
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(tp->threads[i], NULL);
    }
    
    // Free resources
    for (int i = 0; i < 256; i++) {
        free(tp->data.dict[i].code);
    }
    free(tp->data.dict);
    free(tp->data.directory);
    free(tp->requests_list);
    
    if (global_pool) {
        mp_destroy(global_pool);
    }
    
    pthread_mutex_destroy(&tp->mutex);
    pthread_cond_destroy(&tp->cond_var);
    free(tp);
    
    return 0;
}