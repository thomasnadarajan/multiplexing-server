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
#include "message_handling.h"
#include "tp.h"
#include "compression.h"
#include <signal.h>

int main(int argc, char ** argv) {
    // Verify that the user inputs arguments.
    if (argc < 2) {
        return 1;
    }

    // Setup the structures for client and server addresses.
    struct sockaddr_in server_addr, client;
    int sockfd, clfd;
    sockfd = -1;
    clfd = -1;
    socklen_t addr_size;
    addr_size = sizeof(struct sockaddr_in);
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    // Setup thread pool.
    thread_pool * tp = tp_create(argv[1], &server_addr);
    int ret;
    // Bind the address to the socket file descriptor.
    if((ret = bind(sockfd, (struct sockaddr * ) &server_addr, sizeof(struct sockaddr_in))) < 0) {
        printf("%d\n", ret);
        exit(1);
    }
    // Add the server socket to the thread pool.
    tp->serversock = sockfd;
    // Start listening for clients.
    listen(sockfd, 500);
    while(1) {
        // Accept clients.
        clfd = accept(sockfd, (struct sockaddr * ) &client, &addr_size);
        // Close on error (or shutdown).
        if (clfd == -1) {
            close(sockfd);
            free(tp);
            break;
        }
        int * cl = malloc(sizeof(int));
        *cl = clfd;
        pthread_mutex_lock(&tp->mutex);
        enqueue(cl, tp);
        pthread_cond_signal(&tp->cond_var);
        pthread_mutex_unlock(&tp->mutex);
    } 
}