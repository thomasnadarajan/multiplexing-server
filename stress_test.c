#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include "byteswap_compat.h"

#define MAX_CLIENTS 50
#define DURATION_SECONDS 10
#define TEST_PORT 8082

typedef struct {
    int thread_id;
    int port;
    volatile int *running;
    int requests_completed;
    int errors;
    double total_latency;
    double min_latency;
    double max_latency;
} stress_client;

static double get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

// Stress test worker - hammers the server continuously
void* stress_worker(void *arg) {
    stress_client *client = (stress_client*)arg;
    client->requests_completed = 0;
    client->errors = 0;
    client->total_latency = 0;
    client->min_latency = 999999;
    client->max_latency = 0;
    
    // Create persistent connection
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        client->errors++;
        return NULL;
    }
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(client->port);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(sockfd);
        client->errors++;
        return NULL;
    }
    
    // Prepare various test payloads
    char small_payload[32];
    char medium_payload[1024];
    char large_payload[8192];
    
    memset(small_payload, 'S', sizeof(small_payload));
    memset(medium_payload, 'M', sizeof(medium_payload));
    memset(large_payload, 'L', sizeof(large_payload));
    
    while (*client->running) {
        double start = get_time();
        
        // Send echo request with varying sizes
        int size_choice = client->requests_completed % 3;
        void *payload;
        size_t payload_size;
        
        switch (size_choice) {
            case 0:
                payload = small_payload;
                payload_size = sizeof(small_payload);
                break;
            case 1:
                payload = medium_payload;
                payload_size = sizeof(medium_payload);
                break;
            case 2:
                payload = large_payload;
                payload_size = sizeof(large_payload);
                break;
        }
        
        // Send header
        uint8_t header = 0x00;  // Echo type, no compression
        if (send(sockfd, &header, 1, 0) != 1) {
            client->errors++;
            break;
        }
        
        // Send length
        uint64_t length_be = bswap_64(payload_size);
        if (send(sockfd, &length_be, 8, 0) != 8) {
            client->errors++;
            break;
        }
        
        // Send payload
        if (send(sockfd, payload, payload_size, 0) != payload_size) {
            client->errors++;
            break;
        }
        
        // Receive response
        uint8_t resp_header;
        if (recv(sockfd, &resp_header, 1, 0) != 1) {
            client->errors++;
            break;
        }
        
        uint64_t resp_length_be;
        if (recv(sockfd, &resp_length_be, 8, 0) != 8) {
            client->errors++;
            break;
        }
        
        uint64_t resp_length = bswap_64(resp_length_be);
        char *response = malloc(resp_length);
        if (response) {
            if (recv(sockfd, response, resp_length, 0) != resp_length) {
                client->errors++;
                free(response);
                break;
            }
            free(response);
        }
        
        double latency = (get_time() - start) * 1000;  // Convert to ms
        client->total_latency += latency;
        if (latency < client->min_latency) client->min_latency = latency;
        if (latency > client->max_latency) client->max_latency = latency;
        
        client->requests_completed++;
    }
    
    close(sockfd);
    return NULL;
}

void run_stress_test(const char *server_name, const char *server_path, const char *config_path) {
    printf("\n========================================\n");
    printf("STRESS TESTING: %s\n", server_name);
    printf("========================================\n");
    printf("Duration: %d seconds\n", DURATION_SECONDS);
    printf("Max concurrent clients: %d\n\n", MAX_CLIENTS);
    
    // Start server
    pid_t server_pid = fork();
    if (server_pid == 0) {
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
        execl(server_path, server_path, config_path, NULL);
        exit(1);
    }
    
    sleep(1);  // Let server start
    
    volatile int running = 1;
    pthread_t threads[MAX_CLIENTS];
    stress_client clients[MAX_CLIENTS];
    
    printf("Starting stress test...\n");
    double start_time = get_time();
    
    // Start all client threads
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].thread_id = i;
        clients[i].port = TEST_PORT;
        clients[i].running = &running;
        pthread_create(&threads[i], NULL, stress_worker, &clients[i]);
    }
    
    // Run for specified duration
    sleep(DURATION_SECONDS);
    running = 0;
    
    // Wait for all threads
    for (int i = 0; i < MAX_CLIENTS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    double total_time = get_time() - start_time;
    
    // Calculate statistics
    int total_requests = 0;
    int total_errors = 0;
    double total_latency = 0;
    double min_latency = 999999;
    double max_latency = 0;
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        total_requests += clients[i].requests_completed;
        total_errors += clients[i].errors;
        total_latency += clients[i].total_latency;
        if (clients[i].min_latency < min_latency) min_latency = clients[i].min_latency;
        if (clients[i].max_latency > max_latency) max_latency = clients[i].max_latency;
    }
    
    printf("\n--- STRESS TEST RESULTS ---\n");
    printf("Total requests: %d\n", total_requests);
    printf("Total errors: %d\n", total_errors);
    printf("Success rate: %.2f%%\n", 
           (total_requests - total_errors) * 100.0 / total_requests);
    printf("Throughput: %.0f req/sec\n", total_requests / total_time);
    printf("Average latency: %.2f ms\n", total_latency / total_requests);
    printf("Min latency: %.2f ms\n", min_latency);
    printf("Max latency: %.2f ms\n", max_latency);
    printf("Requests per client: %.1f\n", (double)total_requests / MAX_CLIENTS);
    
    // Kill server
    kill(server_pid, SIGTERM);
    waitpid(server_pid, NULL, 0);
}

int main() {
    printf("========================================\n");
    printf("MULTIPLEXING SERVER STRESS TEST\n");
    printf("========================================\n");
    printf("This test pushes servers to their limits\n");
    printf("with sustained high-concurrency load.\n");
    
    // Setup
    system("mkdir -p files");
    system("echo 'test data' > files/test.txt");
    
    // Create config
    FILE *fp = fopen("stress_config.bin", "wb");
    if (fp) {
        uint32_t ip = inet_addr("127.0.0.1");
        fwrite(&ip, 4, 1, fp);
        uint16_t port = htons(TEST_PORT);
        fwrite(&port, 2, 1, fp);
        const char *dir = "./files";
        fwrite(dir, strlen(dir), 1, fp);
        fclose(fp);
    }
    
    // Build servers
    printf("\nBuilding servers...\n");
    system("make server 2>/dev/null");
    system("make server_optimized 2>/dev/null");
    
    // Test original server
    if (access("./server", X_OK) == 0) {
        run_stress_test("ORIGINAL SERVER", "./server", "stress_config.bin");
    } else {
        printf("\nOriginal server not found, skipping...\n");
    }
    
    // Test optimized server
    if (access("./server_optimized", X_OK) == 0) {
        run_stress_test("OPTIMIZED SERVER", "./server_optimized", "stress_config.bin");
    } else {
        printf("\nOptimized server not found, skipping...\n");
    }
    
    printf("\n========================================\n");
    printf("STRESS TEST COMPLETE\n");
    printf("========================================\n");
    printf("The optimized server should show:\n");
    printf("- Higher throughput under load\n");
    printf("- Lower average latency\n");
    printf("- More consistent performance\n");
    printf("- Better resource utilization\n\n");
    
    // Cleanup
    system("rm -f stress_config.bin");
    
    return 0;
}