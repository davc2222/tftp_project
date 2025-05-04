// client_rrq.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>

#define OP_RRQ 1
#define OP_DATA 3
#define OP_ACK 4
#define MAX_DATA_SIZE 512

int send_rrq(int sock, struct sockaddr_in *server, const char *filename)
{
    char buffer[516];
    int pos = 0;

    // אופקוד (2 בתים)
    buffer[pos++] = 0;
    buffer[pos++] = 1; // OP_RRQ

    // שם קובץ
    while (*filename)
    {
        buffer[pos++] = *filename++;
    }
    buffer[pos++] = 0; // null-terminator אחרי שם הקובץ

    // מצב "octet"
    const char *mode = "octet";
    while (*mode)
    {
        buffer[pos++] = *mode++;
    }
    buffer[pos++] = 0; // null-terminator אחרי mode

    return sendto(sock, buffer, pos, 0, (struct sockaddr *)server, sizeof(*server));
}

void receive_file(int sock, struct sockaddr_in *server, const char *filename)
{
    char buffer[516];
    struct sockaddr_in sender;
    socklen_t addrlen = sizeof(sender);
    FILE *file = fopen(filename, "wb");
    if (!file)
    {
        perror("fopen");
        exit(1);
    }

    int expected_block = 1;

    while (1)
    {
        int n = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&sender, &addrlen);

        if (n < 4 || buffer[1] != OP_DATA) {

                continue;
        }
        

            int block = ((unsigned char)buffer[2] << 8) | (unsigned char)buffer[3];

        if (block == expected_block)
        {
            fwrite(&buffer[4], 1, n - 4, file);

            expected_block++;
            printf("Got block %d (%d bytes)\n", block, n - 4);

        }

        // Send ACK
        char ack[4] = {0, OP_ACK, buffer[2], buffer[3]};
        sendto(sock, ack, 4, 0, (struct sockaddr *)server, addrlen);

        if (n < 516)
            break; // Last block
    }

    fclose(file);
    printf("File received.\n");
}

int main()
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in server = {0};
    server.sin_family = AF_INET;
    server.sin_port = htons(6969);
    inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);

    const char *filename = "network_terminal_commands .pdf";
    send_rrq(sock, &server, filename);
    receive_file(sock, &server, filename);

    close(sock);
    return 0;
}