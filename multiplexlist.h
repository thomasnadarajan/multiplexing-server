#ifndef MULTI_H
#define MULTI_H
#include <pthread.h>
typedef struct file_request {
    uint32_t session_id;
    uint64_t offset;
    uint64_t length;
    unsigned char * file_name;
    int num_connect;
    pthread_mutex_t node_lock;
    struct file_request * next;
    int pipefd[2];
} file_request;

typedef struct List {
    file_request * head;
    file_request * tail;
    pthread_mutex_t lock;
} List;
List * create();
void add(List ** list, file_request * input);
void remove_node(List ** list, file_request * input);
file_request * find(List ** list, file_request * input);
#endif