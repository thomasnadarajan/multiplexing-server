#ifndef COMPRESSION_OPT_H
#define COMPRESSION_OPT_H

#include <stdint.h>
#include <stdlib.h>

// Trie node for efficient compression/decompression
typedef struct trie_node {
    struct trie_node *children[2];  // 0 and 1
    unsigned char byte;
    int is_leaf;
} trie_node;

// Optimized compression map using trie structure
typedef struct {
    trie_node *root;
    // Direct lookup table for compression (byte -> code)
    struct {
        uint8_t *code;
        uint8_t code_length;
    } encode_table[256];
} compression_map_opt;

// Function prototypes for optimized compression
void create_map_optimized(compression_map_opt **map);
void destroy_map_optimized(compression_map_opt *map);
void compress_optimized(unsigned char **data, size_t *length, compression_map_opt *map);
void decompress_optimized(unsigned char **data, size_t *length, compression_map_opt *map);

#endif