TFTP Protocol Usage Guide (Based on Your Code)
Introduction
TFTP (Trivial File Transfer Protocol) is a simple UDP-based file transfer protocol. Your implementation supports file download (RRQ), upload (WRQ), and file deletion, with CRC-8 error checking and retransmission support.
________________________________________
Server Workflow
1.	Server Startup
Listens on UDP port 6969 for incoming client requests.
2.	Receiving Requests
Detects request type by the Opcode in the incoming packet: RRQ, WRQ, or DELETE.
3.	Request Handling
o	RRQ (Read Request): Sends the requested file to the client in 512-byte data blocks.
o	WRQ (Write Request): Receives a file from the client block-by-block, acknowledging each one.
o	DELETE: Deletes the specified file on the server.
4.	Sending Responses and ACKs
Every DATA packet includes a CRC-8 checksum. The server retransmits packets if ACKs are lost or errors occur.
________________________________________
Client Usage
Connecting to the Server
•	Enter the server's IP address.
•	The client sends a "ping" request to verify the server is alive.
Choosing an Operation
•	A menu is displayed:
1.	Download file (RRQ)
2.	Upload file (WRQ)
3.	Delete file (DELETE)
4.	Exit
Downloading a File (RRQ)
•	Choose option 1.
•	Enter the filename that exists on the server.
•	The client receives DATA blocks, validates CRC-8, and writes the file locally.
Uploading a File (WRQ)
•	Choose option 2.
•	Enter the name of a local file to upload.
•	The client sends WRQ, waits for ACK, then sends DATA blocks.
•	Each block must be acknowledged by the server.
•	After upload, the server saves the file and creates a backup.
Deleting a File (DELETE)
•	Choose option 3.
•	Enter the filename to delete on the server.
•	The server attempts deletion and returns success or failure messages.
________________________________________
Important Details
•	Files are transferred in blocks of up to 512 bytes.
•	Each DATA packet includes a CRC-8 checksum for data integrity.
•	Retries are performed up to 3 times for lost packets or missing ACKs.
•	Timeout per packet is about 1-3 seconds.
•	Backup copies of uploaded files are saved automatically in a backup directory.
________________________________________
Tips
•	Ensure files you want to upload exist locally.
•	Avoid overwriting important local files when downloading.
•	If you experience CRC errors or timeouts, verify your network reliability.
•	Do not change the server port unless the server configuration is updated accordingly.
________________________________________
Summary
Your implementation offers a lightweight, reliable file transfer mechanism with error checking and retransmission, suitable for small to medium files over UDP.

