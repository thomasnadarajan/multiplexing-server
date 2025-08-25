#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include "tp.h"
#include "message_handling.h"
#include "compression.h"
#include "multiplexlist.h"
#include "memory_pool.h"
#include "byteswap_compat.h"

memory_pool *global_pool = NULL;

#define QUEUE_SIZE 1024

// Forward declarations
static void * thread_worker_optimized(void * args);
static void client_handling_optimized(int * clfd, void * input);
extern message * get_description_optimized(int sockfd, m_node ** compress);
extern void echo_optimized(int sockfd, message * input, m_node ** compressor);
extern void directory_send_optimized(int sockfd, message ** input, char * directory, m_node ** compressor);
extern void file_size_response_optimized(int sockfd, message ** input, char * directory, m_node ** compressor);
extern void parent_send_optimized(int sockfd, int compressed, char * directory, file_request ** input, m_node ** compressor);

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
    pthread_t threads[20];
    int shut;
    int serversock;
    struct {
        m_node *dict;
        char *directory;
    } data;
    List *requests_list;
} thread_pool_optimized;

static int cq_enqueue(circular_queue *q, int *item) {
    if (q->count >= QUEUE_SIZE) {
        return -1;
    }
    
    q->items[q->tail] = item;
    q->tail = (q->tail + 1) % QUEUE_SIZE;
    q->count++;
    return 0;
}

static int* cq_dequeue(circular_queue *q) {
    if (q->count == 0) {
        return NULL;
    }
    
    int *item = q->items[q->head];
    q->head = (q->head + 1) % QUEUE_SIZE;
    q->count--;
    return item;
}

thread_pool * tp_create(char * config_name, struct sockaddr_in * sock) {
    thread_pool_optimized * tp = calloc(1, sizeof(thread_pool_optimized));
    if (!tp) return NULL;
    
    global_pool = mp_create();
    
    pthread_cond_init(&tp->cond_var, NULL);
    pthread_mutex_init(&tp->mutex, NULL);
    
    tp->queue.head = 0;
    tp->queue.tail = 0;
    tp->queue.count = 0;
    tp->shut = 0;
    
    create_map(&(tp->data.dict));
    
    for (int i = 0; i < 20; i++) {
        if (pthread_create(&(tp->threads[i]), NULL, thread_worker_optimized, tp) != 0) {
            perror("pthread_create failed");
            free(tp);
            return NULL;
        }
    }
    
    get_config(config_name, sock, &(tp->data.directory));
    tp->requests_list = create();
    
    return (thread_pool *)tp;
}

void enqueue(int * clfd, thread_pool * input) {
    thread_pool_optimized *tp = (thread_pool_optimized *)input;
    
    pthread_mutex_lock(&tp->mutex);
    
    if (cq_enqueue(&tp->queue, clfd) == -1) {
        pthread_mutex_unlock(&tp->mutex);
        close(*clfd);
        free(clfd);
        return;
    }
    
    pthread_cond_signal(&tp->cond_var);
    pthread_mutex_unlock(&tp->mutex);
}

static void * thread_worker_optimized(void * args) {
    thread_pool_optimized * tp = (thread_pool_optimized *) args;
    
    while (1) {
        pthread_mutex_lock(&tp->mutex);
        
        int * clfd;
        while ((clfd = cq_dequeue(&tp->queue)) == NULL && tp->shut == 0) {
            pthread_cond_wait(&tp->cond_var, &tp->mutex);
        }
        
        if (tp->shut == 1 && clfd == NULL) {
            pthread_mutex_unlock(&tp->mutex);
            break;
        }
        
        pthread_mutex_unlock(&tp->mutex);
        
        if (clfd != NULL) {
            client_handling_optimized(clfd, tp);
        }
    }
    
    return NULL;
}

static void client_handling_optimized(int * clfd, void * input) {
    thread_pool_optimized *tp = (thread_pool_optimized *)input;
    int sockfd = *clfd;
    
    int tcp_nodelay = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &tcp_nodelay, sizeof(int));
    
    int sndbuf = 65536;
    int rcvbuf = 65536;
    setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(int));
    setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(int));
    
    while (1) {
        if (tp->shut == 1) {
            break;
        }
        
        message * msg = get_description_optimized(sockfd, &(tp->data.dict));
        
        if (msg == NULL) {
            close(sockfd);
            if (global_pool) mp_free(global_pool, clfd, sizeof(int));
            else free(clfd);
            return;
        }
        
        if (msg->main.type != 0 && msg->main.type != 2 && 
            msg->main.type != 4 && msg->main.type != 6 && msg->main.type != 8) {
            error_send(sockfd);
            close(sockfd);
            if (global_pool) mp_free(global_pool, clfd, sizeof(int));
            else free(clfd);
            free(msg->buffer);
            if (global_pool) mp_free(global_pool, msg, sizeof(message));
            else free(msg);
            return;
        }
        
        switch (msg->main.type) {
            case 0x0:
                echo_optimized(sockfd, msg, &(tp->data.dict));
                break;
                
            case 0x2:
                directory_send_optimized(sockfd, &msg, tp->data.directory, &(tp->data.dict));
                break;
                
            case 0x4:
                file_size_response_optimized(sockfd, &msg, tp->data.directory, &(tp->data.dict));
                break;
                
            case 0x6: {
                file_request * req = dissect_file_request(msg);
                file_request * curr = find(&(tp->requests_list), req);
                
                if (curr) {
                    if (strcmp((char*)req->file_name, (char*)curr->file_name) != 0 || 
                        req->length != curr->length || 
                        req->offset != curr->offset) {
                        error_send(sockfd);
                        close(sockfd);
                        free(req->file_name);
                        free(req);
                        free(msg->buffer);
                        if (global_pool) mp_free(global_pool, msg, sizeof(message));
                        else free(msg);
                        if (global_pool) mp_free(global_pool, clfd, sizeof(int));
                        else free(clfd);
                        return;
                    }
                    
                    pthread_mutex_lock(&curr->node_lock);
                    curr->num_connect++;
                    pthread_mutex_unlock(&curr->node_lock);
                    
                    child_send(sockfd, msg->main.requires_compression, 
                              tp->data.directory, &curr, &(tp->data.dict));
                    
                    close(sockfd);
                    free(req->file_name);
                    free(req);
                    free(msg->buffer);
                    if (global_pool) mp_free(global_pool, msg, sizeof(message));
                    else free(msg);
                    if (global_pool) mp_free(global_pool, clfd, sizeof(int));
                    else free(clfd);
                    return;
                } else {
                    pipe(req->pipefd);
                    pthread_mutex_init(&req->node_lock, NULL);
                    req->num_connect = 0;
                    add(&(tp->requests_list), req);
                    
                    parent_send_optimized(sockfd, msg->main.requires_compression, 
                                        tp->data.directory, &req, &(tp->data.dict));
                    
                    remove_node(&(tp->requests_list), req);
                }
                break;
            }
                
            case 0x8:
                close(sockfd);
                if (global_pool) mp_free(global_pool, clfd, sizeof(int));
                else free(clfd);
                if (global_pool) mp_free(global_pool, msg, sizeof(message));
                else free(msg);
                
                pthread_mutex_lock(&tp->mutex);
                tp->shut = 1;
                pthread_cond_broadcast(&tp->cond_var);
                pthread_mutex_unlock(&tp->mutex);
                
                int *f;
                while ((f = cq_dequeue(&tp->queue)) != NULL) {
                    close(*f);
                    if (global_pool) mp_free(global_pool, f, sizeof(int));
                    else free(f);
                }
                
                for (int i = 0; i < 256; i++) {
                    free(tp->data.dict[i].code);
                }
                free(tp->data.dict);
                free(tp->data.directory);
                free(tp->requests_list);
                
                if (global_pool) {
                    mp_destroy(global_pool);
                    global_pool = NULL;
                }
                
                shutdown(tp->serversock, SHUT_RDWR);
                return;
        }
        
        free(msg->buffer);
        if (global_pool) mp_free(global_pool, msg, sizeof(message));
        else free(msg);
    }
}