#ifndef M_H
#define M_H
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "tp.h"
typedef struct header {
    unsigned type : 4;
    unsigned compression : 1;
    unsigned requires_compression : 1;
} header;
typedef struct message {
    header main;
    uint64_t length;
    unsigned char * buffer;
} message;
void get_config (char * file_name, struct sockaddr_in * main,  char ** directory);
message * get_description(int sockfd, m_node ** compress);
void error_send(int sockfd);
void echo(int sockfd, message * input, m_node ** compress);
void file_size_response(int sockfd, message ** input, char * directory, m_node ** compress);
void directory_send(int sockfd, message ** input, char * directory, m_node ** compress);
file_request * dissect_file_request(message * input);
void child_send(int sockfd, int compressed, char * directory, file_request ** input, m_node ** dict);
void parent_send(int sockfd, int compressed, char * directory, file_request ** input, m_node ** dict);
#endif