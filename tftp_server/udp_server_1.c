// rrq_server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 6969
#define BUFFER_SIZE 516

void send_data_block(int sockfd, struct sockaddr_in *client_addr, socklen_t addr_len,
                     FILE *file, uint16_t block_num) {
    char buffer[BUFFER_SIZE];
    int len = 0;

    buffer[len++] = 0;
    buffer[len++] = 3; // Opcode DATA
    buffer[len++] = (block_num >> 8) & 0xFF;
    buffer[len++] = block_num & 0xFF;

    int bytes_read = fread(&buffer[len], 1, 512, file);
    len += bytes_read;

    sendto(sockfd, buffer, len, 0, (struct sockaddr*)client_addr, addr_len);
}

int main() {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    char buffer[BUFFER_SIZE];
    socklen_t addr_len = sizeof(client_addr);

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr));

    printf("TFTP server ready on port %d...\n", PORT);

    while (1) {
        int n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0,
                         (struct sockaddr*)&client_addr, &addr_len);
        if (n >= 4 && buffer[1] == 1) { // RRQ
            char *filename = &buffer[2];
            printf("RRQ for file: %s\n", filename);

            FILE *file = fopen(filename, "rb");
            if (!file) {
                perror("File open failed");
                continue;
            }

            uint16_t block = 1;
            do {
                send_data_block(sockfd, &client_addr, addr_len, file, block++);
            } while (!feof(file));

            fclose(file);
        }
    }

    close(sockfd);
    return 0;
}
