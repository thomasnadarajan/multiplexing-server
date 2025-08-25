#ifndef MEMORY_POOL_H
#define MEMORY_POOL_H

#include <stdlib.h>
#include <pthread.h>

#define POOL_SMALL_SIZE 32
#define POOL_MEDIUM_SIZE 256
#define POOL_LARGE_SIZE 1024
#define POOL_SMALL_COUNT 1000
#define POOL_MEDIUM_COUNT 500
#define POOL_LARGE_COUNT 100

typedef struct pool_block {
    void *memory;
    int in_use;
    struct pool_block *next;
} pool_block;

typedef struct {
    pool_block *small_blocks;
    pool_block *medium_blocks;
    pool_block *large_blocks;
    pthread_mutex_t lock;
    size_t allocations;
    size_t deallocations;
} memory_pool;

memory_pool* mp_create();
void* mp_alloc(memory_pool *pool, size_t size);
void mp_free(memory_pool *pool, void *ptr, size_t size);
void mp_destroy(memory_pool *pool);
void mp_stats(memory_pool *pool);

#endif