/*!
 * \file tftp_client.c
 * \brief
 * This file implements a UDP-based client for the Trivial File Transfer Protocol (TFTP),
 * supporting the following operations:
 * - RRQ (Read Request): Downloading a file from the server
 * - WRQ (Write Request): Uploading a file to the server
 * - DELETE: Requesting deletion of a file from the server
 * - Ping: A special read request "__ping__" to verify server availability
 *
 * The implementation uses CRC-8 validation to ensure data integrity and handles retransmissions
 * on timeouts or missing acknowledgments. This client is compatible with a custom TFTP server
 * that supports dynamic ports and extended functionality like deletion and ping.
 */

 #include "tftp_client.h"
 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <unistd.h>
 #include <arpa/inet.h>
 #include <sys/time.h>
 
 /**
  * @brief Compute CRC-8 checksum over a data buffer using polynomial 0x07.
  *
  * @param data Pointer to input data.
  * @param len Number of bytes in the input buffer.
  * @return CRC-8 checksum result.
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
  * @brief Ping the server using a special RRQ for "__ping__" to verify it's alive.
  *
  * @param sock UDP socket used for communication.
  * @param server_addr Pointer to server's address structure.
  * @param addr_len Size of the server address structure.
  * @return 1 if the server responded with valid data, 0 otherwise.
  */
 int ping_server(int sock, struct sockaddr_in *server_addr, socklen_t addr_len) {
     unsigned char ping_packet[] = {0, OP_RRQ, '_', '_', 'p', 'i', 'n', 'g', '_', '_', 0};
     sendto(sock, ping_packet, sizeof(ping_packet), 0, (struct sockaddr *)server_addr, addr_len);
 
     unsigned char buffer[MAX_PACKET_SIZE];

     // support for dynamic port
     struct sockaddr_in from_addr;
     socklen_t from_len = sizeof(from_addr);
 
     // Set socket receive timeout to 3 seconds
     struct timeval timeout = {3, 0};
     setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
 
     // Wait for a DATA response from the server
     int n = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&from_addr, &from_len);
     return (n >= 5 && buffer[1] == OP_DATA);
 }
 
 /**
  * @brief Send an error packet to the server.
  *
  * @param sock UDP socket.
  * @param server_addr Pointer to server address.
  * @param addr_len Length of server address.
  * @param code Error code to send.
  * @param msg Error message string (null-terminated).
  */
 void send_error(int sock, struct sockaddr_in *server_addr, socklen_t addr_len, int code, const char *msg) {
     unsigned char buf[MAX_PACKET_SIZE];
     int len = 0;
     buf[len++] = 0;
     buf[len++] = OP_ERROR;
     buf[len++] = 0;
     buf[len++] = code;
 
     // Copy message into packet
     while (*msg) buf[len++] = *msg++;
     buf[len++] = 0;
 
     sendto(sock, buf, len, 0, (struct sockaddr *)server_addr, addr_len);
 }
 
/**
 * @brief Download a file from the server using RRQ (Read Request).
 *        Handles retransmissions and CRC-8 validation.
 *
 * @param sock The UDP socket to use
 * @param server_addr Pointer to the server address struct
 * @param filename The name of the file to download
 */
void rrq(int sock, struct sockaddr_in *server_addr, const char *filename) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        perror("fopen");
        return;
    }

    // Build and send RRQ packet
    unsigned char rrq_packet[516];
    int rrq_len = 2 + strlen(filename) + 1;
    rrq_packet[0] = 0;
    rrq_packet[1] = OP_RRQ;
    strcpy((char *)&rrq_packet[2], filename);
    sendto(sock, rrq_packet, rrq_len, 0, (struct sockaddr *)server_addr, sizeof(*server_addr));

    uint16_t expected_block = 1;
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    unsigned char buf[MAX_PACKET_SIZE];

    while (1) {
        int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from_addr, &from_len);
        if (n < 5) {
            printf("Invalid packet\n");
            break;
        }

        uint8_t opcode = buf[1];
        uint16_t block = (buf[2] << 8) | buf[3];
        uint8_t crc_received = buf[n - 1];

        // Calculate CRC over opcode + block + data
 uint8_t crc_calc =calculate_crc8(&buf[4], n - 5);

        if (crc_calc != crc_received) {
            printf("CRC mismatch on block %d (expected %02X, got %02X)\n", block, crc_calc, crc_received);
            continue; // wait for retransmit
        }

        if (opcode == OP_DATA && block == expected_block) {
            int data_len = n - 4 - 1;  // total - header (2+2) - CRC
            if (data_len > 0) {
                fwrite(&buf[4], 1, data_len, fp);
            }

            // Send ACK for received block
            unsigned char ack[4];
            ack[0] = 0;
            ack[1] = OP_ACK;
            ack[2] = buf[2];
            ack[3] = buf[3];
            sendto(sock, ack, sizeof(ack), 0, (struct sockaddr *)&from_addr, from_len);

            expected_block++;

            //  Case 1: Normal end — data block is less than 512 bytes
            if (data_len < MAX_DATA_SIZE) {
                printf("Download complete\n");
                break;
            }

        } else if (opcode == OP_DATA && block == expected_block && (n == 5)) {
            // Case 2: Empty data block (0 bytes of data)
            // This is sent *only* after a final 512-byte block to signal end of file.
            printf("Received final empty block (block %d)\n", block);

            // Send ACK for empty block
            unsigned char ack[4];
            ack[0] = 0;
            ack[1] = OP_ACK;
            ack[2] = buf[2];
            ack[3] = buf[3];
            sendto(sock, ack, sizeof(ack), 0, (struct sockaddr *)&from_addr, from_len);

            printf("Download complete\n");
            break;

        } else if (opcode == OP_ERROR) {
            printf("Server error: %s\n", &buf[4]);
            break;
        } else {
            printf("Unexpected packet (opcode: %d, block: %d)\n", opcode, block);
        }
    }

    fclose(fp);
}

 
 /**
  * @brief Perform a TFTP WRQ (upload) to the server.
  *
  * @param sock UDP socket.
  * @param server_addr Pointer to server address.
  * @param addr_len Length of the address.
  * @param local_file Path to local file to send.
  * @param remote_file Destination filename on the server.
  */
 /**
 * @brief Perform a TFTP WRQ (upload) to the server.
 *
 * This function uploads a local file to a TFTP server using the Write Request (WRQ) procedure.
 * It handles retries, acknowledgments, and special case of sending a final empty DATA block
 * if the file size is an exact multiple of 512 bytes.
 *
 * @param sock UDP socket used for communication.
 * @param server_addr Pointer to the server's sockaddr_in structure.
 * @param addr_len Length of the server address structure.
 * @param local_file Path to the local file to be uploaded.
 * @param remote_file Target filename on the server.
 */
void wrq(int sock, struct sockaddr_in *server_addr, socklen_t addr_len, const char *local_file, const char *remote_file) {
    unsigned char buf[MAX_PACKET_SIZE];
    unsigned char ack[4];

    // Open the local file for reading in binary mode
    FILE *fp = fopen(local_file, "rb");
    if (!fp) {
        perror("Cannot open local file");
        return;
    }

    // Check if file size is within TFTP limits (max 65535 blocks * 512 bytes)
    fseek(fp, 0, SEEK_END);
    long filesize = ftell(fp);
    rewind(fp);
    if (filesize > 512 * 65535) {
        printf("File too large for TFTP\n");
        fclose(fp);
        return;
    }

    // Prepare and send the WRQ (Write Request) packet with the remote filename
    int wrq_len = 2 + strlen(remote_file) + 1; // Opcode(2) + filename + null terminator
    buf[0] = 0;
    buf[1] = OP_WRQ;  // WRQ opcode
    strcpy((char *)&buf[2], remote_file);
    sendto(sock, buf, wrq_len, 0, (struct sockaddr *)server_addr, addr_len);

    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    // Set socket receive timeout (3 seconds)
    struct timeval timeout = {3, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    // Wait for ACK(0) from the server acknowledging the WRQ
    int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from_addr, &from_len);
    if (n < 4 || buf[1] != OP_ACK || buf[2] != 0 || buf[3] != 0) {
        printf("Did not receive ACK for WRQ\n");
        fclose(fp);
        return;
    }

    int block = 1;  // Start block numbering from 1

    while (1) {
        // Read up to MAX_DATA_SIZE bytes from file into buffer starting at buf[4]
        size_t bytes_read = fread(&buf[4], 1, MAX_DATA_SIZE, fp);

        // Construct DATA packet header
        buf[0] = 0;
        buf[1] = OP_DATA;  // DATA opcode
        buf[2] = (block >> 8) & 0xFF;  // High byte of block number
        buf[3] = block & 0xFF;         // Low byte of block number

        // Calculate CRC8 over the data portion and store it immediately after data
        buf[bytes_read + 4] = calculate_crc8(&buf[4], bytes_read);

        // Retry logic: try up to 3 times to send DATA and receive correct ACK
        int retries = 3;
        while (retries-- > 0) {
            sendto(sock, buf, bytes_read + 5, 0, (struct sockaddr *)&from_addr, from_len);
            n = recvfrom(sock, ack, sizeof(ack), 0, (struct sockaddr *)&from_addr, &from_len);
            if (n >= 4 && ack[1] == OP_ACK && ack[2] == buf[2] && ack[3] == buf[3]) {
                // Correct ACK received for current block
                break;
            }
        }

        if (retries < 0) {
            // Timeout waiting for ACK, abort upload
            printf("Timeout waiting for ACK for block %d\n", block);
            break;
        }

        block++;  // Increment block number for next DATA packet

        if (bytes_read < MAX_DATA_SIZE) {
            // File read less than 512 bytes → this is the final DATA block, upload complete
            printf("Upload complete\n");
            break;
        }

        // If bytes_read == MAX_DATA_SIZE (512), we might be at file boundary.
        // Check if EOF has been reached by attempting to read one more byte.
        int next_byte = fgetc(fp);
        if (next_byte == EOF) {
            // File ends exactly at 512-byte boundary → send final zero-length DATA block

            // Prepare zero-length DATA block with incremented block number
            buf[0] = 0;
            buf[1] = OP_DATA;
            buf[2] = (block >> 8) & 0xFF;
            buf[3] = block & 0xFF;
            buf[4] =calculate_crc8(NULL, 0);  // CRC for empty data

            retries = 3;
            while (retries-- > 0) {
                sendto(sock, buf, 5, 0, (struct sockaddr *)&from_addr, from_len);
                n = recvfrom(sock, ack, sizeof(ack), 0, (struct sockaddr *)&from_addr, &from_len);
                if (n >= 4 && ack[1] == OP_ACK && ack[2] == buf[2] && ack[3] == buf[3]) {
                    // ACK received for zero-length block
                    break;
                }
            }
            printf("Upload complete\n");
            break;
        } else {
            // Not EOF, so push the byte back for next fread() call

            ungetc(next_byte, fp);
        }
    }

    fclose(fp);
}

 
 /**
  * @brief Send a DELETE request to the server for a specific file.
  *
  * @param sock UDP socket.
  * @param server_addr Pointer to server address structure.
  * @param addr_len Address length.
  * @param remote_file Name of file to delete on the server.
  */
 void delete_file(int sock, struct sockaddr_in *server_addr, socklen_t addr_len, const char *remote_file) {

     unsigned char buf[MAX_PACKET_SIZE];
     unsigned char response[MAX_PACKET_SIZE];
 
     int len = 2 + strlen(remote_file) + 1;
     buf[0] = 0;
     buf[1] = OP_DELETE;
     strcpy((char *)&buf[2], remote_file);
     // send delete command 
     sendto(sock, buf, len, 0, (struct sockaddr *)server_addr, addr_len);
      // support for dynamic port
     struct sockaddr_in from_addr;
     socklen_t from_len = sizeof(from_addr);
     // set timout
     struct timeval timeout = {3, 0};
     setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
     // wait for server response
     int n = recvfrom(sock, response, sizeof(response), 0, (struct sockaddr *)&from_addr, &from_len);
     // if error
     if (n < 4 || response[1] != OP_ERROR) {
         printf("Unexpected or missing server response\n");
         return;
     }
      //  check if file deleted 

     if (response[3] == 0)
         printf("Delete successful: %s\n", &response[4]);
     else
         printf("Delete failed: %s\n", &response[4]);
 }
 
 /**
  * @brief Entry point of the TFTP client.
  * 
  * Prompts user for server IP and provides a menu to upload, download, or delete files.
  * Communicates over UDP using standard TFTP opcodes with CRC-8 verification.
  */
 int main() {

     char server_ip[16];
     printf("Enter server IP address: ");
     if (!fgets(server_ip, sizeof(server_ip), stdin)) {
         printf("Input error\n");
         return 1;
     }
     server_ip[strcspn(server_ip, "\r\n")] = 0;
 
     int sock = socket(AF_INET, SOCK_DGRAM, 0);
     if (sock < 0) {
         perror("socket");
         return 1;
     }
 
     struct sockaddr_in server_addr = {0};
     server_addr.sin_family = AF_INET;
     server_addr.sin_port = htons(SERVER_PORT);
 
     if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) != 1) {
         printf("Invalid IP address\n");
         close(sock);
         return 1;
     }
 
     if (!ping_server(sock, &server_addr, sizeof(server_addr))) {
         printf("Server not responding. Exiting.\n");
         close(sock);
         return 1;
     } else {
         printf("Server is alive.\n");
     }
 
     while (1) {
         printf("\nChoose operation:\n");
         printf("1) rrq (download file)\n");
         printf("2) wrq (upload file)\n");
         printf("3) delete file\n");
         printf("4) exit\n");
         printf("Your choice: ");
 
         int choice = 0;
         if (scanf("%d", &choice) != 1) {
             while (getchar() != '\n');
             printf("Invalid input\n");
             continue;
         }
         // clear bfr
         while (getchar() != '\n');
 
         char filename[256];

         // handle user choice
         switch (choice) {
             case 1:
                 printf("Enter filename to download: ");
                 if (!fgets(filename, sizeof(filename), stdin)) continue;
                 filename[strcspn(filename, "\r\n")] = 0;
                 rrq(sock, &server_addr,  filename);
                 break;
             case 2:
                 printf("Enter filename to upload: ");
                 if (!fgets(filename, sizeof(filename), stdin)) continue;
                 filename[strcspn(filename, "\r\n")] = 0;
                 wrq(sock, &server_addr, sizeof(server_addr), filename, filename);
                 break;
             case 3:
                 printf("Enter filename to delete: ");
                 if (!fgets(filename, sizeof(filename), stdin)) continue;
                 filename[strcspn(filename, "\r\n")] = 0;
                 delete_file(sock, &server_addr, sizeof(server_addr), filename);
                 break;
             case 4:
                 printf("Exiting...\n");
                 close(sock);
                 return 0;
             default:
                 printf("Invalid option\n");
         }
     }
 
     return 0;
 }
 