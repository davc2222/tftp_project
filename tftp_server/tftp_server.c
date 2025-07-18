/**
 * @file tftp_server.c
 * @brief TFTP Server implementation supporting RRQ (read), WRQ (write), and DELETE operations.
 *
 * This TFTP server listens on a fixed port and handles TFTP requests from clients using the UDP protocol.
 * It supports:
 *   - Read requests (RRQ): Sends files to the client with CRC-8 validation and ACK-based reliability.
 *   - Write requests (WRQ): Receives files from clients with CRC-8 validation and sends ACKs.
 *   - Delete requests: Deletes a file and sends confirmation or failure.
 *   - CRC-8 error detection for data blocks to ensure data integrity.
 *   - Dynamic port binding for each data transfer session (per client).
 *   - Backup creation for uploaded files under the "backup" folder.
 *   - Ping support (client sends "__ping__" RRQ and receives a single dummy DATA block).
 *    -The server is robust against missing ACKs or CRC mismatches and supports retransmission retries.
 */

 #include "tftp_server.h"
 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <unistd.h>
 #include <arpa/inet.h>
 #include <sys/socket.h>
 #include <sys/time.h>
 #include <sys/stat.h>
 #include <errno.h>
 
 /**
  * @brief Calculate CRC-8 checksum over a data buffer.
  * 
  * @param data Pointer to input data buffer.
  * @param len Length of the input buffer.
  * @return uint8_t The computed CRC-8 value.
  */
 uint8_t calculate_crc8(const uint8_t *data, size_t len) {
     uint8_t crc = 0;
     for (size_t i = 0; i < len; ++i) {
         crc ^= data[i];
         for (int j = 0; j < 8; ++j)
             crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1);
     }
     return crc;
 }
 
 /**
  * @brief Send an ERROR packet to the client with a specific error message.
  * 
  * @param sock Socket to send from.
  * @param client Client's address.
  * @param client_len Length of client's address struct.
  * @param error_code Numeric error code (TFTP standard or custom).
  * @param msg Human-readable error message.
  */
 void send_error(int sock, struct sockaddr_in *client, socklen_t client_len, int error_code, const char *msg) {
     unsigned char buffer[MAX_PACKET_SIZE];
     int len = 0;
     buffer[len++] = 0;
     buffer[len++] = OP_ERROR;
     buffer[len++] = 0;
     buffer[len++] = error_code;
 
     while (*msg) buffer[len++] = *msg++;  // Copy message to buffer
     buffer[len++] = 0; // Null terminator
     sendto(sock, buffer, len, 0, (struct sockaddr *)client, client_len);
 }
 
 /**
  * @brief Creates a backup copy of a file inside the "backup" folder.
  * 
  * @param filename Name of the file to back up.
  */
 void backup_file(const char *filename) {
     char backup_dir[] = "backup";
 
     // Create backup directory if it doesn't exist
     struct stat st = {0}; // reset struct
     // if DIR not exist so create
     if (stat(backup_dir, &st) == -1) {

        // creat DIR with permission setting
         if (mkdir(backup_dir, 0755) != 0) {
             perror("Failed to create BACKUP directory");
             return;
         }
     }
 
     char backup_path[512];
     snprintf(backup_path, sizeof(backup_path), "%s/%s", backup_dir, filename);
 
     // open source file
     FILE *src = fopen(filename, "rb");
     if (!src) {
         perror("Backup: cannot open source file");
         return;
     }

    // open destenation file
     FILE *dst = fopen(backup_path, "wb");
     if (!dst) {
         perror("Backup: cannot open backup file");
         fclose(src);
         return;
     }
    // copy the file for backup in 1024
    // bytes parts
     char buf[1024];
     size_t n;
     while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
         fwrite(buf, 1, n, dst);
     }
     // close the files
     fclose(src);
     fclose(dst);
     printf("Backup created: %s\n", backup_path);
 }
 
/**
 * @brief Handle WRQ (Write Request) from client: receive and store file (upload).
 * 
 * @param listen_sock Listening socket used to receive WRQ.
 * @param client Pointer to client's socket address.
 * @param client_len Length of client's socket address.
 * @param filename The name of the file to store.
 */
void handle_wrq(int listen_sock, struct sockaddr_in *client, socklen_t client_len, char *filename) {
    // Create new UDP socket for data transfer
    int data_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (data_sock < 0) {
        perror("socket");
        return;
    }

    // Initialize address structure for binding
    struct sockaddr_in data_addr = {0};
    data_addr.sin_family = AF_INET;
    data_addr.sin_addr.s_addr = INADDR_ANY;
    data_addr.sin_port = 0;  // Let system choose dynamic port

    // Bind socket to dynamic port
    if (bind(data_sock, (struct sockaddr *)&data_addr, sizeof(data_addr)) < 0) {
        perror("bind");
        close(data_sock);
        return;
    }

    // Open file for writing binary data
    FILE *file = fopen(filename, "wb");
    if (!file) {
        send_error(listen_sock, client, client_len, 2, "Cannot create file");
        close(data_sock);
        return;
    }

    // Send initial ACK(0) to confirm WRQ acceptance
    unsigned char ack[4] = {0, OP_ACK, 0, 0};
    sendto(data_sock, ack, 4, 0, (struct sockaddr *)client, client_len);

    unsigned char buffer[MAX_PACKET_SIZE];

    // Copy client address for communication on dynamic port
    struct sockaddr_in client_addr = *client;
    socklen_t client_addr_len = client_len;

    int last_block = 0;  // Track last accepted block number

    // Set 3-second timeout for receiving data packets
    struct timeval timeout = {3, 0};
    setsockopt(data_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    while (1) {
        int retries = 3;

        int n;
        // Retry loop: receive DATA block with retries if no data received
        while (retries-- > 0) {
            n = recvfrom(data_sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &client_addr_len);

            if (n >= 0) {
                // Packet received, exit retry loop
                break;
            }

            // Timeout or error: wait 200ms before retrying
            usleep(200000);
        }

        if (n < 0) {
            printf("Timeout waiting for DATA block %d\n", last_block + 1);
            break;
        }

        // Extract block number from DATA packet
        int recv_block = (buffer[2] << 8) | buffer[3];

        // Validate CRC8 of received data
        uint8_t received_crc = buffer[n - 1];
        uint8_t calc_crc = calculate_crc8(&buffer[4], n - 5);

        if (received_crc != calc_crc) {
            printf("CRC mismatch on block %d\n", recv_block);
            // Ignore this packet, wait for resend
            continue;
        }

        // Accept only next expected block (discard duplicates/out-of-order)
        if (recv_block == last_block + 1) {
            // Write data payload to file (excluding 4-byte header + CRC byte)
            fwrite(&buffer[4], 1, n - 5, file);
            last_block = recv_block;
        }

        // Send ACK for the last valid block received
        ack[0] = 0;
        ack[1] = OP_ACK;
        ack[2] = buffer[2];
        ack[3] = buffer[3];
        sendto(data_sock, ack, 4, 0, (struct sockaddr *)&client_addr, client_addr_len);

        // If data length < 512, this is last block, finish transfer
        if (n - 5 < MAX_DATA_SIZE) break;
    }

    fclose(file);
    backup_file(filename);
    close(data_sock);
    printf("Received and saved '%s'\n", filename);
}

 
 /**
 * @brief Handle RRQ (Read Request) from client: send file contents (download).
 * 
 * @param listen_sock Listening socket used to receive RRQ.
 * @param client Pointer to client's socket address.
 * @param client_len Length of client's socket address.
 * @param filename The name of the file to send.
 */
void handle_rrq(int listen_sock, struct sockaddr_in *client, socklen_t client_len, char *filename) {
    // Create new UDP socket for data transfer
    int data_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (data_sock < 0) {
        perror("socket");
        return;
    }

    struct sockaddr_in data_addr = {0}; // Initialize address struct with zeros
    data_addr.sin_family = AF_INET;     // IPv4
    data_addr.sin_addr.s_addr = INADDR_ANY; // Accept any IP
    data_addr.sin_port = 0;             // System chooses a dynamic port

    // Bind socket to dynamic port
    if (bind(data_sock, (struct sockaddr *)&data_addr, sizeof(data_addr)) < 0) {
        perror("bind");
        close(data_sock);
        return;
    }

    // Handle "__ping__" special request with a dummy DATA packet
    if (strcmp(filename, "__ping__") == 0) {
        unsigned char ping_data[] = {0, OP_DATA, 0, 1, 0}; // DATA block #1 with 0 data bytes and CRC 0
        sendto(data_sock, ping_data, sizeof(ping_data), 0, (struct sockaddr *)client, client_len);
        close(data_sock);
        return;
    }

    FILE *file = fopen(filename, "rb");
    if (!file) {
        send_error(listen_sock, client, client_len, 1, "File not found");
        close(data_sock);
        return;
    }

    unsigned char buffer[MAX_PACKET_SIZE], ack[4];
    int block = 1;
    struct sockaddr_in client_addr = *client;
    socklen_t client_addr_len = client_len;

    // Set 1-second timeout for receiving ACK packets
    struct timeval timeout = {1, 0}; 
    setsockopt(data_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    while (1) {
        // Read up to 512 bytes from file into buffer starting at offset 4
        int bytes = fread(&buffer[4], 1, MAX_DATA_SIZE, file);

        // Prepare DATA packet header
        buffer[0] = 0;
        buffer[1] = OP_DATA;
        buffer[2] = (block >> 8) & 0xFF;
        buffer[3] = block & 0xFF;

        // Append CRC8 of data
        buffer[bytes + 4] = calculate_crc8(&buffer[4], bytes);

        int retries = 3;
        while (retries-- > 0) {
            // Send DATA packet
            sendto(data_sock, buffer, bytes + 5, 0, (struct sockaddr *)&client_addr, client_addr_len);

            // Wait for ACK with timeout
            socklen_t len = sizeof(client_addr);
            int n = recvfrom(data_sock, ack, sizeof(ack), 0, (struct sockaddr *)&client_addr, &len);

            if (n >= 4 && ack[1] == OP_ACK && ack[2] == buffer[2] && ack[3] == buffer[3]) {
                // Valid ACK received for current block
                client_addr_len = len; // Update client address length if changed
                break;
            }
        }

        if (retries < 0) {
            printf("No ACK for block %d, aborting.\n", block);
            break;
        }

        // If last block is exactly 512 bytes, send final empty DATA block
        if (bytes < MAX_DATA_SIZE) {
            if (bytes == MAX_DATA_SIZE) {
                block++;
                buffer[0] = 0;
                buffer[1] = OP_DATA;
                buffer[2] = (block >> 8) & 0xFF;
                buffer[3] = block & 0xFF;
                buffer[4] =calculate_crc8(NULL, 0); // CRC of empty data

                retries = 3;
                while (retries-- > 0) {
                    sendto(data_sock, buffer, 5, 0, (struct sockaddr *)&client_addr, client_addr_len);

                    // Reset timeout for recvfrom
                    setsockopt(data_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

                    int n = recvfrom(data_sock, ack, sizeof(ack), 0, (struct sockaddr *)&client_addr, &client_addr_len);
                    if (n >= 4 && ack[1] == OP_ACK && ack[2] == buffer[2] && ack[3] == buffer[3]) {
                        break; // ACK received for final empty block
                    }
                }
            }
            break; // Finished sending file
        }

        block++;
    }

    fclose(file);
    close(data_sock);
    printf("Finished sending '%s'\n", filename);
}

 /**
  * @brief Handle DELETE request from client: attempt to delete file.
  * 
  * @param sock Socket to use for response.
  * @param client Pointer to client's socket address.
  * @param client_len Length of client's address.
  * @param filename File to delete.
  */
 void handle_delete(int sock, struct sockaddr_in *client, socklen_t client_len, char *filename) {

     printf("DELETE request for file: %s\n", filename);
      // delete file
     if (remove(filename) == 0) {
         send_error(sock, client, client_len, 0, "File deleted successfully");
         printf("File '%s' deleted successfully.\n", filename);
     } else {
         send_error(sock, client, client_len, 1, "Failed to delete file");
         printf("Failed to delete file '%s'\n", filename);
     }
 }
 
 /**
  * @brief Main server loop: initializes and handles incoming TFTP requests.
  * 
  * @return int Exit status.
  */
 int main() {

    // create socket
     int sock = socket(AF_INET, SOCK_DGRAM, 0); // IPv4 m UDP ,
     struct sockaddr_in server = {0}, client; // address for server , add for clinet
     socklen_t client_len = sizeof(client); // the len of the clinet  IP & Port
 
     // Ensure backup directory exists
     struct stat st = {0};
     if (stat("backup", &st) == -1) {
         mkdir("backup", 0755);
     }
 
      // init
     server.sin_family = AF_INET; //  IPv4
     server.sin_port = htons(SERVER_PORT); // 6969
     server.sin_addr.s_addr = INADDR_ANY; // for any IP
      // link the socket to IP & port 
     if (bind(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
         perror("Bind failed");
         return 1;
     }
 
     printf("TFTP server running on port %d...\n", SERVER_PORT);
 
     while (1) {

         unsigned char buffer[MAX_PACKET_SIZE];
         int n = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&client, &client_len);

         if (n < 4) continue;
          int opcode = buffer[1];

           // get the file name
         char *filename = (char *)&buffer[2];
        // check opcode
         if (opcode == OP_RRQ) {
            // handle rrq (download)
             printf("RRQ for file: %s\n", filename);
             handle_rrq(sock, &client, client_len, filename);
         } else if (opcode == OP_WRQ) {
            // handle wrq (upload)
             printf("WRQ for file: %s\n", filename);
             handle_wrq(sock, &client, client_len, filename);
         } else if (opcode == OP_DELETE) {
            // delete file
             handle_delete(sock, &client, client_len, filename);
         } else {
             // iligal opcode 
             send_error(sock, &client, client_len, 4, "Illegal TFTP operation");
         }
     }
 
     close(sock);
     return 0;
 }
 