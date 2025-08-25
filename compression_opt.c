#include "compression_opt.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>

// Create a new trie node
static trie_node* create_trie_node() {
    trie_node *node = calloc(1, sizeof(trie_node));
    return node;
}

// Insert a code into the trie for decompression
static void insert_trie(trie_node *root, uint8_t *code, uint8_t code_length, unsigned char byte) {
    trie_node *current = root;
    
    for (int i = 0; i < code_length; i++) {
        int bit = code[i];
        if (!current->children[bit]) {
            current->children[bit] = create_trie_node();
        }
        current = current->children[bit];
    }
    
    current->is_leaf = 1;
    current->byte = byte;
}

// Destroy trie recursively
static void destroy_trie(trie_node *node) {
    if (!node) return;
    destroy_trie(node->children[0]);
    destroy_trie(node->children[1]);
    free(node);
}

void create_map_optimized(compression_map_opt **map) {
    *map = calloc(1, sizeof(compression_map_opt));
    (*map)->root = create_trie_node();
    
    int fd = open("(sample)compression.dict", O_RDONLY);
    if (fd == -1) {
        perror("Failed to open compression dictionary");
        exit(1);
    }
    
    struct stat st;
    if (stat("(sample)compression.dict", &st) == -1) {
        close(fd);
        perror("Failed to stat compression dictionary");
        exit(1);
    }
    
    size_t size = st.st_size;
    unsigned char *buffer = malloc(size);
    if (!buffer) {
        close(fd);
        perror("Failed to allocate memory");
        exit(1);
    }
    
    if (read(fd, buffer, size) != size) {
        close(fd);
        free(buffer);
        perror("Failed to read compression dictionary");
        exit(1);
    }
    close(fd);
    
    int current_byte = 0;
    int curr_byte_bit = 8;
    
    // Parse dictionary and build both encode table and decode trie
    for (int byte_val = 0; byte_val < 256; byte_val++) {
        // Read 8 bits for code length
        uint8_t code_length = 0;
        for (int i = 7; i >= 0; i--) {
            if (current_byte >= size) break;
            
            int bit = (buffer[current_byte] >> (curr_byte_bit - 1)) & 1;
            code_length |= (bit << i);
            
            curr_byte_bit--;
            if (curr_byte_bit == 0) {
                curr_byte_bit = 8;
                current_byte++;
            }
        }
        
        // Allocate and read the code
        (*map)->encode_table[byte_val].code_length = code_length;
        (*map)->encode_table[byte_val].code = malloc(code_length);
        
        for (int i = 0; i < code_length; i++) {
            if (current_byte >= size) break;
            
            (*map)->encode_table[byte_val].code[i] = 
                (buffer[current_byte] >> (curr_byte_bit - 1)) & 1;
            
            curr_byte_bit--;
            if (curr_byte_bit == 0) {
                curr_byte_bit = 8;
                current_byte++;
            }
        }
        
        // Insert into trie for decompression
        insert_trie((*map)->root, (*map)->encode_table[byte_val].code, 
                   code_length, (unsigned char)byte_val);
    }
    
    free(buffer);
}

void destroy_map_optimized(compression_map_opt *map) {
    if (!map) return;
    
    // Free encode table codes
    for (int i = 0; i < 256; i++) {
        free(map->encode_table[i].code);
    }
    
    // Free trie
    destroy_trie(map->root);
    
    free(map);
}

void compress_optimized(unsigned char **data, size_t *length, compression_map_opt *map) {
    if (!data || !*data || !length || *length == 0) return;
    
    // Calculate compressed size (worst case: same size * 8)
    size_t max_compressed_size = (*length) * 8;
    unsigned char *compressed = malloc(max_compressed_size);
    if (!compressed) {
        perror("Failed to allocate compression buffer");
        return;
    }
    
    size_t compressed_bits = 0;
    size_t byte_index = 0;
    int bit_index = 7;
    
    memset(compressed, 0, max_compressed_size);
    
    // Compress each byte
    for (size_t i = 0; i < *length; i++) {
        unsigned char byte = (*data)[i];
        uint8_t *code = map->encode_table[byte].code;
        uint8_t code_length = map->encode_table[byte].code_length;
        
        // Write each bit of the code
        for (int j = 0; j < code_length; j++) {
            if (code[j]) {
                compressed[byte_index] |= (1 << bit_index);
            }
            
            bit_index--;
            if (bit_index < 0) {
                bit_index = 7;
                byte_index++;
            }
            compressed_bits++;
        }
    }
    
    // Calculate actual compressed size in bytes
    size_t compressed_bytes = (compressed_bits + 7) / 8;
    
    // Only use compression if it actually reduces size
    if (compressed_bytes < *length) {
        free(*data);
        *data = realloc(compressed, compressed_bytes);
        *length = compressed_bytes;
    } else {
        free(compressed);
    }
}

void decompress_optimized(unsigned char **data, size_t *length, compression_map_opt *map) {
    if (!data || !*data || !length || *length == 0) return;
    
    // Allocate buffer for decompressed data (assume 2x expansion initially)
    size_t decompressed_capacity = (*length) * 2;
    unsigned char *decompressed = malloc(decompressed_capacity);
    if (!decompressed) {
        perror("Failed to allocate decompression buffer");
        return;
    }
    
    size_t decompressed_size = 0;
    trie_node *current = map->root;
    
    // Traverse compressed data bit by bit
    for (size_t byte_idx = 0; byte_idx < *length; byte_idx++) {
        for (int bit_idx = 7; bit_idx >= 0; bit_idx--) {
            int bit = ((*data)[byte_idx] >> bit_idx) & 1;
            
            current = current->children[bit];
            if (!current) {
                // Invalid code, corruption detected
                free(decompressed);
                return;
            }
            
            if (current->is_leaf) {
                // Found a complete code
                if (decompressed_size >= decompressed_capacity) {
                    // Expand buffer
                    decompressed_capacity *= 2;
                    unsigned char *new_buffer = realloc(decompressed, decompressed_capacity);
                    if (!new_buffer) {
                        free(decompressed);
                        return;
                    }
                    decompressed = new_buffer;
                }
                
                decompressed[decompressed_size++] = current->byte;
                current = map->root;  // Reset to root for next code
            }
        }
    }
    
    // Replace original data with decompressed
    free(*data);
    *data = realloc(decompressed, decompressed_size);
    *length = decompressed_size;
}