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
 uint8_t crc8(const uint8_t *data, size_t len) {
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
  * @brief Perform a TFTP RRQ (download) from the server.
  *
  * @param sock UDP socket.
  * @param server_addr Server address structure.
  * @param addr_len Length of the server address.
  * @param filename Name of the remote file to download.
  */
 void rrq(int sock, struct sockaddr_in *server_addr, socklen_t addr_len, const char *filename) {
     unsigned char buf[MAX_PACKET_SIZE];
     unsigned char ack[4];
     // open file to write  data
     FILE *fp = fopen(filename, "wb");
     if (!fp) {
         perror("Cannot open local file");
         return;
     }
 
     // Construct RRQ packet
     int rrq_len = 2 + strlen(filename) + 1;
     buf[0] = 0;
     buf[1] = OP_RRQ;
     strcpy((char *)&buf[2], filename);
     sendto(sock, buf, rrq_len, 0, (struct sockaddr *)server_addr, addr_len);
 
     int expected_block = 1;
     struct sockaddr_in from_addr;
     socklen_t from_len = sizeof(from_addr);
 
     // Set 3-second timeout for receiving
     struct timeval timeout = {3, 0};
     setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
 
     while (1) {

        // check for incomming msgs and handle according
        // to opcode
         int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from_addr, &from_len);
         if (n < 0) {
             printf("Timeout or error receiving data\n");
             break;
         }
          //    case server error
         if (buf[1] == OP_ERROR) {
             printf("Server error: %s\n", &buf[4]);
             break;
         }
            // no data packet
         if (buf[1] != OP_DATA) {
             printf("Unexpected packet opcode %d\n", buf[1]);
             continue;
         }
           // get vlocks num from buffer
         int block = (buf[2] << 8) | buf[3];
           // get crc from buffer
         uint8_t received_crc = buf[n - 1];
         // calc crc on data only , skip header(4) and substrct the crc 
         // byte .
         uint8_t calc_crc = crc8(&buf[4], n - 5);
          // check if recived crc  value = cal crc
         if (received_crc != calc_crc) {
             printf("CRC mismatch on block %d\n", block);
             continue;
         }
              // check if it's the desired block
         if (block == expected_block) {
             // Write received data to file
             fwrite(&buf[4], 1, n - 5, fp);
             expected_block++;
         } else {
             // ACK the previous block again if a duplicate or out-of-order packet arrives
             int ack_block = expected_block - 1;
             ack[0] = 0; ack[1] = OP_ACK;
             ack[2] = (ack_block >> 8) & 0xFF;
             ack[3] = ack_block & 0xFF;
             // send ACK to last block
             sendto(sock, ack, 4, 0, (struct sockaddr *)&from_addr, from_len);
             continue;
         }
 
         // Send ACK for the current block
         ack[0] = 0;
         ack[1] = OP_ACK;
         ack[2] = buf[2];
         ack[3] = buf[3];
         sendto(sock, ack, 4, 0, (struct sockaddr *)&from_addr, from_len);
      /**
         If less than 512 bytes received, this is the last block
         if the last block was 512 bytes the
         server will send 0 data msg
         so client according to statment know it
         was the last block that been received
     */
         if (n - 5 < MAX_DATA_SIZE) {
             printf("Download complete\n");
             break;
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
 void wrq(int sock, struct sockaddr_in *server_addr, socklen_t addr_len, const char *local_file, const char *remote_file) {

     unsigned char buf[MAX_PACKET_SIZE];
     unsigned char ack[4];
       // open file to upload to server
     FILE *fp = fopen(local_file, "rb");
     if (!fp) {
         perror("Cannot open local file");
         return;
     }
 
     // Check file size limit
     fseek(fp, 0, SEEK_END);
     long filesize = ftell(fp);
     rewind(fp);
     if (filesize > 512 * 65535) {
         printf("File too large for TFTP\n");
         fclose(fp);
         return;
     }
 
     // Send WRQ request
     int wrq_len = 2 + strlen(remote_file) + 1;
     buf[0] = 0;
     buf[1] = OP_WRQ;
     strcpy((char *)&buf[2], remote_file);
     sendto(sock, buf, wrq_len, 0, (struct sockaddr *)server_addr, addr_len);
       // to support dynamic port
     struct sockaddr_in from_addr;
     socklen_t from_len = sizeof(from_addr);
     // set timeout
     struct timeval timeout = {3, 0};
     setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
 
     // Expect ACK(0) from server
     int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from_addr, &from_len);
     if (n < 4 || buf[1] != OP_ACK || buf[2] != 0 || buf[3] != 0) {
         printf("Did not receive ACK for WRQ\n");
         fclose(fp);
         return;
     }
 
     int block = 1;
     while (1) {
         size_t bytes_read = fread(&buf[4], 1, MAX_DATA_SIZE, fp);
         buf[0] = 0;
         buf[1] = OP_DATA;
          // set block num
         buf[2] = (block >> 8) & 0xFF;
         buf[3] = block & 0xFF;
         // calc CRC
         buf[bytes_read + 4] = crc8(&buf[4], bytes_read);
 
         int retries = 3;
         while (retries-- > 0) {
            // send the data
             sendto(sock, buf, bytes_read + 5, 0, (struct sockaddr *)&from_addr, from_len);
             // wait for acknonledg 
             n = recvfrom(sock, ack, sizeof(ack), 0, (struct sockaddr *)&from_addr, &from_len);
             if (n >= 4 && ack[1] == OP_ACK && ack[2] == buf[2] && ack[3] == buf[3])
                 break;
         }
 
         if (retries < 0) {
             printf("Timeout waiting for ACK for block %d\n", block);
             break;
         }
 
         if (bytes_read < MAX_DATA_SIZE) {
             printf("Upload complete\n");
 
             // Final 0-byte DATA block if file ends exactly on 512 bytes
             if (bytes_read == MAX_DATA_SIZE) {
                 block++;
                 buf[0] = 0;
                 buf[1] = OP_DATA;
                 buf[2] = (block >> 8) & 0xFF;
                 buf[3] = block & 0xFF;
                 buf[4] = crc8(NULL, 0);
 
                 retries = 3;
                 while (retries-- > 0) {
                     sendto(sock, buf, 5, 0, (struct sockaddr *)&from_addr, &from_len);
                     n = recvfrom(sock, ack, sizeof(ack), 0, (struct sockaddr *)&from_addr, &from_len);
                     if (n >= 4 && ack[1] == OP_ACK && ack[2] == buf[2] && ack[3] == buf[3])
                         break;
                 }
             }
             break;
         }
 
         block++;
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
                 rrq(sock, &server_addr, sizeof(server_addr), filename);
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
 