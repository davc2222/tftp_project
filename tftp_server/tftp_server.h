/**
 * @file tftp_server.h
 * @brief Header file for the TFTP server implementation.
 */

 #ifndef TFTP_SERVER_H
 #define TFTP_SERVER_H
 
 #include <stdint.h>
 #include <netinet/in.h>
 
 #define SERVER_PORT 6969
 #define MAX_DATA_SIZE 512
 #define MAX_PACKET_SIZE 517
 
 // TFTP Opcodes
 #define OP_RRQ    1
 #define OP_WRQ    2
 #define OP_DATA   3
 #define OP_ACK    4
 #define OP_ERROR  5
 #define OP_DELETE 6
 
 /**
  * @brief Calculates CRC-8 for a given data buffer.
  * @param data Pointer to the data buffer.
  * @param len Length of the data.
  * @return The computed CRC-8 value.
  */
 uint8_t crc8(const uint8_t *data, size_t len);
 
 /**
  * @brief Sends an error message to the client.
  * @param sock Socket file descriptor.
  * @param client Pointer to client address structure.
  * @param client_len Length of the client address structure.
  * @param error_code Error code.
  * @param msg Error message.
  */
 void send_error(int sock, struct sockaddr_in *client, socklen_t client_len, int error_code, const char *msg);
 
 /**
  * @brief Handles read requests from the client (RRQ).
  * @param listen_sock Listening socket.
  * @param client Pointer to client address.
  * @param client_len Length of client address.
  * @param filename Requested file name.
  */
 void handle_rrq(int listen_sock, struct sockaddr_in *client, socklen_t client_len, char *filename);
 
 /**
  * @brief Handles write requests from the client (WRQ).
  * @param listen_sock Listening socket.
  * @param client Pointer to client address.
  * @param client_len Length of client address.
  * @param filename Target file name.
  */
 void handle_wrq(int listen_sock, struct sockaddr_in *client, socklen_t client_len, char *filename);
 
 /**
  * @brief Handles file delete requests from the client.
  * @param sock Socket file descriptor.
  * @param client Pointer to client address.
  * @param client_len Length of client address.
  * @param filename File to delete.
  */
 void handle_delete(int sock, struct sockaddr_in *client, socklen_t client_len, char *filename);
 
 /**
  * @brief Creates a backup copy of a given file in the "backup" directory.
  * @param filename Name of the file to back up.
  */
 void backup_file(const char *filename);
 
 #endif // TFTP_SERVER_H
 