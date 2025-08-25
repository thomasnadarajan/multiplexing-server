#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include "byteswap_compat.h"
#include "compression.h"
#include "multiplexlist.h"
#include "memory_pool.h"

extern memory_pool *global_pool;

#define SEND_BUFFER_SIZE 65536
#define READ_BUFFER_SIZE 8192

static ssize_t read_full(int fd, void *buf, size_t count) {
    size_t total = 0;
    while (total < count) {
        ssize_t n = read(fd, (char*)buf + total, count - total);
        if (n <= 0) return n;
        total += n;
    }
    return total;
}

static ssize_t send_all(int sockfd, const void *buf, size_t len, int flags) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = send(sockfd, (const char*)buf + total, len - total, flags);
        if (n <= 0) return n;
        total += n;
    }
    return total;
}

message * get_description_optimized(int sockfd, m_node ** compress) {
    unsigned char header;
    if (read(sockfd, &header, 1) != 1) {
        return NULL;
    }
    
    message * msg = global_pool ? mp_alloc(global_pool, sizeof(message)) : malloc(sizeof(message));
    if (!msg) return NULL;
    
    msg->buffer = NULL;
    msg->main.type = (header >> 4);
    
    if (msg->main.type == 0x8 || (msg->main.type != 0 && msg->main.type != 2 && 
                msg->main.type != 4 && msg->main.type != 6 && msg->main.type != 8)) {
        return msg;
    }
    
    msg->main.compression = (header >> 3) & 1;
    msg->main.requires_compression = (header >> 2) & 1;
    msg->length = 0;
    
    if (read_full(sockfd, &msg->length, 8) != 8) {
        if (global_pool) mp_free(global_pool, msg, sizeof(message));
        else free(msg);
        return NULL;
    }
    
    msg->length = bswap_64(msg->length);
    
    if (msg->length > 0) {
        msg->buffer = malloc(msg->length);
        if (!msg->buffer || read_full(sockfd, msg->buffer, msg->length) != msg->length) {
            free(msg->buffer);
            if (global_pool) mp_free(global_pool, msg, sizeof(message));
            else free(msg);
            return NULL;
        }
    }
    
    if (msg->main.compression == 1) {
        if (msg->main.type == 0) {
            if (msg->main.requires_compression != 1) {
                decompress(&msg, compress);
            }
        } else {
            decompress(&msg, compress);
        }
    }
    
    return msg;
}

void echo_optimized(int sockfd, message * input, m_node ** compressor) {
    size_t total_size = input->length + 9;
    char * full = malloc(total_size);
    if (!full) return;
    
    if (input->main.requires_compression == 1) {
        full[0] = 0b00011000;
        if (input->main.compression == 0) {
            compress(&input, compressor);
            total_size = input->length + 9;
        }
    } else {
        full[0] = 0b00010000;
    }
    
    uint64_t to_send = bswap_64(input->length);
    memcpy(full + 1, &to_send, 8);
    
    if (input->length > 0) {
        memcpy(full + 9, input->buffer, input->length);
    }
    
    send_all(sockfd, full, total_size, MSG_NOSIGNAL);
    free(full);
}

void file_size_response_optimized(int sockfd, message ** input, char * directory, m_node ** compressor) {
    char *filename = (char*) (*input)->buffer;
    if (strstr(filename, "..") != NULL || strchr(filename, '/') != NULL) {
        error_send(sockfd);
        return;
    }
    
    size_t path_len = strlen(directory) + strlen(filename) + 2;
    char path[4096];
    
    if (path_len > sizeof(path)) {
        error_send(sockfd);
        return;
    }
    
    snprintf(path, sizeof(path), "%s/%s", directory, filename);
    
    struct stat st;
    if (stat(path, &st) == -1) {
        error_send(sockfd);
        return;
    }
    
    uint64_t size = st.st_size;
    char header;
    
    if ((*input)->main.requires_compression == 1) {
        header = 0b01011000;
        
        message msg;
        msg.length = 8;
        size = bswap_64(size);
        msg.buffer = (unsigned char*)&size;
        
        compress(&msg, compressor);
        
        size_t send_size = 9 + msg.length;
        unsigned char send_buffer[send_size];
        
        send_buffer[0] = header;
        uint64_t compressed_len = bswap_64(msg.length);
        memcpy(send_buffer + 1, &compressed_len, 8);
        memcpy(send_buffer + 9, msg.buffer, msg.length);
        
        send_all(sockfd, send_buffer, send_size, MSG_NOSIGNAL);
        
        if (msg.buffer != (unsigned char*)&size) {
            free(msg.buffer);
        }
    } else {
        header = 0b01010000;
        unsigned char send_buffer[17];
        
        send_buffer[0] = header;
        uint64_t length = bswap_64(8);
        memcpy(send_buffer + 1, &length, 8);
        size = bswap_64(size);
        memcpy(send_buffer + 9, &size, 8);
        
        send_all(sockfd, send_buffer, 17, MSG_NOSIGNAL);
    }
}

void directory_send_optimized(int sockfd, message ** input, char * directory, m_node ** compressor) {
    DIR * dir = opendir(directory);
    if (!dir) {
        error_send(sockfd);
        return;
    }
    
    size_t buf_capacity = 4096;
    size_t buf_size = 0;
    unsigned char *buf = malloc(buf_capacity);
    if (!buf) {
        closedir(dir);
        error_send(sockfd);
        return;
    }
    
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (de->d_type == DT_REG) {
            size_t name_len = strlen(de->d_name);
            size_t needed = buf_size + name_len + 1;
            
            if (needed > buf_capacity) {
                buf_capacity = needed * 2;
                unsigned char *new_buf = realloc(buf, buf_capacity);
                if (!new_buf) {
                    free(buf);
                    closedir(dir);
                    error_send(sockfd);
                    return;
                }
                buf = new_buf;
            }
            
            memcpy(buf + buf_size, de->d_name, name_len);
            buf_size += name_len;
            buf[buf_size++] = '\0';
        }
    }
    
    closedir(dir);
    
    if (buf_size == 0) {
        buf[buf_size++] = '\0';
    }
    
    if ((*input)->main.requires_compression == 1) {
        char header = 0b00111000;
        send(sockfd, &header, 1, MSG_NOSIGNAL);
        
        message msg;
        msg.buffer = buf;
        msg.length = buf_size;
        
        compress(&msg, compressor);
        
        uint64_t compressed_len = bswap_64(msg.length);
        send(sockfd, &compressed_len, 8, MSG_NOSIGNAL);
        send_all(sockfd, msg.buffer, msg.length, MSG_NOSIGNAL);
        
        if (msg.buffer != buf) {
            free(msg.buffer);
        }
        free(buf);
    } else {
        char header = 0b00110000;
        uint64_t length = bswap_64(buf_size);
        
        send(sockfd, &header, 1, MSG_NOSIGNAL);
        send(sockfd, &length, 8, MSG_NOSIGNAL);
        send_all(sockfd, buf, buf_size, MSG_NOSIGNAL);
        free(buf);
    }
}

void parent_send_optimized(int sockfd, int compressed, char * directory, 
                           file_request ** input, m_node ** compressor) {
    char *filename = (char*)(*input)->file_name;
    if (strstr(filename, "..") != NULL || strchr(filename, '/') != NULL) {
        error_send(sockfd);
        return;
    }
    
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", directory, filename);
    
    int fd = open(path, O_RDONLY);
    if (fd == -1) {
        error_send(sockfd);
        close((*input)->pipefd[0]);
        close((*input)->pipefd[1]);
        return;
    }
    
    lseek(fd, (*input)->offset, SEEK_SET);
    
    size_t read_size = (*input)->length;
    unsigned char *file_buffer = malloc(read_size + 20);
    if (!file_buffer) {
        close(fd);
        error_send(sockfd);
        return;
    }
    
    uint32_t temp_int = bswap_32((*input)->session_id);
    uint64_t offset_be = bswap_64((*input)->offset);
    uint64_t length_be = bswap_64((*input)->length);
    
    memcpy(file_buffer, &temp_int, 4);
    memcpy(file_buffer + 4, &offset_be, 8);
    memcpy(file_buffer + 12, &length_be, 8);
    
    ssize_t bytes_read = read(fd, file_buffer + 20, read_size);
    close(fd);
    
    if (bytes_read != read_size) {
        free(file_buffer);
        error_send(sockfd);
        return;
    }
    
    uint64_t offsets[2] = {offset_be, length_be};
    write((*input)->pipefd[1], offsets, 16);
    
    if (compressed == 1) {
        message msg;
        msg.buffer = file_buffer;
        msg.length = 20 + read_size;
        
        compress(&msg, compressor);
        
        char header = 0b01111000;
        uint64_t compressed_len = bswap_64(msg.length);
        
        send(sockfd, &header, 1, MSG_NOSIGNAL);
        send(sockfd, &compressed_len, 8, MSG_NOSIGNAL);
        send_all(sockfd, msg.buffer, msg.length, MSG_NOSIGNAL);
        
        if (msg.buffer != file_buffer) {
            free(msg.buffer);
        }
        free(file_buffer);
    } else {
        char header = 0b01110000;
        uint64_t total_len = bswap_64(20 + read_size);
        
        send(sockfd, &header, 1, MSG_NOSIGNAL);
        send(sockfd, &total_len, 8, MSG_NOSIGNAL);
        send_all(sockfd, file_buffer, 20 + read_size, MSG_NOSIGNAL);
        free(file_buffer);
    }
    
    close((*input)->pipefd[0]);
    close((*input)->pipefd[1]);
}