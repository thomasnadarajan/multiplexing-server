#include <pthread.h>
#include <string.h>
#include "message_handling.h"
#include "multiplexlist.h"
/* 
    Create a list (linked list) storing file_requests.
*/
List * create() {
    
    List * list = malloc(sizeof(List));
    list->head = NULL;
    list->tail = NULL;
    pthread_mutex_init(&list->lock, NULL);
    return list;
}
/*
    Add node to the linked list.
*/
void add(List ** list, file_request * input) {
    // Ensure thread safety using a list lock when adding file_requests.
    pthread_mutex_lock(&(*list)->lock);
    if ((*list)->head == NULL) {
        (*list)->head = input;
        (*list)->tail = input;
    }
    else {
        (*list)->tail->next = input; 
        (*list)->tail = (*list)->tail->next;
    }
    pthread_mutex_unlock(&(*list)->lock);
}
/*
    Remove node from linked list.
*/
void remove_node(List ** list, file_request * input) {
    pthread_mutex_lock(&(*list)->lock);
    if (input == (*list)->head && input == (*list)->tail) {
        free(input->file_name);
        free(input);
        (*list)->head = NULL;
        (*list)->tail = NULL;
        pthread_mutex_unlock(&(*list)->lock);
        return;
    }
    file_request * node = (*list)->head;
    if ((*list)->head == NULL) {
        pthread_mutex_unlock(&(*list)->lock);
        return;
    }
    while (node->next != input) {
        node = node->next;
    }
    node->next = input->next;
    if ((*list)->tail == input) {
        (*list)->tail = node;
    }
    pthread_mutex_unlock(&(*list)->lock);
    free(input->file_name);
    free(input);
}
/*
    Check whether request currently exists in the list and return it if so.
*/
file_request * find(List ** list, file_request * input) {
    file_request * curr = (*list)->head;
    while (curr != NULL) {
        if (curr->session_id == input->session_id) {
            return curr;
        }
    }
    return NULL;
}