#ifndef TP_H
#define TP_H
#include <pthread.h>
#include <sys/types.h>
#include <stdint.h>
#include "multiplexlist.h"
#include <netinet/in.h>
typedef struct Node {
    int * clfd;
    struct Node * next;
} Node;
typedef struct map_node {
    unsigned char byte;
    uint8_t code_l;
    uint8_t * code;
} m_node;
typedef struct lifetime_data {
    char * directory;
    m_node * dict;
} lifetime_data;
typedef struct thread_pool {
    int serversock;
    pthread_cond_t cond_var;
    pthread_mutex_t mutex;
    pthread_t threads[100];
    int shut;
    lifetime_data data;
    List * requests_list;
    Node * head;
    Node * tail;
} thread_pool;

thread_pool * tp_create(char * config_name , struct sockaddr_in *sock);
int * dequeue(thread_pool *input);
void enqueue(int * clfd, thread_pool * input);
void * thread_worker(void * args);
void shutter();
void client_handling(int * clfd, thread_pool * input);
#endif