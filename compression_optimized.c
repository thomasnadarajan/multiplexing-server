#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "compression.h"
#include "message_handling.h"

#define INITIAL_BUFFER_SIZE 1024
#define BUFFER_GROWTH_FACTOR 2

void create_map_optimized(m_node ** compressor) {
    (*compressor) = calloc(256, sizeof(m_node));
    if (!(*compressor)) {
        perror("Failed to allocate compression map");
        exit(1);
    }
    
    int fd = open("(sample)compression.dict", O_RDONLY);
    if (fd == -1) {
        for (int i = 0; i < 256; i++) {
            (*compressor)[i].byte = i;
            (*compressor)[i].code = malloc(8);
            if (!(*compressor)[i].code) continue;
            
            for (int j = 0; j < 8; j++) {
                (*compressor)[i].code[j] = (i >> (7-j)) & 1;
            }
            (*compressor)[i].code_l = 8;
        }
        return;
    }
    
    struct stat st;
    fstat(fd, &st);
    size_t size = st.st_size;
    
    unsigned char * buffer = malloc(size);
    if (!buffer) {
        close(fd);
        perror("Failed to allocate buffer");
        exit(1);
    }
    
    ssize_t bytes_read = read(fd, buffer, size);
    close(fd);
    
    if (bytes_read != size) {
        free(buffer);
        return;
    }
    
    int curr_num = 0;
    int count = 0;
    int old_curr = 0;
    
    for (int j = 0; j < size; j++) {
        if (buffer[j] == '\n') {
            (*compressor)[count].byte = curr_num;
            
            int code_size = j - old_curr;
            (*compressor)[count].code_l = code_size;
            (*compressor)[count].code = malloc(code_size);
            
            if ((*compressor)[count].code) {
                for (int i = 0; i < code_size; i++) {
                    (*compressor)[count].code[i] = buffer[old_curr + i] - '0';
                }
            }
            
            count++;
            if (count >= 256) break;
            
            if (j + 1 < size) {
                curr_num = 0;
                for (int k = j + 1; k < size && buffer[k] != ' '; k++) {
                    curr_num = curr_num * 10 + (buffer[k] - '0');
                }
                
                for (j = j + 1; j < size && buffer[j] != ' '; j++);
                old_curr = j + 1;
            }
        }
    }
    
    free(buffer);
}

void decompress_optimized(message ** input, m_node ** dict) {
    if (!input || !(*input) || !(*input)->buffer || (*input)->length == 0) {
        return;
    }
    
    size_t estimated_size = (*input)->length * 8;
    unsigned char *new_representation = malloc(estimated_size);
    if (!new_representation) return;
    
    size_t rep_size = 0;
    int *buffer = NULL;
    int buffer_capacity = 0;
    int count = 0;
    
    for (size_t i = 0; i < (*input)->length; i++) {
        for (int j = 8; j >= 1; j--) {
            if (count >= buffer_capacity) {
                buffer_capacity = buffer_capacity ? buffer_capacity * 2 : 8;
                int *new_buffer = realloc(buffer, sizeof(int) * buffer_capacity);
                if (!new_buffer) {
                    free(buffer);
                    free(new_representation);
                    return;
                }
                buffer = new_buffer;
            }
            
            buffer[count++] = ((*input)->buffer[i] >> (j - 1)) & 1;
            
            for (int q = 0; q < 256; q++) {
                if (count == (*dict)[q].code_l) {
                    int match = 1;
                    for (int k = 0; k < count; k++) {
                        if (buffer[k] != (*dict)[q].code[k]) {
                            match = 0;
                            break;
                        }
                    }
                    
                    if (match) {
                        if (rep_size >= estimated_size) {
                            estimated_size *= 2;
                            unsigned char *new_rep = realloc(new_representation, estimated_size);
                            if (!new_rep) {
                                free(buffer);
                                free(new_representation);
                                return;
                            }
                            new_representation = new_rep;
                        }
                        
                        new_representation[rep_size++] = (*dict)[q].byte;
                        count = 0;
                        break;
                    }
                }
            }
        }
    }
    
    free(buffer);
    free((*input)->buffer);
    
    (*input)->buffer = realloc(new_representation, rep_size);
    (*input)->length = rep_size;
}

void compress_optimized(message ** input, m_node ** dict) {
    if (!input || !(*input) || !(*input)->buffer || (*input)->length == 0) {
        return;
    }
    
    size_t estimated_size = (*input)->length * 2;
    unsigned char *new_representation = malloc(estimated_size);
    if (!new_representation) return;
    
    size_t curr_byte = 0;
    int total_count = 0;
    unsigned char current_byte = 0;
    
    for (size_t i = 0; i < (*input)->length; i++) {
        unsigned char byte = (*input)->buffer[i];
        m_node *node = &(*dict)[byte];
        
        for (int j = 0; j < node->code_l; j++) {
            int bit_position = 7 - (total_count % 8);
            
            if (node->code[j] == 1) {
                current_byte |= (1 << bit_position);
            }
            
            total_count++;
            
            if (total_count % 8 == 0) {
                if (curr_byte >= estimated_size) {
                    estimated_size *= 2;
                    unsigned char *new_rep = realloc(new_representation, estimated_size);
                    if (!new_rep) {
                        free(new_representation);
                        return;
                    }
                    new_representation = new_rep;
                }
                
                new_representation[curr_byte++] = current_byte;
                current_byte = 0;
            }
        }
    }
    
    if (total_count % 8 != 0) {
        if (curr_byte >= estimated_size) {
            estimated_size = curr_byte + 1;
            unsigned char *new_rep = realloc(new_representation, estimated_size);
            if (!new_rep) {
                free(new_representation);
                return;
            }
            new_representation = new_rep;
        }
        
        uint8_t bits_left = (8 - (total_count % 8)) % 8;
        current_byte |= bits_left;
        new_representation[curr_byte++] = current_byte;
    }
    
    free((*input)->buffer);
    (*input)->buffer = realloc(new_representation, curr_byte);
    (*input)->length = curr_byte;
}