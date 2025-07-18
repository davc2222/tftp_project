/*!
 * \file tftp_client.h
 * \brief Header file for TFTP client functions.
 */

 #ifndef TFTP_CLIENT_H
 #define TFTP_CLIENT_H
 
 #include <stdint.h>
 #include <netinet/in.h>
 #include <sys/socket.h>
 
 #define SERVER_PORT         6969
 #define MAX_DATA_SIZE      512
 #define MAX_PACKET_SIZE  517
 
 // TFTP operation codes
 #define OP_RRQ     1
 #define OP_WRQ    2
 #define OP_DATA    3
 #define OP_ACK      4
 #define OP_ERROR  5
 #define OP_DELETE 6
 
 
 
 /*!
  * \brief Compute CRC-8 over a data buffer.
  */
 uint8_t calculate_crc8(const uint8_t *data, size_t len);
 
 /*!
  * \brief Ping the TFTP server to verify connectivity.
  */
 int ping_server(int sock, struct sockaddr_in *server_addr, socklen_t addr_len);
 
 /*!
  * \brief Send an error packet to the server.
  */
 void send_error(int sock, struct sockaddr_in *server_addr, socklen_t addr_len, int code, const char *msg);
 
 /*!
  * \brief Read a file from the server (RRQ).
  */
 
 void rrq(int sock, struct sockaddr_in *server_addr, const char *filename);
 /*!
  * \brief Write a file to the server (WRQ).
  */
 void wrq(int sock, struct sockaddr_in *server_addr, socklen_t addr_len, const char *local_file, const char *remote_file);
 
 /*!
  * \brief Delete a file on the server.
  */
 void delete_file(int sock, struct sockaddr_in *server_addr, socklen_t addr_len, const char *remote_file);
 
 
 
 #endif // TFTP_CLIENT_H
 