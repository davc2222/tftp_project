TFTP Protocol Usage Guide (Based on This Code) Introduction TFTP (Trivial File Transfer Protocol) is a simple UDP-based file transfer protocol. Your implementation supports file download (RRQ), upload (WRQ), and file deletion, with CRC-8 error checking and retransmission support.

Server Workflow
1. Server Startup Listens on UDP port 6969 for incoming client requests.
2. Receiving Requests Detects request type by the Opcode in the incoming packet: RRQ, WRQ, or DELETE.
3. Request handling o RRQ (Read Request): Sends the requested file to the client in 512-byte data blocks. WRQ (Write Request): Receives a file from the client block-by-block, acknowledging each one. W File size limit: Files larger than approximately 33.5 MB (512 bytes * 65535 blocks) are rejected to avoid protocol limitations. DELETE: Deletes the specified file on the server.
4. Sending Responses and ACKs Every DATA packet includes a CRC-8 checksum. The server retransmits packets if ACKs are lost or errors occur.

Client Usage Connecting to the Server • Enter the server's IP address. • The client sends a "ping" request to verify the server is alive. Choosing an Operation A menu is displayed:
1. Download file (RRQ)
2. Upload file (WRQ)
3. Delete file (DELETE)
4. Exit Downloading a File (RRQ) • Choose option 1. • Enter the filename that exists on the server. • The client receives DATA blocks, validates CRC-8, and writes the file locally. Uploading a File (WRQ) • Choose option 2. • Enter the name of a local file to upload. • The client checks the file size and rejects files larger than ~33.5 MB to comply with protocol limits. • The client sends WRQ, waits for ACK, then sends DATA blocks. • Each block must be acknowledged by the server. • After upload, the server saves the file and creates a backup. Deleting a File (DELETE) • Choose option 3. • Enter the filename to delete on the server. • The server attempts deletion and returns success or failure messages.

Important Details • Files are transferred in blocks of up to 512 bytes. • The maximum number of blocks is 65,535, limiting the file size to approximately 33.5 MB. • Each DATA packet includes a CRC-8 checksum for data integrity. • Retries are performed up to 3 times for lost packets or missing ACKs. • Timeout per packet is about 1-3 seconds. • Backup copies of uploaded files are saved automatically in a backup directory.

Tips • Ensure files you want to upload exist locally and are within the size limit. • Avoid overwriting important local files when downloading. • If you experience CRC errors or timeouts, verify your network reliability. • Do not change the server port unless the server configuration is updated accordingly.

Summary this implementation offers a lightweight, reliable file transfer mechanism with error checking and retransmission, suitable for small to medium files over UDP, with a size limitation of about 33.5 MB per file due to protocol constraints.



Start the Client
./build/app

Start the Server
./build/app


