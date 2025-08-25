#!/bin/bash

echo "========================================="
echo "OPTIMIZED SERVER PERFORMANCE BENCHMARK"
echo "========================================="
echo ""
echo "This benchmark tests the optimized server with:"
echo "- Circular queue for thread pool (no dynamic allocation)"
echo "- Memory pooling for frequent allocations"
echo "- Optimized socket options (TCP_NODELAY, larger buffers)"
echo "- Larger listen backlog (1024 vs 500)"
echo "- 20 worker threads with efficient queueing"
echo ""

# Setup test environment
echo "Setting up test environment..."
mkdir -p files
echo "test data for benchmark" > files/test.txt
echo "ABCDEFGHIJKLMNOPQRSTUVWXYZ" > files/test1.txt
echo "1234567890" > files/test2.txt

# Create config file
cat > optimized_config.bin << 'EOF'
EOF
python3 -c "
import struct
import socket
# Write IP (127.0.0.1)
ip = socket.inet_aton('127.0.0.1')
# Write port (8085) in network byte order
port = struct.pack('!H', 8085)
# Write directory
directory = b'./files'
with open('optimized_config.bin', 'wb') as f:
    f.write(ip)
    f.write(port)
    f.write(directory)
" 2>/dev/null || {
    # Fallback if python3 not available
    printf '\x7f\x00\x00\x01' > optimized_config.bin  # 127.0.0.1
    printf '\x1f\x95' >> optimized_config.bin          # port 8085
    printf './files' >> optimized_config.bin           # directory
}

# Build optimized server if needed
if [ ! -f ./server_optimized_standalone ]; then
    echo "Building optimized server..."
    make server_optimized_standalone > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        echo "Failed to build optimized server"
        exit 1
    fi
fi

echo "✓ Optimized server ready"
echo ""

# Start optimized server in background
echo "Starting optimized server on port 8085..."
./server_optimized_standalone optimized_config.bin > /dev/null 2>&1 &
SERVER_PID=$!
sleep 1

# Check if server started
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "Failed to start optimized server"
    exit 1
fi

echo "✓ Server started (PID: $SERVER_PID)"
echo ""

# Run stress test
echo "========================================="
echo "RUNNING STRESS TEST"
echo "========================================="
echo "Duration: 10 seconds"
echo "Concurrent clients: 50"
echo ""

# Create inline stress test
cat > /tmp/stress_opt.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/time.h>
#include <stdint.h>

#define NUM_CLIENTS 50
#define TEST_DURATION 10
#define PORT 8085

typedef struct {
    int id;
    volatile int *running;
    int requests;
    int errors;
    double total_latency;
} client_data;

uint64_t bswap_64(uint64_t x) {
    return ((x & 0xff00000000000000ull) >> 56) |
           ((x & 0x00ff000000000000ull) >> 40) |
           ((x & 0x0000ff0000000000ull) >> 24) |
           ((x & 0x000000ff00000000ull) >> 8)  |
           ((x & 0x00000000ff000000ull) << 8)  |
           ((x & 0x0000000000ff0000ull) << 24) |
           ((x & 0x000000000000ff00ull) << 40) |
           ((x & 0x00000000000000ffull) << 56);
}

double get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

void* client_worker(void *arg) {
    client_data *data = (client_data*)arg;
    
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return NULL;
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(sockfd);
        data->errors++;
        return NULL;
    }
    
    char payload[256];
    memset(payload, 'T', sizeof(payload));
    
    while (*data->running) {
        double start = get_time();
        
        // Send echo request
        uint8_t header = 0x00;
        send(sockfd, &header, 1, 0);
        
        uint64_t len = bswap_64(sizeof(payload));
        send(sockfd, &len, 8, 0);
        send(sockfd, payload, sizeof(payload), 0);
        
        // Receive response
        uint8_t resp_header;
        uint64_t resp_len;
        char resp_data[256];
        
        if (recv(sockfd, &resp_header, 1, 0) == 1 &&
            recv(sockfd, &resp_len, 8, 0) == 8 &&
            recv(sockfd, resp_data, bswap_64(resp_len), 0) > 0) {
            data->requests++;
            data->total_latency += (get_time() - start) * 1000;
        } else {
            data->errors++;
            break;
        }
    }
    
    close(sockfd);
    return NULL;
}

int main() {
    volatile int running = 1;
    pthread_t threads[NUM_CLIENTS];
    client_data clients[NUM_CLIENTS];
    
    printf("Starting %d concurrent clients...\n", NUM_CLIENTS);
    
    for (int i = 0; i < NUM_CLIENTS; i++) {
        clients[i].id = i;
        clients[i].running = &running;
        clients[i].requests = 0;
        clients[i].errors = 0;
        clients[i].total_latency = 0;
        pthread_create(&threads[i], NULL, client_worker, &clients[i]);
    }
    
    sleep(TEST_DURATION);
    running = 0;
    
    for (int i = 0; i < NUM_CLIENTS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    int total_requests = 0;
    int total_errors = 0;
    double total_latency = 0;
    
    for (int i = 0; i < NUM_CLIENTS; i++) {
        total_requests += clients[i].requests;
        total_errors += clients[i].errors;
        total_latency += clients[i].total_latency;
    }
    
    printf("\n--- RESULTS ---\n");
    printf("Total requests: %d\n", total_requests);
    printf("Total errors: %d\n", total_errors);
    printf("Throughput: %.0f req/sec\n", (double)total_requests / TEST_DURATION);
    if (total_requests > 0) {
        printf("Avg latency: %.2f ms\n", total_latency / total_requests);
    }
    printf("Success rate: %.1f%%\n", 
           total_requests > 0 ? (total_requests * 100.0 / (total_requests + total_errors)) : 0);
    
    return 0;
}
EOF

gcc -pthread -o /tmp/stress_opt /tmp/stress_opt.c
/tmp/stress_opt

echo ""
echo "========================================="
echo "OPTIMIZED SERVER PERFORMANCE SUMMARY"
echo "========================================="
echo ""
echo "Key Optimizations Applied:"
echo "✓ Circular queue (no linked list overhead)"
echo "✓ Memory pooling (reduced allocation overhead)"
echo "✓ TCP_NODELAY (reduced latency)"
echo "✓ Larger socket buffers (65KB)"
echo "✓ Larger listen backlog (1024)"
echo ""
echo "Expected improvements over baseline:"
echo "- Higher throughput (>200K req/sec possible)"
echo "- Lower average latency (<0.25ms possible)"
echo "- Better scalability with concurrent clients"
echo "- More consistent performance under load"
echo ""

# Cleanup
echo "Cleaning up..."
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null
rm -f optimized_config.bin /tmp/stress_opt /tmp/stress_opt.c

echo "✓ Benchmark complete"