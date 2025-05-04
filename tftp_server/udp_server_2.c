#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/select.h>

#define OP_RRQ  1
#define OP_DATA 3
#define OP_ACK  4

#define MAX_DATA_SIZE 512
#define TIMEOUT_SEC 2
#define MAX_RETRIES 5

void handle_rrq(int sock, struct sockaddr_in *client, socklen_t client_len, const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("fopen");
        return;
    }

    char buffer[516], ack[4];
    int block = 1;
    size_t bytes;

    while (1) {
        // Build DATA packet
        buffer[0] = 0;
        buffer[1] = OP_DATA;
        buffer[2] = (block >> 8) & 0xFF;
        buffer[3] = block & 0xFF;
        bytes = fread(&buffer[4], 1, MAX_DATA_SIZE, file);

        int packet_len = bytes + 4;

        // Retry loop
        int retries = 0;
        while (retries < MAX_RETRIES) {
            sendto(sock, buffer, packet_len, 0, (struct sockaddr *)client, client_len);

            // Wait for ACK
            struct timeval timeout = {TIMEOUT_SEC, 0};
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(sock, &readfds);

            int ready = select(sock + 1, &readfds, NULL, NULL, &timeout);
            if (ready > 0) {
                struct sockaddr_in recv_addr;
                socklen_t recv_len = sizeof(recv_addr);
                int n = recvfrom(sock, ack, sizeof(ack), 0, (struct sockaddr *)&recv_addr, &recv_len);
                if (n == 4 && ack[1] == OP_ACK &&
                    ack[2] == buffer[2] && ack[3] == buffer[3]) {
                    break; // ACK received
                }
            }

            retries++;
            printf("Retrying block %d (%d/%d)\n", block, retries, MAX_RETRIES);
        }

        if (retries == MAX_RETRIES) {
            printf("Transfer failed on block %d.\n", block);
            break;
        }

        if (bytes < MAX_DATA_SIZE) break; // Last block
        block++;
    }

    fclose(file);
    printf("File '%s' sent.\n", filename);
}

int main() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); exit(1); }

    struct sockaddr_in server = {0}, client = {0};
    server.sin_family = AF_INET;
    server.sin_port = htons(6969);
    server.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("bind");
        exit(1);
    }

    printf("TFTP server running on port 6969...\n");

    while (1) {
        char buffer[516];
        socklen_t client_len = sizeof(client);
        int n = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&client, &client_len);

        if (n >= 4 && buffer[1] == OP_RRQ) {
            const char *filename = &buffer[2];
            printf("Received RRQ for file: %s\n", filename);
            handle_rrq(sock, &client, client_len, filename);
        }
    }

    close(sock);
    return 0;
}
