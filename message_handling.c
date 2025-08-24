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
#include <byteswap.h>
#include <errno.h>
#include <dirent.h>
#include "compression.h"
#include "multiplexlist.h"
#include <sys/select.h>
#define _GNU_SOURCE
/*
    Takes in the name of the config file, pointer to the field inside the thread_pool
    structure within which the name of the directory will be stored, and server's main
    address structure. Reads config file, updates the fields of the address structure
    and the name of the directory.
*/
void get_config (char * file_name, struct sockaddr_in * main, char ** directory) {
    int fd = open(file_name, O_RDONLY);
    // Using the stat library calculate the length of the file.
    struct stat st;
    stat(file_name, &st);
    size_t size = st.st_size;
    size-=6;
    read(fd, &main->sin_addr.s_addr, 4);
    read(fd, &main->sin_port, 2);
    *(directory) = malloc(size + 1);
    read(fd, *directory, size);
    (*(directory))[size] = '\0';
    close(fd);
}
/*
    Get Description finds and appropriately separates the contents of the message header.
    Uses bit shifting (4, 3 and 2 bits to the right). A message structure exists
    for this purpose. Has field describings describing type, length
    and compression settings. Decompresses message where required.
*/
message * get_description(int sockfd, m_node ** compress) {
    
    unsigned char header;
    if (read(sockfd, &header, 1) == 0) {
        close(sockfd);
        return NULL;
    }
    message * msg;
    msg = malloc(sizeof(message));
    msg->buffer = NULL;
    msg->main.type = (header >> 4);
    if (msg->main.type == 0x8 || (msg->main.type != 0 && msg->main.type != 2 && 
                msg->main.type != 4 && msg->main.type != 6 && msg->main.type != 8)) {
        return msg;
    }
    /* 
    Shift for setting fields appropriately
    in the bit field.
    */
    msg->main.compression = (header >> 3);
    msg->main.requires_compression = (header >> 2);
    msg->length = 0;
    read(sockfd, &msg->length, 8);
    msg->length = bswap_64(msg->length);
    if (msg->length > 0) {
        msg->buffer = malloc(msg->length);
        read(sockfd, msg->buffer, msg->length);
    }
    /* 
    Decompress the payload if the type is not echo
    and payload already compressed.
    */
    if (msg->main.compression == 1) {
        if (msg->main.type == 0) {
            if (msg->main.requires_compression != 1) {
                decompress(&msg, compress);
            }
        }
        else {
            decompress(&msg, compress);
        }
    }
    return msg;
}
/*
    Sends header containing appropraite error bits.
*/
void error_send(int sockfd) {
    char header = 0b11110000;
    uint64_t a = 0;
    send(sockfd, &header, 1, 0);
    send(sockfd, &a, 8, 0);
}
/*
    Send back the contents of the payload and compress where appropriate.
*/
void echo(int sockfd, message * input, m_node ** compressor) {
    char * full = NULL;
    full = malloc(input->length + 9);
    // Compress where requires compression set and compression not already set.
    if (input->main.requires_compression == 1) {
        // Set the message header appropriately (depending on compression).
        full[0] =  0b00011000;
        if (input->main.compression == 0) {
            compress(&input, compressor);
        }
    }
    else {
        // Set the message header appropriately (depending on compression).
        full[0] =  0b00010000;
    }
    uint64_t to_send = bswap_64(input->length);
    memcpy(full + 1, &to_send, 8);
    if (input->length > 0) {
        memcpy(full + 9, input->buffer, input->length);
    }
    send(sockfd, full, input->length + 9, 0);
    free(full);
}
/*
    Takes in the message for the file, and calculates the file size
    using the stat library. Compress where appropriate. Takes in compression struct.
*/
void file_size_response(int sockfd, message ** input, char * directory, m_node ** compressor) {
    // Validate filename doesn't contain path traversal
    char *filename = (char*) (*input)->buffer;
    if (strstr(filename, "..") != NULL || strchr(filename, '/') != NULL) {
        error_send(sockfd);
        return;
    }
    
    // Use snprintf to prevent buffer overflow
    size_t path_len = strlen(directory) + strlen(filename) + 2;
    char * path = malloc(path_len);
    if (!path) {
        error_send(sockfd);
        return;
    }
    snprintf(path, path_len, "%s/%s", directory, filename);
    
    int fd = open(path, O_RDONLY);
    if (fd == -1) {
        free(path);
        error_send(sockfd);
        return;
    }
    // Using the stat library calculate the file size.
    struct stat st;
    stat(path, &st);
    uint64_t size = st.st_size;
    close(fd);
    free(path);
    char header;
    // Set the message header appropriately, but compress since bit set.
    if ((*input)->main.requires_compression == 1) {
        header = 0b01011000;
        // Create message for the purposes of inputting into the standard form compression function.
        message * msg = malloc(sizeof(message));
        msg->length = 8;
        size = bswap_64(size);
        msg->buffer = malloc(8);
        memcpy(msg->buffer , &size, 8);
        // Send for compression.
        compress(&msg, compressor);
        unsigned char * send_container = malloc(9 + msg->length);
        // Stores the old_length, before endian swap.
        uint64_t old_l = msg->length;
        msg->length = bswap_64(msg->length);
        // Set header appropriately.
        send_container[0] = header;
        // Copy contents of compressed across to the final container.
        memcpy(send_container + 1, &msg->length, 8);
        memcpy(send_container + 9, msg->buffer, old_l);
        send(sockfd, send_container, 9 + old_l, 0);
        free(msg->buffer);
        free(msg);
        free(send_container);
        
    }
    else {
        // Set the message header appropriately (depending on compression).
        header = 0b01010000;
        uint64_t length = bswap_64(8);
        unsigned char * send_container = malloc(17);
        // Set header appropriately.
        send_container[0] = header;
        // Copy the contents of the message to be sent to the relevant container.
        memcpy(send_container + 1, &length, 8);
        size = bswap_64(size);
        memcpy(send_container + 9, &size, 8);
        send(sockfd, send_container, 17, 0);
        free(send_container);
    }
    
    free(path);
}
/*
    Takes in the name of the directory and lists files in the directory.
    Stores these in a dynamically allocated array of characters.
    Uses DIR pointers as opposed to purely low level system calls.
*/
void directory_send(int sockfd, message ** input, char * directory, m_node ** compressor) {
    int old_l = 0;
    unsigned char * buf = NULL;
    struct dirent *de;
    DIR * d;
    int n = 0;
    if ((d = opendir(directory))) {
        // Iterate through the files in this directory and add to the buffer containing file names.
        while ((de = readdir(d)) != NULL) {
            if (de->d_type == DT_REG) {
                buf = realloc(buf, old_l + strlen(de->d_name) + 1);
                strcpy((char*) buf + old_l, (char*) de->d_name);
                old_l += strlen(de->d_name) + 1;
                buf[old_l -1] = '\0';
                n++;
            }
        }
        if (n == 0) {
            old_l++;
            buf = realloc(buf, 1);
            buf[old_l - 1] = '\0';
        }
    }
    else {
        printf("This broke\n");
    }

    char header;
    if ((*input)->main.requires_compression == 1) {
        header = 0b00111000;
        send(sockfd, &header, 1, 0);
        message * msg = malloc(sizeof(message));
        msg->buffer = buf;
        msg->length = old_l;
        // Compress data attached to standard message input.
        compress(&msg, compressor);
        old_l = msg->length;
        msg->length = bswap_64(msg->length);
        // Send compressed directory data piecewise - length then buffer.
        send(sockfd, &msg->length, 8, 0);
        send(sockfd , msg->buffer, old_l, 0);
        free(msg->buffer);
        free(msg);
    }
    else {
        header = 0b00110000;
        send(sockfd, &header, 1, 0);
        uint64_t temp = old_l;
        temp = bswap_64(temp);
        send(sockfd, &temp, 8, 0);
        send(sockfd, buf, old_l, 0);
        free(buf);
    }
    closedir(d);
    
}
/*
    Accesses a message, dissects in terms of the file request description components and creates
    new instance of a file_request to be added to the request queue. 
*/
file_request * dissect_file_request(message * input) {
    file_request * req = malloc(sizeof(file_request));
    memcpy(&req->session_id, input->buffer, 4);
    memcpy(&req->offset, (input->buffer + 4), 8);
    memcpy(&req->length, (input->buffer + 12), 8);
    // Swap out of network byte order.
    req->session_id = bswap_32(req->session_id);
    req->length = bswap_64(req->length);
    req->offset = bswap_64(req->offset);
    req->file_name = malloc(strlen((char *)(input->buffer + 20)) + 1);
    // Maintain the file_name in the request.
    strcpy((char *) req->file_name, (char *) (input->buffer + 20));
    return req;
}

void child_send(int sockfd, int compressed, char * directory, file_request ** input, m_node ** dict) {
    // Validate filename doesn't contain path traversal
    char *filename = (char *)(*input)->file_name;
    if (strstr(filename, "..") != NULL || strchr(filename, '/') != NULL) {
        error_send(sockfd);
        return;
    }
    
    // Use snprintf to prevent buffer overflow
    size_t path_len = strlen(directory) + strlen(filename) + 2;
    char * path = malloc(path_len);
    if (!path) {
        error_send(sockfd);
        return;
    }
    snprintf(path, path_len, "%s/%s", directory, filename);
    // Pull offset and length from pipe contained in the file request.
    uint64_t o_l[2];
    read((*input)->pipefd[0], o_l, 16);
    unsigned char * buffer = malloc(20 + o_l[1]);
    uint32_t temp_int = bswap_32((*input)->session_id);
    o_l[0] = bswap_64(o_l[0]);
    o_l[1] = bswap_64(o_l[1]);
    // Copy contents of file header to new buffer.
    memcpy(buffer, &temp_int, 4);
    memcpy(buffer + 4, &o_l[0], 8);
    memcpy(buffer + 12, &o_l[1], 8);
    o_l[0] = bswap_64(o_l[0]);
    o_l[1] = bswap_64(o_l[1]);
    int fd = open(path, O_RDONLY);
    if (fd == -1) {
        free(buffer);
        free(path);
        error_send(sockfd);
        return;
    }
    // Seek to location in file.
    lseek(fd, o_l[0], SEEK_SET);
    // Read contents of file into buffer.
    read(fd, buffer + 20, o_l[1]);
    close(fd);
    free(path);
    if (compressed == 1) {
        message * msg = malloc(sizeof(message));
        msg->buffer = buffer;
        msg->length = 20 + o_l[1];
        // Attach memory to standard message input and compress.
        compress(&msg, dict);
        // Store old length, before endian swap.
        uint64_t temp = msg->length;
        msg->length = bswap_64(msg->length);
        // Create container for final message.
        unsigned char * send_container = malloc(9 + temp);
        send_container[0] = 0b01111000;
        // Copy data to sending container.
        memcpy(send_container + 1, &msg->length, 8);
        memcpy(send_container + 9, msg->buffer, temp);
        send(sockfd, send_container, temp + 9, 0);
        // Cleanup.
        free(msg->buffer);
        free(msg);
        free(send_container);
    }
    else {
        uint64_t temp = 20 + o_l[1];
        // Create container for final message.
        unsigned char * send_container = malloc(9 + temp);
        send_container[0] = 0b01110000;
        uint64_t temp_o = bswap_64(temp);
        // Copy data to container.
        memcpy(send_container + 1, &temp_o, 8);
        memcpy(send_container + 9, buffer, temp);
        send(sockfd, send_container, temp + 9, 0);
        free(send_container);
        free(buffer);
    }
}

void parent_send(int sockfd, int compressed, char * directory, file_request ** input, m_node ** dict) {
    // Validate filename doesn't contain path traversal
    char *filename = (char *)((*input)->file_name);
    if (strstr(filename, "..") != NULL || strchr(filename, '/') != NULL) {
        error_send(sockfd);
        return;
    }
    
    // Use snprintf to prevent buffer overflow
    size_t path_len = strlen(directory) + strlen(filename) + 2;
    char * path = malloc(path_len);
    if (!path) {
        error_send(sockfd);
        return;
    }
    snprintf(path, path_len, "%s/%s", directory, filename);

    int fd = open(path, O_RDONLY);

    if (fd == -1) {
        free(path);
        error_send(sockfd);
        return;
    }
    // Use stat library to measure file size.
    struct stat st;
    stat(path, &st);

    if ((*input)->offset > st.st_size || (*input)->offset + (*input)->length > st.st_size) {
        close(fd);
        free(path);
        error_send(sockfd);
        return;
    }
    uint64_t size = (*input)->length;
    /* Divide the file size into blocks, depending on the number
    of connections. */
    uint64_t division = 0;
    division = size / ((*input)->num_connect + 1);
    // Initialise a buffer with the basic file segment details.
    unsigned char * buffer = malloc(20 + division);
    division = bswap_64(division);
    (*input)->session_id = bswap_32((*input)->session_id);
    memcpy(buffer, &((*input)->session_id), 4);
    memcpy(buffer + 12, &division, 8);
    division = bswap_64(division);
    (*input)->session_id = bswap_32((*input)->session_id);
    // Set the current offset to the offset specified in the message header.
    uint64_t current_offset = (*input)->offset;
    // Split the remaining data into new segments.
    if ((*input)->num_connect > 0) {
        // Write the next offset and length to the pipe to each multiplexed connection.
        uint64_t o_l[2];
        uint64_t num_alt = division + 1;
        o_l[0] = current_offset;
        o_l[1] = num_alt;
        for(int i = 0; i <  size % ((*input)->num_connect + 1); i++) {
            write((*input)->pipefd[1], o_l, 16);
            current_offset+=num_alt;
            o_l[0] = current_offset;
        }
        int remaining = (*input)->num_connect - (size % ((*input)->num_connect + 1));
        o_l[1] = division;
        for (int i = 0; i < remaining; i++) {
            write((*input)->pipefd[1], o_l, 16);
            current_offset+=division;
            o_l[0] = current_offset;
        }
    }
    // Add offset for the parent sender to the buffer.
    current_offset = bswap_64(current_offset);
    memcpy(buffer + 4, &current_offset, 8);
    current_offset = bswap_64(current_offset);
    // Seek to the offset.
    lseek(fd, current_offset, SEEK_SET);
    read(fd, buffer + 20, division);
    
    if (compressed == 1) {
        message * msg = malloc(sizeof(message));
        msg->buffer = buffer;
        msg->length = division + 20;
        // After putting compression data in standard form, compress.
        compress(&msg, dict);
        uint64_t temp = msg->length;
        msg->length = bswap_64(msg->length);
        // Configure container for final send with relevant file data.
        unsigned char *send_container = malloc(9 + temp);
        send_container[0] = 0b01111000;
        memcpy(send_container + 1, &msg->length, 8);
        memcpy(send_container + 9, msg->buffer, temp);
        send(sockfd, send_container, temp + 9, 0);    
        free(send_container);   
        free(msg->buffer);
        free(msg);
    }
    else {
        uint64_t temp = division + 20;
        uint64_t o_temp = bswap_64(temp);
        // Configure container for final send with relevant file data.
        unsigned char *send_container = malloc(temp + 9);
        send_container[0] = 0b01110000;
        memcpy(send_container + 1, &o_temp, 8);
        memcpy(send_container + 9, buffer, temp);
        send(sockfd, send_container, 9 + temp, 0);
        free(send_container);
        free(buffer);
    }
    // Final cleanup.
    free(path);
    close(fd);
}