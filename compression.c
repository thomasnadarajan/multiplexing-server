#include "compression.h"
#include "message_handling.h"
#include <sys/stat.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
/*
    Interpret the compression dictionary as a map data structure. Store in thread
    pool structure.
*/
void create_map(m_node ** compressor) {
    (*compressor) = malloc(256 * sizeof(m_node));
    int fd = open("(sample)compression.dict", O_RDONLY);
    struct stat st;
    stat("(sample)compression.dict", &st);
    int size = st.st_size;
    unsigned char * buffer = malloc(size);
    read(fd, buffer, size);
    int count = 0;
    unsigned char curr = 0x00;
    int current_byte = 0;
    int curr_byte_bit = 8;
    /*
        Count bits read from the dictionary, and for each of the specified bits add it to the current
        map byte's storage of the in unsigned 8 bit integer form.
    */
    while (count < 256) {
        (*compressor)[count].byte = curr;
        u_int8_t len[8];
        for (int i = 0; i < 8; i++) {
            len[i] = (buffer[current_byte] & ( 1 << (curr_byte_bit - 1) )) >> (curr_byte_bit - 1);
            curr_byte_bit--;
            if (curr_byte_bit == 0) {
                curr_byte_bit = 8;
                current_byte++;
            }
        }
        // Get the convert the len array to the 1 byte integer describing code length.
        u_int8_t size = 0;
        int power = 7;
        for (int i = 0; i < 8; i++) {
            if(!(2 * len[i] == 0 && power == 0)) {
                size += pow(2 * len[i], power);
            }
            power--;
        }
        // Add extract successive bits for size and add if set to the code array.
        (*compressor)[count].code_l = size;
        (*compressor)[count].code = malloc(size);
        for (int i = 0; i < size; i++) {
            (*compressor)[count].code[i] = (buffer[current_byte] & ( 1 << (curr_byte_bit - 1) )) >> (curr_byte_bit - 1);
            curr_byte_bit--;
            if (curr_byte_bit == 0) {
                curr_byte_bit = 8;
                current_byte++;
            }
        }
        count++;
        curr++;
    }
    free(buffer);
    
}
void decompress(message ** input, m_node ** dict) {
    unsigned char * new_representation = NULL;
    u_int8_t * buffer = NULL;
    int count = 0;
    int rep_size = 0;
    int total_bits = (((*input)->length - 1) * 8) - ((u_int8_t) (*input)->buffer[(*input)->length - 1]);
    int curr_bits = 0;
    for (int i = 0; i < (*input)->length; i++) {
        for (int j = 8; j > 0; j--) {
            count++;
            buffer = realloc(buffer, sizeof(int) * count);
            buffer[count - 1] = ((*input)->buffer[i] & ( 1 << (j - 1) )) >> (j - 1);
            for (int q = 0; q < 256; q++) {
                if ((*dict)[q].code_l == count) {
                    if (memcmp((*dict)[q].code, buffer, count) == 0) {
                        rep_size++;
                        new_representation = realloc(new_representation, rep_size);
                        new_representation[rep_size - 1] = (*dict)[q].byte;
                        count = 0;
                        free(buffer);
                        buffer = NULL;
                        break;
                    }
                }
            }
            curr_bits++;
            if (curr_bits == total_bits) {
                
                if (count != 0) {
                    free(buffer);
                }
                free((*input)->buffer);
                (*input)->buffer = new_representation;
                (*input)->length = rep_size;
                return;
            }
        }
    }

    return;
}

void compress(message** input, m_node ** dict) {
    unsigned char * new_representation = NULL;
    int curr_byte = 0;
    int curr_bit = 8;
    int total_count = 0;
    /* 
        Iterate through the bytes of the message payload.
        Write to the new_representation, extending
        the representation each time we have read in 8 bits from the dictionary.
    */
    for (int i = 0; i < (*input)->length; i++) {
        for (int j = 0; j < (*dict)[(unsigned int)(*input)->buffer[i]].code_l; j++) {
            if (curr_bit == 8) {
                curr_byte++;
                new_representation = realloc(new_representation, curr_byte);
            }
            if ((*dict)[(unsigned int) (*input)->buffer[i]].code[j]) {
                new_representation[curr_byte -1] |= (1 << (curr_bit - 1));
            }
            else {
                new_representation[curr_byte -1] &= ~(1 << (curr_bit - 1));
            }
            curr_bit--;
            total_count++;
            if (curr_bit == 0) {
                curr_bit = 8;
            }
        }
    }
    // Zero out any bits remaining for padding.
    while (curr_bit >0 && curr_bit != 8) {
        new_representation[curr_byte -1] &= ~(1 << (curr_bit - 1));
        curr_bit--;
    }
    // Add the number of padding bits to the end.
    curr_byte++;
    new_representation = realloc(new_representation, curr_byte);
    u_int8_t bits_left = (8 - (total_count % 8)) % 8;
    new_representation[curr_byte - 1] = bits_left;
    (*input)->length = curr_byte;
    free((*input)->buffer);
    (*input)->buffer = new_representation;
}