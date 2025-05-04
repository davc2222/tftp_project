
// rrq_client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define SERVER_PORT 6969
#define SERVER_IP "127.0.0.1"
#define BUFFER_SIZE 516 // 2 + filename + 1 + mode + 1

void send_rrq(int sockfd, struct sockaddr_in *server_addr, const char *filename)
{
    char buffer[BUFFER_SIZE];
    int len = 0;

    // Opcode: RRQ = 1
    buffer[len++] = 0;
    buffer[len++] = 1;

    // Filename
    strcpy(&buffer[len], filename);
    len += strlen(filename) + 1;

    // Mode: "octet"
    strcpy(&buffer[len], "octet");
    len += strlen("octet") + 1;

    sendto(sockfd, buffer, len, 0, (struct sockaddr *)server_addr, sizeof(*server_addr));
}

int main()
{
    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    // שליחת בקשת RRQ
    char filename[] = "file.txt";
    send_rrq(sockfd, &server_addr, filename);

    FILE *out = fopen(filename, "wb"); // שמירה בשם שנשלח

    if (!out)
    {
        perror("fopen");
        exit(1);
    }

    while (1)
    {
        int n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, NULL, NULL);
        if (n <= 4)
            break; // כל בלוק מכיל לפחות 4 בתים: opcode + block #
        fwrite(&buffer[4], 1, n - 4, out);
        if (n < 516)
            break; // סוף הקובץ (פחות מ־512 בתים)
    }

    fclose(out);
    close(sockfd);
    return 0;
}
