#include <pthread.h>
#include "tp.h"
#include <netinet/in.h>
#include "message_handling.h"
#include "compression.h"
#include "multiplexlist.h"
#include "byteswap_compat.h"
/*
    Create a thread pool, and store compression dict and config details within.
*/
thread_pool * tp_create(char * config_name, struct sockaddr_in * sock) {
    thread_pool * tp = malloc (sizeof(thread_pool));
    pthread_cond_init(&tp->cond_var, NULL);
    pthread_mutex_init(&tp->mutex, NULL);
    tp->head = NULL;
    tp->tail = NULL;
    tp->shut = 0;
    create_map(&(tp->data.dict));
    for (int i = 0 ; i < 20; i++) {
        if (pthread_create(&(tp->threads[i]), NULL, thread_worker, tp) != 0) {
            perror("pthread_create failed");
            exit(1);
        }
    }
    get_config(config_name, sock, &(tp->data.directory));
    tp->requests_list = create();
    return tp;
}
/*
    Dequeue work from the thread pool.
*/
int * dequeue(thread_pool * input) {
    if (input->head == NULL) {
        return NULL;
    }
    else {
        int * toret = input->head->clfd;
        Node * tmp = input->head;
        input->head = input->head->next;
        if (input->head == NULL) {
            input->tail = NULL;
        }
        free(tmp);
        return toret;
    }
}
/*
    Enqueue work (a socket descriptor) on the thread pool.
*/
void enqueue(int * clfd, thread_pool * input) {
    Node * toadd = malloc(sizeof(Node));
    toadd->clfd = clfd;
    toadd->next = NULL;
    if (input->tail == NULL) {
        input->head = toadd;
    }
    else {
        input->tail->next = toadd;
    }
    input->tail = toadd;
}
/*
    Main thread loop, waits for work at condition variable.
    Jumps back to loop start if socket descriptor closed.
*/
void * thread_worker(void * args) {
    thread_pool * input = (thread_pool *) args;
    while (1) {
        if (input->shut == 1) {
            return NULL;
        }
        pthread_mutex_lock(&input->mutex);
        int * clfd;
        // Wait on the condition variable.
        while ((clfd = dequeue(input)) == NULL) {
            pthread_cond_wait(&input->cond_var, &input->mutex);
            if (input->shut == 1) {
                break;
            }
        }
        if (input->shut == 1) {
            break;
        }
        pthread_mutex_unlock(&input->mutex);
        if (clfd != NULL) {
            client_handling(clfd, input);
        }
        if (input->shut == 1) {
            break;
        }
    }
    return NULL;
}

void client_handling(int * clfd, thread_pool * input) {
    int main = *clfd;
        while (1) {
            // If the server has been shutdown, break from the loop.
            if (input->shut == 1) {
                return;
            }
            // Get the message from client.
            message * msg = get_description(main, &(input->data.dict));

            //  If the client closed the connection, break from the loop.
            if (msg == NULL) {
                close(main);
                free(clfd);
                return;
            }
            // If the error message is received, break from the loop.
            if (msg->main.type != 0 && msg->main.type != 2 && 
                msg->main.type != 4 && msg->main.type != 6 && msg->main.type != 8) {
                error_send(main);
                close(main);
                free(clfd);
                free(msg->buffer);
                free(msg);
                return;
            }
            // Echo handling.
            if (msg->main.type == 0x0) {
                echo(main, msg, &(input->data.dict));
            }
            // Directory send handling.
            if (msg->main.type == 0x2) {
                directory_send(main, &msg, (input->data.directory), &(input->data.dict));
            }
            // Send file size handling.
            if (msg->main.type == 0x4) {
                file_size_response(main, &msg, input->data.directory, &(input->data.dict));
            }
            if (msg->main.type == 0x6){
                file_request * req = dissect_file_request(msg);
                //find if a request already exists.
                file_request * curr = NULL;
                // Find if the request exists in the list already.
                if ((curr = find(&(input->requests_list), req))) {
                    // If any of the properties are not the same in the received request, error.
                    if (strcmp((char*)req->file_name, (char*)curr->file_name) != 0 || 
                        req->length != curr->length || 
                            req->offset != curr->offset) {
                        error_send(main);
                        free(clfd);
                        free(msg->buffer);
                        free(msg);
                        return;
                    }
                    else {
                        // Handle a message sent from child.
                        pthread_mutex_lock(&curr->node_lock);
                        curr->num_connect++;
                        pthread_mutex_unlock(&curr->node_lock);
                        child_send(main, msg->main.requires_compression, 
                            input->data.directory, &curr, &(input->data.dict));
                        close(main);
                        free(req->file_name);
                        free(req);
                        free(msg->buffer);
                        free(msg);
                        free(clfd);
                        return;
                    }
                }
                else {
                    // Add file request to the list.
                    pipe(req->pipefd);
                    pthread_mutex_init(&req->node_lock, NULL);
                    req->num_connect = 0;
                    add(&(input->requests_list), req);
                    parent_send(main, msg->main.requires_compression, 
                        input->data.directory, &req, &(input->data.dict));
                    remove_node(&(input->requests_list), req);
               }
               
            }
            // Shut down server.
            if (msg->main.type == 0x8) {
                close(main);
                free(clfd);
                free(msg);
                input->shut = 1;
                pthread_cond_broadcast(&input->cond_var);
                int * f;
                while((f = dequeue(input)) != NULL) {
                    close(*f);
                    free(f);
                }
                for (int i = 0; i < 256; i++) {
                    free(input->data.dict[i].code);
                }
                // Free all of the structures attached to the thread pool.
                free(input->data.dict);
                free(input->data.directory);
                free(input->requests_list);
                /* Send shutdown to main server socket,
                cancelling accept blocking.
                */
                shutdown(input->serversock, SHUT_RDWR);
                return;
            }
            free(msg->buffer);
            free(msg);
        }
}