#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Usage: %s <output_file> <port> <directory>\n", argv[0]);
        printf("Example: %s config.bin 8080 ./files\n", argv[0]);
        return 1;
    }

    FILE *fp = fopen(argv[1], "wb");
    if (!fp) {
        perror("Failed to create config file");
        return 1;
    }

    // Write IP address (127.0.0.1 for localhost)
    uint32_t ip = inet_addr("127.0.0.1");
    fwrite(&ip, 4, 1, fp);

    // Write port (network byte order)
    uint16_t port = htons(atoi(argv[2]));
    fwrite(&port, 2, 1, fp);

    // Write directory path
    fwrite(argv[3], strlen(argv[3]), 1, fp);

    fclose(fp);
    printf("Config file '%s' created successfully\n", argv[1]);
    printf("  IP: 127.0.0.1\n");
    printf("  Port: %s\n", argv[2]);
    printf("  Directory: %s\n", argv[3]);
    
    return 0;
}