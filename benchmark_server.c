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
#include <fcntl.h>
#include <errno.h>
#include "byteswap_compat.h"

#define NUM_CLIENTS 10
#define REQUESTS_PER_CLIENT 100
#define TEST_PORT_ORIGINAL 8080
#define TEST_PORT_OPTIMIZED 8081
#define TEST_FILE_SIZE 1024

typedef struct {
    int client_id;
    int port;
    int num_requests;
    double duration;
    int success_count;
    int use_compression;
} client_data;

static double get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

// Create test configuration file
void create_test_config(const char *filename, int port) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        perror("Failed to create config");
        exit(1);
    }
    
    // Write IP (127.0.0.1 in network byte order)
    uint32_t ip = inet_addr("127.0.0.1");
    fwrite(&ip, 4, 1, fp);
    
    // Write port in network byte order
    uint16_t net_port = htons(port);
    fwrite(&net_port, 2, 1, fp);
    
    // Write directory
    const char *dir = "./files";
    fwrite(dir, strlen(dir), 1, fp);
    
    fclose(fp);
}

// Create test files
void setup_test_files() {
    system("mkdir -p files");
    
    // Create test files of various sizes
    const char *files[] = {"test1.txt", "test2.txt", "test3.txt"};
    const int sizes[] = {1024, 2048, 4096};
    
    for (int i = 0; i < 3; i++) {
        char path[256];
        snprintf(path, sizeof(path), "files/%s", files[i]);
        
        FILE *fp = fopen(path, "w");
        if (fp) {
            for (int j = 0; j < sizes[i]; j++) {
                fputc('A' + (j % 26), fp);
            }
            fclose(fp);
        }
    }
}

// Send message to server
int send_message(int sockfd, uint8_t type, uint8_t compression, 
                 uint8_t requires_compression, const void *payload, size_t payload_len) {
    uint8_t header = (type << 4) | (compression << 3) | (requires_compression << 2);
    
    if (send(sockfd, &header, 1, 0) != 1) return -1;
    
    uint64_t length_be = bswap_64(payload_len);
    if (send(sockfd, &length_be, 8, 0) != 8) return -1;
    
    if (payload_len > 0) {
        if (send(sockfd, payload, payload_len, 0) != payload_len) return -1;
    }
    
    return 0;
}

// Receive response from server
int receive_response(int sockfd, void *buffer, size_t max_len) {
    uint8_t header;
    if (recv(sockfd, &header, 1, 0) != 1) return -1;
    
    uint64_t length_be;
    if (recv(sockfd, &length_be, 8, 0) != 8) return -1;
    
    uint64_t length = bswap_64(length_be);
    if (length > max_len) return -1;
    
    if (length > 0) {
        if (recv(sockfd, buffer, length, 0) != length) return -1;
    }
    
    return length;
}

// Test echo functionality
int test_echo(int sockfd, int use_compression) {
    const char *test_data = "Hello, this is a test message for echo functionality!";
    size_t data_len = strlen(test_data);
    
    if (send_message(sockfd, 0x0, 0, use_compression, test_data, data_len) < 0) {
        return -1;
    }
    
    char response[1024];
    int resp_len = receive_response(sockfd, response, sizeof(response));
    if (resp_len < 0) return -1;
    
    // For compressed responses, we'd need to decompress, but for now just check we got something
    return (resp_len > 0) ? 0 : -1;
}

// Test directory listing
int test_directory(int sockfd, int use_compression) {
    if (send_message(sockfd, 0x2, 0, use_compression, NULL, 0) < 0) {
        return -1;
    }
    
    char response[4096];
    int resp_len = receive_response(sockfd, response, sizeof(response));
    return (resp_len > 0) ? 0 : -1;
}

// Test file size query
int test_file_size(int sockfd, int use_compression) {
    const char *filename = "test1.txt";
    
    if (send_message(sockfd, 0x4, 0, use_compression, filename, strlen(filename) + 1) < 0) {
        return -1;
    }
    
    char response[1024];
    int resp_len = receive_response(sockfd, response, sizeof(response));
    return (resp_len > 0) ? 0 : -1;
}

// Test file retrieval (multiplexing)
int test_file_retrieval(int sockfd, int use_compression) {
    // Prepare file request
    struct {
        uint32_t session_id;
        uint64_t offset;
        uint64_t length;
        char filename[32];
    } request;
    
    request.session_id = bswap_32(1234);
    request.offset = bswap_64(0);
    request.length = bswap_64(1024);
    strcpy(request.filename, "test1.txt");
    
    size_t req_len = 20 + strlen(request.filename) + 1;
    
    if (send_message(sockfd, 0x6, 0, use_compression, &request, req_len) < 0) {
        return -1;
    }
    
    char response[4096];
    int resp_len = receive_response(sockfd, response, sizeof(response));
    return (resp_len > 0) ? 0 : -1;
}

// Client worker thread
void* client_worker(void *arg) {
    client_data *data = (client_data*)arg;
    
    double start_time = get_time();
    data->success_count = 0;
    
    for (int i = 0; i < data->num_requests; i++) {
        // Create new connection for each request (simulates real usage)
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) continue;
        
        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(data->port);
        server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        
        if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            close(sockfd);
            continue;
        }
        
        // Test different message types
        int test_type = i % 4;
        int success = 0;
        
        switch (test_type) {
            case 0:
                success = (test_echo(sockfd, data->use_compression) == 0);
                break;
            case 1:
                success = (test_directory(sockfd, data->use_compression) == 0);
                break;
            case 2:
                success = (test_file_size(sockfd, data->use_compression) == 0);
                break;
            case 3:
                success = (test_file_retrieval(sockfd, data->use_compression) == 0);
                break;
        }
        
        if (success) data->success_count++;
        
        close(sockfd);
    }
    
    data->duration = get_time() - start_time;
    return NULL;
}

// Run benchmark against a server
void benchmark_server(int port, const char *server_name, int use_compression) {
    printf("\n========================================\n");
    printf("BENCHMARKING: %s (port %d)\n", server_name, port);
    printf("========================================\n");
    printf("Clients: %d, Requests per client: %d\n", NUM_CLIENTS, REQUESTS_PER_CLIENT);
    printf("Compression: %s\n\n", use_compression ? "ENABLED" : "DISABLED");
    
    pthread_t threads[NUM_CLIENTS];
    client_data clients[NUM_CLIENTS];
    
    // Let server fully start
    sleep(1);
    
    double start_time = get_time();
    
    // Start client threads
    for (int i = 0; i < NUM_CLIENTS; i++) {
        clients[i].client_id = i;
        clients[i].port = port;
        clients[i].num_requests = REQUESTS_PER_CLIENT;
        clients[i].use_compression = use_compression;
        
        pthread_create(&threads[i], NULL, client_worker, &clients[i]);
    }
    
    // Wait for all clients
    for (int i = 0; i < NUM_CLIENTS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    double total_time = get_time() - start_time;
    
    // Calculate statistics
    int total_success = 0;
    double total_client_time = 0;
    
    for (int i = 0; i < NUM_CLIENTS; i++) {
        total_success += clients[i].success_count;
        total_client_time += clients[i].duration;
        printf("Client %d: %d/%d successful (%.2f sec)\n", 
               i, clients[i].success_count, clients[i].num_requests, clients[i].duration);
    }
    
    printf("\n--- RESULTS ---\n");
    printf("Total requests: %d\n", NUM_CLIENTS * REQUESTS_PER_CLIENT);
    printf("Successful: %d (%.1f%%)\n", total_success, 
           (total_success * 100.0) / (NUM_CLIENTS * REQUESTS_PER_CLIENT));
    printf("Total time: %.4f seconds\n", total_time);
    printf("Throughput: %.0f requests/sec\n", total_success / total_time);
    printf("Avg latency: %.2f ms\n", (total_client_time / total_success) * 1000);
}

// Start server in background
pid_t start_server(const char *executable, const char *config) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child process - run server
        freopen("/dev/null", "w", stdout);  // Suppress server output
        freopen("/dev/null", "w", stderr);
        execl(executable, executable, config, NULL);
        exit(1);  // If exec fails
    }
    return pid;
}

int main(int argc, char **argv) {
    printf("========================================\n");
    printf("END-TO-END SERVER PERFORMANCE BENCHMARK\n");
    printf("========================================\n");
    printf("This test runs actual servers and measures\n");
    printf("real network performance with socket I/O.\n\n");
    
    // Setup test environment
    printf("Setting up test environment...\n");
    setup_test_files();
    create_test_config("config_original.bin", TEST_PORT_ORIGINAL);
    create_test_config("config_optimized.bin", TEST_PORT_OPTIMIZED);
    
    // Build servers if needed
    printf("Building servers...\n");
    system("make server 2>/dev/null");
    system("make server_optimized 2>/dev/null");
    
    // Test 1: Original server without compression
    printf("\n--- TEST 1: ORIGINAL SERVER (NO COMPRESSION) ---\n");
    pid_t pid_orig = start_server("./server", "config_original.bin");
    if (pid_orig > 0) {
        benchmark_server(TEST_PORT_ORIGINAL, "Original Server", 0);
        kill(pid_orig, SIGTERM);
        waitpid(pid_orig, NULL, 0);
    }
    
    // Test 2: Original server with compression
    printf("\n--- TEST 2: ORIGINAL SERVER (WITH COMPRESSION) ---\n");
    pid_t pid_orig_comp = start_server("./server", "config_original.bin");
    if (pid_orig_comp > 0) {
        benchmark_server(TEST_PORT_ORIGINAL, "Original Server + Compression", 1);
        kill(pid_orig_comp, SIGTERM);
        waitpid(pid_orig_comp, NULL, 0);
    }
    
    // Test 3: Optimized server without compression
    printf("\n--- TEST 3: OPTIMIZED SERVER (NO COMPRESSION) ---\n");
    pid_t pid_opt = start_server("./server_optimized", "config_optimized.bin");
    if (pid_opt > 0) {
        benchmark_server(TEST_PORT_OPTIMIZED, "Optimized Server", 0);
        kill(pid_opt, SIGTERM);
        waitpid(pid_opt, NULL, 0);
    }
    
    // Test 4: Optimized server with compression
    printf("\n--- TEST 4: OPTIMIZED SERVER (WITH COMPRESSION) ---\n");
    pid_t pid_opt_comp = start_server("./server_optimized", "config_optimized.bin");
    if (pid_opt_comp > 0) {
        benchmark_server(TEST_PORT_OPTIMIZED, "Optimized Server + Compression", 1);
        kill(pid_opt_comp, SIGTERM);
        waitpid(pid_opt_comp, NULL, 0);
    }
    
    printf("\n========================================\n");
    printf("BENCHMARK COMPLETE\n");
    printf("========================================\n");
    printf("The optimized server should show:\n");
    printf("- Higher throughput (requests/sec)\n");
    printf("- Lower latency\n");
    printf("- Better compression performance\n");
    printf("- More stable under load\n\n");
    
    // Cleanup
    system("rm -f config_original.bin config_optimized.bin");
    
    return 0;
}