#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <assert.h>
#include "compression.h"
#include "message_handling.h"
#include "multiplexlist.h"

#define NUM_ITERATIONS 10000
#define NUM_THREADS 20
#define LARGE_BUFFER_SIZE 1048576  // 1MB
#define SMALL_BUFFER_SIZE 1024     // 1KB

typedef struct {
    double duration;
    size_t memory_used;
    size_t operations;
} benchmark_result;

static double get_time_diff(struct timeval start, struct timeval end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
}

static size_t get_memory_usage() {
    FILE *fp = fopen("/proc/self/status", "r");
    if (!fp) return 0;
    
    char line[256];
    size_t vm_rss = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line, "VmRSS: %zu", &vm_rss);
            break;
        }
    }
    
    fclose(fp);
    return vm_rss * 1024; // Convert KB to bytes
}

// Test 1: Memory allocation patterns - frequent small allocations
benchmark_result test_memory_allocation_small() {
    printf("\n[TEST 1] Testing small memory allocation patterns...\n");
    struct timeval start, end;
    size_t initial_mem = get_memory_usage();
    
    gettimeofday(&start, NULL);
    
    // Simulate frequent small allocations (current inefficient pattern)
    void *buffers[NUM_ITERATIONS];
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        buffers[i] = malloc(32);
        if (!buffers[i]) {
            printf("Allocation failed at iteration %d\n", i);
            break;
        }
        memset(buffers[i], 'A', 32);
    }
    
    // Cleanup
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        if (buffers[i]) free(buffers[i]);
    }
    
    gettimeofday(&end, NULL);
    
    benchmark_result result = {
        .duration = get_time_diff(start, end),
        .memory_used = get_memory_usage() - initial_mem,
        .operations = NUM_ITERATIONS
    };
    
    printf("  Duration: %.4f seconds\n", result.duration);
    printf("  Operations: %zu\n", result.operations);
    printf("  Throughput: %.0f ops/sec\n", result.operations / result.duration);
    
    return result;
}

// Test 2: Compression/Decompression performance
benchmark_result test_compression_performance() {
    printf("\n[TEST 2] Testing compression performance...\n");
    struct timeval start, end;
    
    // Setup compression dictionary
    m_node *dict;
    create_map(&dict);
    
    // Create test data
    unsigned char *test_data = malloc(SMALL_BUFFER_SIZE);
    for (int i = 0; i < SMALL_BUFFER_SIZE; i++) {
        test_data[i] = 'A' + (i % 26);
    }
    
    gettimeofday(&start, NULL);
    
    // Test compression/decompression cycles
    for (int i = 0; i < NUM_ITERATIONS / 10; i++) {
        message *msg = malloc(sizeof(message));
        msg->buffer = malloc(SMALL_BUFFER_SIZE);
        memcpy(msg->buffer, test_data, SMALL_BUFFER_SIZE);
        msg->length = SMALL_BUFFER_SIZE;
        
        // Compress
        compress(&msg, &dict);
        
        // Decompress
        decompress(&msg, &dict);
        
        free(msg->buffer);
        free(msg);
    }
    
    gettimeofday(&end, NULL);
    
    // Cleanup
    for (int i = 0; i < 256; i++) {
        if (dict[i].code) free(dict[i].code);
    }
    free(dict);
    free(test_data);
    
    benchmark_result result = {
        .duration = get_time_diff(start, end),
        .operations = NUM_ITERATIONS / 10
    };
    
    printf("  Duration: %.4f seconds\n", result.duration);
    printf("  Operations: %zu compress/decompress cycles\n", result.operations);
    printf("  Throughput: %.0f cycles/sec\n", result.operations / result.duration);
    
    return result;
}

// Test 3: Thread pool enqueue/dequeue performance
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int *items[NUM_ITERATIONS];
    int head;
    int tail;
    int count;
} test_queue;

benchmark_result test_thread_pool_queue() {
    printf("\n[TEST 3] Testing thread pool queue performance...\n");
    struct timeval start, end;
    
    test_queue queue;
    pthread_mutex_init(&queue.mutex, NULL);
    pthread_cond_init(&queue.cond, NULL);
    queue.head = 0;
    queue.tail = 0;
    queue.count = 0;
    
    gettimeofday(&start, NULL);
    
    // Test enqueue/dequeue operations
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        pthread_mutex_lock(&queue.mutex);
        
        // Enqueue
        int *item = malloc(sizeof(int));
        *item = i;
        queue.items[queue.tail] = item;
        queue.tail = (queue.tail + 1) % NUM_ITERATIONS;
        queue.count++;
        
        // Immediately dequeue
        if (queue.count > 0) {
            int *dequeued = queue.items[queue.head];
            queue.head = (queue.head + 1) % NUM_ITERATIONS;
            queue.count--;
            free(dequeued);
        }
        
        pthread_mutex_unlock(&queue.mutex);
    }
    
    gettimeofday(&end, NULL);
    
    pthread_mutex_destroy(&queue.mutex);
    pthread_cond_destroy(&queue.cond);
    
    benchmark_result result = {
        .duration = get_time_diff(start, end),
        .operations = NUM_ITERATIONS * 2  // enqueue + dequeue
    };
    
    printf("  Duration: %.4f seconds\n", result.duration);
    printf("  Operations: %zu enqueue/dequeue\n", result.operations);
    printf("  Throughput: %.0f ops/sec\n", result.operations / result.duration);
    
    return result;
}

// Test 4: Message handling with memcpy operations
benchmark_result test_message_handling() {
    printf("\n[TEST 4] Testing message handling performance...\n");
    struct timeval start, end;
    
    gettimeofday(&start, NULL);
    
    for (int i = 0; i < NUM_ITERATIONS / 100; i++) {
        // Simulate message creation with multiple memcpy operations
        size_t msg_size = 1024;
        unsigned char *buffer = malloc(msg_size + 9);
        unsigned char *payload = malloc(msg_size);
        
        // Fill payload
        memset(payload, 'X', msg_size);
        
        // Simulate multiple memcpy operations (current inefficient pattern)
        buffer[0] = 0x00;
        uint64_t length = msg_size;
        memcpy(buffer + 1, &length, 8);
        memcpy(buffer + 9, payload, msg_size);
        
        // Additional unnecessary copies
        unsigned char *temp = malloc(msg_size + 9);
        memcpy(temp, buffer, msg_size + 9);
        
        free(temp);
        free(buffer);
        free(payload);
    }
    
    gettimeofday(&end, NULL);
    
    benchmark_result result = {
        .duration = get_time_diff(start, end),
        .operations = NUM_ITERATIONS / 100
    };
    
    printf("  Duration: %.4f seconds\n", result.duration);
    printf("  Operations: %zu message creations\n", result.operations);
    printf("  Throughput: %.0f msgs/sec\n", result.operations / result.duration);
    
    return result;
}

// Test 5: Multiplex list operations
benchmark_result test_multiplex_list() {
    printf("\n[TEST 5] Testing multiplex list performance...\n");
    struct timeval start, end;
    
    List *list = create();
    
    gettimeofday(&start, NULL);
    
    // Test add/find/remove operations
    for (int i = 0; i < NUM_ITERATIONS / 100; i++) {
        file_request *req = malloc(sizeof(file_request));
        req->session_id = i;
        req->offset = i * 1024;
        req->length = 1024;
        req->file_name = malloc(32);
        snprintf((char*)req->file_name, 32, "file_%d.txt", i);
        pthread_mutex_init(&req->node_lock, NULL);
        
        // Add to list
        add(&list, req);
        
        // Find in list
        file_request *found = find(&list, req);
        assert(found == req);
        
        // Remove from list
        remove_node(&list, req);
        
        free(req->file_name);
        free(req);
    }
    
    gettimeofday(&end, NULL);
    
    free(list);
    
    benchmark_result result = {
        .duration = get_time_diff(start, end),
        .operations = (NUM_ITERATIONS / 100) * 3  // add + find + remove
    };
    
    printf("  Duration: %.4f seconds\n", result.duration);
    printf("  Operations: %zu list operations\n", result.operations);
    printf("  Throughput: %.0f ops/sec\n", result.operations / result.duration);
    
    return result;
}

// Test 6: Concurrent thread stress test
typedef struct {
    int thread_id;
    int iterations;
    pthread_mutex_t *shared_mutex;
    int *shared_counter;
} thread_test_data;

void *thread_stress_worker(void *arg) {
    thread_test_data *data = (thread_test_data *)arg;
    
    for (int i = 0; i < data->iterations; i++) {
        pthread_mutex_lock(data->shared_mutex);
        (*data->shared_counter)++;
        
        // Simulate some work
        void *temp = malloc(128);
        memset(temp, 'A', 128);
        free(temp);
        
        pthread_mutex_unlock(data->shared_mutex);
        
        // Small delay to simulate real work
        usleep(1);
    }
    
    return NULL;
}

benchmark_result test_thread_concurrency() {
    printf("\n[TEST 6] Testing thread concurrency performance...\n");
    struct timeval start, end;
    
    pthread_mutex_t shared_mutex;
    pthread_mutex_init(&shared_mutex, NULL);
    int shared_counter = 0;
    
    pthread_t threads[NUM_THREADS];
    thread_test_data thread_data[NUM_THREADS];
    
    gettimeofday(&start, NULL);
    
    // Create threads
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_data[i].thread_id = i;
        thread_data[i].iterations = NUM_ITERATIONS / NUM_THREADS;
        thread_data[i].shared_mutex = &shared_mutex;
        thread_data[i].shared_counter = &shared_counter;
        
        pthread_create(&threads[i], NULL, thread_stress_worker, &thread_data[i]);
    }
    
    // Wait for threads
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    gettimeofday(&end, NULL);
    
    pthread_mutex_destroy(&shared_mutex);
    
    benchmark_result result = {
        .duration = get_time_diff(start, end),
        .operations = shared_counter
    };
    
    printf("  Duration: %.4f seconds\n", result.duration);
    printf("  Operations: %d total operations\n", shared_counter);
    printf("  Throughput: %.0f ops/sec\n", result.operations / result.duration);
    
    return result;
}

// Test 7: Large buffer handling
benchmark_result test_large_buffer_handling() {
    printf("\n[TEST 7] Testing large buffer handling...\n");
    struct timeval start, end;
    size_t initial_mem = get_memory_usage();
    
    gettimeofday(&start, NULL);
    
    for (int i = 0; i < 100; i++) {
        // Allocate large buffer
        unsigned char *large_buffer = malloc(LARGE_BUFFER_SIZE);
        if (!large_buffer) {
            printf("Failed to allocate large buffer at iteration %d\n", i);
            break;
        }
        
        // Fill buffer
        memset(large_buffer, 'B', LARGE_BUFFER_SIZE);
        
        // Simulate multiple copies (inefficient pattern)
        unsigned char *copy1 = malloc(LARGE_BUFFER_SIZE);
        memcpy(copy1, large_buffer, LARGE_BUFFER_SIZE);
        
        unsigned char *copy2 = malloc(LARGE_BUFFER_SIZE);
        memcpy(copy2, copy1, LARGE_BUFFER_SIZE);
        
        free(copy2);
        free(copy1);
        free(large_buffer);
    }
    
    gettimeofday(&end, NULL);
    
    benchmark_result result = {
        .duration = get_time_diff(start, end),
        .memory_used = get_memory_usage() - initial_mem,
        .operations = 100
    };
    
    printf("  Duration: %.4f seconds\n", result.duration);
    printf("  Operations: %zu large buffer operations\n", result.operations);
    printf("  Throughput: %.0f ops/sec\n", result.operations / result.duration);
    printf("  Peak memory delta: %zu bytes\n", result.memory_used);
    
    return result;
}

// Main test runner
int main(int argc, char **argv) {
    printf("========================================\n");
    printf("MULTIPLEXING SERVER PERFORMANCE TESTS\n");
    printf("========================================\n");
    printf("Running baseline performance tests...\n");
    printf("This will measure current (unoptimized) performance.\n");
    
    benchmark_result results[7];
    double total_time = 0;
    
    // Run all tests
    results[0] = test_memory_allocation_small();
    results[1] = test_compression_performance();
    results[2] = test_thread_pool_queue();
    results[3] = test_message_handling();
    results[4] = test_multiplex_list();
    results[5] = test_thread_concurrency();
    results[6] = test_large_buffer_handling();
    
    // Calculate totals
    for (int i = 0; i < 7; i++) {
        total_time += results[i].duration;
    }
    
    // Summary
    printf("\n========================================\n");
    printf("BASELINE PERFORMANCE SUMMARY\n");
    printf("========================================\n");
    printf("Total test duration: %.4f seconds\n", total_time);
    printf("\nTest Results:\n");
    printf("  1. Small allocations: %.4f sec (%.0f ops/sec)\n", 
           results[0].duration, results[0].operations / results[0].duration);
    printf("  2. Compression: %.4f sec (%.0f cycles/sec)\n", 
           results[1].duration, results[1].operations / results[1].duration);
    printf("  3. Thread queue: %.4f sec (%.0f ops/sec)\n", 
           results[2].duration, results[2].operations / results[2].duration);
    printf("  4. Message handling: %.4f sec (%.0f msgs/sec)\n", 
           results[3].duration, results[3].operations / results[3].duration);
    printf("  5. Multiplex list: %.4f sec (%.0f ops/sec)\n", 
           results[4].duration, results[4].operations / results[4].duration);
    printf("  6. Thread concurrency: %.4f sec (%.0f ops/sec)\n", 
           results[5].duration, results[5].operations / results[5].duration);
    printf("  7. Large buffers: %.4f sec (%.0f ops/sec)\n", 
           results[6].duration, results[6].operations / results[6].duration);
    
    printf("\n========================================\n");
    printf("Key Performance Issues Identified:\n");
    printf("========================================\n");
    printf("1. Excessive small memory allocations\n");
    printf("2. Inefficient realloc usage in compression\n");
    printf("3. Multiple unnecessary memcpy operations\n");
    printf("4. No memory pooling for frequent allocations\n");
    printf("5. Thread contention on shared mutex\n");
    printf("6. Linear search in multiplex list\n");
    printf("7. Large buffer copies instead of zero-copy\n");
    
    // Save results to file for comparison
    FILE *fp = fopen("baseline_performance.txt", "w");
    if (fp) {
        fprintf(fp, "BASELINE PERFORMANCE RESULTS\n");
        fprintf(fp, "Total Duration: %.4f\n", total_time);
        for (int i = 0; i < 7; i++) {
            fprintf(fp, "Test %d: %.4f sec, %zu ops\n", 
                    i+1, results[i].duration, results[i].operations);
        }
        fclose(fp);
        printf("\nResults saved to baseline_performance.txt\n");
    }
    
    return 0;
}