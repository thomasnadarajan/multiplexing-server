#include "memory_pool.h"
#include <string.h>
#include <stdio.h>

static pool_block* create_block_list(size_t block_size, int count) {
    pool_block *head = NULL;
    pool_block *current = NULL;
    
    for (int i = 0; i < count; i++) {
        pool_block *block = malloc(sizeof(pool_block));
        if (!block) return head;
        
        block->memory = malloc(block_size);
        if (!block->memory) {
            free(block);
            return head;
        }
        
        block->in_use = 0;
        block->next = NULL;
        
        if (!head) {
            head = block;
            current = block;
        } else {
            current->next = block;
            current = block;
        }
    }
    
    return head;
}

memory_pool* mp_create() {
    memory_pool *pool = malloc(sizeof(memory_pool));
    if (!pool) return NULL;
    
    pthread_mutex_init(&pool->lock, NULL);
    pool->allocations = 0;
    pool->deallocations = 0;
    
    pool->small_blocks = create_block_list(POOL_SMALL_SIZE, POOL_SMALL_COUNT);
    pool->medium_blocks = create_block_list(POOL_MEDIUM_SIZE, POOL_MEDIUM_COUNT);
    pool->large_blocks = create_block_list(POOL_LARGE_SIZE, POOL_LARGE_COUNT);
    
    return pool;
}

void* mp_alloc(memory_pool *pool, size_t size) {
    if (!pool) return malloc(size);
    
    pthread_mutex_lock(&pool->lock);
    
    pool_block *blocks = NULL;
    size_t block_size = 0;
    
    if (size <= POOL_SMALL_SIZE) {
        blocks = pool->small_blocks;
        block_size = POOL_SMALL_SIZE;
    } else if (size <= POOL_MEDIUM_SIZE) {
        blocks = pool->medium_blocks;
        block_size = POOL_MEDIUM_SIZE;
    } else if (size <= POOL_LARGE_SIZE) {
        blocks = pool->large_blocks;
        block_size = POOL_LARGE_SIZE;
    } else {
        pthread_mutex_unlock(&pool->lock);
        return malloc(size);
    }
    
    pool_block *current = blocks;
    while (current) {
        if (!current->in_use) {
            current->in_use = 1;
            pool->allocations++;
            pthread_mutex_unlock(&pool->lock);
            memset(current->memory, 0, block_size);
            return current->memory;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&pool->lock);
    return malloc(size);
}

void mp_free(memory_pool *pool, void *ptr, size_t size) {
    if (!pool || !ptr) {
        free(ptr);
        return;
    }
    
    pthread_mutex_lock(&pool->lock);
    
    pool_block *blocks = NULL;
    
    if (size <= POOL_SMALL_SIZE) {
        blocks = pool->small_blocks;
    } else if (size <= POOL_MEDIUM_SIZE) {
        blocks = pool->medium_blocks;
    } else if (size <= POOL_LARGE_SIZE) {
        blocks = pool->large_blocks;
    } else {
        pthread_mutex_unlock(&pool->lock);
        free(ptr);
        return;
    }
    
    pool_block *current = blocks;
    while (current) {
        if (current->memory == ptr) {
            current->in_use = 0;
            pool->deallocations++;
            pthread_mutex_unlock(&pool->lock);
            return;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&pool->lock);
    free(ptr);
}

static void destroy_block_list(pool_block *head) {
    while (head) {
        pool_block *next = head->next;
        free(head->memory);
        free(head);
        head = next;
    }
}

void mp_destroy(memory_pool *pool) {
    if (!pool) return;
    
    destroy_block_list(pool->small_blocks);
    destroy_block_list(pool->medium_blocks);
    destroy_block_list(pool->large_blocks);
    
    pthread_mutex_destroy(&pool->lock);
    free(pool);
}

void mp_stats(memory_pool *pool) {
    if (!pool) return;
    
    printf("Memory Pool Statistics:\n");
    printf("  Allocations: %zu\n", pool->allocations);
    printf("  Deallocations: %zu\n", pool->deallocations);
    printf("  Active allocations: %zu\n", pool->allocations - pool->deallocations);
}