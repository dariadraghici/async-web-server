// SPDX-License-Identifier: BSD-3-Clause

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/sendfile.h>
#include <sys/eventfd.h>
#include <libaio.h>
#include <errno.h>

#include "aws.h"
#include "utils/util.h"
#include "utils/debug.h"
#include "utils/sock_util.h"
#include "utils/w_epoll.h"

/* server socket file descriptor */
static int listenfd;

/* epoll file descriptor */
static int epollfd;

static io_context_t ctx;

static int aws_on_path_cb(http_parser *p, const char *buf, size_t len)
{
	struct connection *conn = (struct connection *)p->data;

	memcpy(conn->request_path, buf, len);
	conn->request_path[len] = '\0';
	conn->have_path = 1;

	return 0;
}

static void connection_prepare_send_reply_header(struct connection *conn)
{
	/* TODO: Prepare the connection buffer to send the reply header. */
	// https://www.rfc-editor.org/rfc/rfc7230#section-3.1.2
	// Server response:
	//  HTTP/1.1 ...
	//  Date: ...
	//  Server: ...
	//  Last-Modified: ...
	//  ETag: "..."
	//  Accept-Ranges: bytes
	//  Content-Length: ...
	//  Vary: Accept-Encoding
	//  Content-Type: text/plain

	const char *status_line = "HTTP/1.1 200 OK\r\n"; // HTTP/1.1 Message Syntax and Routing
	const char *hdr_server = "Server: SO AWS\r\n"; // Server header field
	const char *hdr_ranges = "Accept-Ranges: bytes\r\n";
	const char *hdr_len = "Content-Length: ";
	const char *hdr_ct = "Content-Type: application/octet-stream\r\n";
	const char *hdr_connection = "Connection: close\r\n";
	const char *empty_line = "\r\n"; // empty line to indicate end of headers

	char content_length_buf[64]; // buffer to hold content length value as string
	// https://www.geeksforgeeks.org/c/snprintf-c-library/
	// int snprintf(char *str, size_t size, const char *format, ...);
	// The snprintf() function sends formatted output to a string pointed to, by str.
	sprintf(content_length_buf, "%ld", conn->file_size); // convert file size to string

	size_t len_status = strlen(status_line);
	size_t len_server = strlen(hdr_server);
	size_t len_ranges = strlen(hdr_ranges);
	size_t len_hdr_len = strlen(hdr_len);
	size_t len_len = strlen(content_length_buf);
	size_t len_ct = strlen(hdr_ct);
	size_t len_connection = strlen(hdr_connection);
	size_t len_empty = strlen(empty_line);

	char *ptr = conn->send_buffer; // copy each part of the response into the send buffer sequentially

	memcpy(ptr, status_line, len_status); // copy status line
	ptr = ptr + len_status; // move pointer forward

	memcpy(ptr, hdr_server, len_server); // copy server header
	ptr = ptr + len_server; // move pointer forward

	memcpy(ptr, hdr_ranges, len_ranges); // copy ranges header
	ptr = ptr + len_ranges; // move pointer forward

	memcpy(ptr, hdr_len, len_hdr_len); // copy content length header
	ptr = ptr + len_hdr_len; // move pointer forward

	memcpy(ptr, content_length_buf, len_len); // copy content length value
	ptr = ptr + len_len; // move pointer forward

	*ptr++ = '\r'; // add carriage return because snprintf doesn't add it
	*ptr++ = '\n'; // add line feed because snprintf doesn't add it

	memcpy(ptr, hdr_ct, len_ct); // copy content type header
	ptr = ptr + len_ct; // move pointer forward

	memcpy(ptr, hdr_connection, len_connection); // copy connection header
	ptr = ptr + len_connection; // move pointer forward

	memcpy(ptr, empty_line, len_empty); // copy empty line
	ptr = ptr + len_empty; // move pointer forward

	conn->send_len = ptr - conn->send_buffer; // total length of the response
	conn->send_pos = 0; // reset send position to 0 to start sending from the beginning of the buffer
}

static void connection_prepare_send_404(struct connection *conn)
{
	/* TODO: Prepare the connection buffer to send the 404 header. */
	// https://www.rfc-editor.org/rfc/rfc7230#section-3.1.2
	// Server response:
	//  HTTP/1.1 ...
	//  Date: ...
	//  Server: ...
	//  Last-Modified: ...
	//  ETag: "..."
	//  Accept-Ranges: bytes
	//  Content-Length: ...
	//  Vary: Accept-Encoding
	//  Content-Type: text/plain
	//  Connection: close

	const char *status_line = "HTTP/1.1 404 Not Found\r\n"; // HTTP/1.1 Message Syntax and Routing
	const char *hdr_server = "Server: SO AWS\r\n"; // Server header field
	const char *hdr_ranges = "Accept-Ranges: bytes\r\n";
	const char *hdr_len = "Content-Length: 0\r\n";
	const char *hdr_ct = "Content-Type: text/plain\r\n";
	const char *hdr_connection = "Connection: close\r\n";
	const char *empty_line = "\r\n"; // empty line to indicate end of headers

	size_t len_status = strlen(status_line);
	size_t len_server = strlen(hdr_server);
	size_t len_ranges = strlen(hdr_ranges);
	size_t len_len = strlen(hdr_len);
	size_t len_ct = strlen(hdr_ct);
	size_t len_connection = strlen(hdr_connection);
	size_t len_empty = strlen(empty_line);

	char *ptr = conn->send_buffer; // copy each part of the response into the send buffer sequentially

	memcpy(ptr, status_line, len_status); // copy status line
	ptr = ptr + len_status; // move pointer forward

	memcpy(ptr, hdr_server, len_server); // copy server header
	ptr = ptr + len_server; // move pointer forward

	memcpy(ptr, hdr_ranges, len_ranges); // copy ranges header
	ptr = ptr + len_ranges; // move pointer forward

	memcpy(ptr, hdr_len, len_len); // copy content length header
	ptr = ptr + len_len; // move pointer forward

	memcpy(ptr, hdr_ct, len_ct); // copy content type header
	ptr = ptr + len_ct; // move pointer forward

	memcpy(ptr, hdr_connection, len_connection); // copy connection header
	ptr = ptr + len_connection; // move pointer forward

	memcpy(ptr, empty_line, len_empty); // copy empty line
	ptr = ptr + len_empty; // move pointer forward

	conn->send_len = ptr - conn->send_buffer; // total length of the response
	conn->send_pos = 0; // reset send position to 0 to start sending from the beginning of the buffer
}

static enum resource_type connection_get_resource_type(struct connection *conn)
{
	/* TODO: Get resource type depending on request path/filename. Filename should
	 * point to the static or dynamic folder.
	 */

	if (strstr(conn->request_path, AWS_REL_STATIC_FOLDER)) // defined in aws.h as "static/"
		return RESOURCE_TYPE_STATIC; // if the request path contains "static/" return RESOURCE_TYPE_STATIC
									// defined in aws.h in resource_type enum
	if (strstr(conn->request_path, AWS_REL_DYNAMIC_FOLDER)) // defined in aws.h as "dynamic/"
		return RESOURCE_TYPE_DYNAMIC; // if the request path contains "dynamic/" return RESOURCE_TYPE_DYNAMIC
									// defined in aws.h in resource_type enum
	return RESOURCE_TYPE_NONE;
}


struct connection *connection_create(int sockfd)
{
	/* TODO: Initialize connection structure on given socket. */
	struct connection *conn = malloc(sizeof(*conn)); // allocate memory for connection structure

	conn->sockfd = sockfd; // set the socket file descriptor
	conn->fd = -1; // initialize file descriptor to -1 (no file opened)
	conn->state = STATE_INITIAL; // set initial state (STATE_INITIAL is defined in aws.h in enum connection_state)

	conn->recv_len = 0; // initialize received length to 0
	conn->send_len = 0; // initialize send length to 0
	conn->send_pos = 0; // initialize send position to 0
	conn->file_pos = 0; // initialize file position to 0
	conn->file_size = 0; // initialize file size to 0
	conn->have_path = 0; // initialize have_path to 0 (no path received yet)
	conn->res_type = RESOURCE_TYPE_NONE; // initialize resource type

	http_parser_init(&conn->request_parser, HTTP_REQUEST); // initialize HTTP request parser from http_parser.h
	conn->request_parser.data = conn; // associate connection structure with parser data

	return conn;
}

void connection_start_async_io(struct connection *conn)
{
	/* TODO: Start asynchronous operation (read from file).
	 * Use io_submit(2) & friends for reading data asynchronously.
	 */
	if (conn->fd < 0 || conn->file_pos >= conn->file_size) // if file not opened or all data read
		return;

	size_t to_read = BUFSIZ; // maximum bytes to read

	if (conn->file_pos + to_read > conn->file_size) // adjust to_read if it exceeds file size
		to_read = conn->file_size - conn->file_pos; // remaining bytes to read

	// https://ocw.cs.pub.ro/courses/so/laboratoare/laborator-11
	// void io_prep_pread(struct iocb *iocb, int fd, void *buf, size_t nbytes, off_t offset);
	// Prepares an iocb structure for a pread operation
	struct iocb *iocb = &conn->iocb; // I/O control block for the asynchronous operation
	int fd = conn->fd; // file descriptor to read from
	void *buf = conn->send_buffer; // buffer to store read data
	size_t nbytes = to_read; // number of bytes to read
	off_t offset = conn->file_pos; // offset in the file to start reading from

	io_prep_pread(iocb, fd, buf, nbytes, offset); // prepare the iocb for pread operation
	conn->iocb.data = (void *)conn; // associate connection structure with iocb data

	conn->piocb[0] = &conn->iocb; // array of iocb pointers for io_submit

	// submit the asynchronous I/O request
	// int io_submit(aio_context_t ctx, long nr, struct iocb **iocbpp);
	long nr = 1; // number of I/O requests to submit
	struct iocb **iocbpp = conn->piocb; // pointer to array of iocb pointers
	int rc = io_submit(ctx, nr, iocbpp);

	if (rc != 1) {// if submission failed
		if (rc < 0) // if error code is negative
			conn->state = STATE_CONNECTION_CLOSED;
		return;
	}
	conn->state = STATE_ASYNC_ONGOING;
}

void connection_remove(struct connection *conn)
{
	/* TODO: Remove connection handler. */
	w_epoll_remove_ptr(epollfd, conn->sockfd, conn); // remove from epoll monitoring
	// from w_epoll.h it removes the file descriptor from the epoll instance

	close(conn->sockfd); // close the client socket

	if (conn->fd != -1) // if a file is opened
		close(conn->fd); // close the file descriptor

	free(conn); // free the connection structure
}

void handle_new_connection(void)
{
	/* TODO: Handle a new connection request on the server socket. */
	static int client_sockfd; // client socket file descriptor
	struct sockaddr_in client_addr; // client address structure
	socklen_t addr_len = sizeof(struct sockaddr_in); // length of client address structure
	struct connection *conn;

	/* TODO: Accept new connection. */
	// https://pubs.opengroup.org/onlinepubs/009604499/functions/accept.html
	// accept() function extracts the first connection on the queue of pending connections
	// create a new socket with the same socket type protocol and address family as the specified socket,
	// and allocate a new file descriptor for that socket.
	// int accept(int socket, struct sockaddr *restrict address, socklen_t *restrict address_len);
	client_sockfd = accept(listenfd, (struct sockaddr *)&client_addr, &addr_len); // accept new connection on server socket
	if (client_sockfd < 0) // if accept failed (returns -1)
		return;

	/* TODO: Set socket to be non-blocking. */
	//https://stackoverflow.com/questions/1543466/how-do-i-change-a-tcp-socket-to-be-non-blocking
	// https://www.ibm.com/docs/en/zvm/7.3.0?topic=descriptions-fcntl-control-open-file-descriptors
	// int fcntl(int socket, int cmd, ...);
	int flags = fcntl(client_sockfd, F_GETFL, 0); // F_GETFL to get the file access mode and the file status flags
	// F_GETFL Obtains the file status flags and file access mode flags for fildes.

	fcntl(client_sockfd, F_SETFL, flags | O_NONBLOCK); // F_SETFL to set the file status flags to non-blocking mode
	// Sets the file status flags for fildes. You must specify a third int argument
	// giving the new file descriptor flag settings.

	/* TODO: Instantiate new connection handler. */
	conn = connection_create(client_sockfd); // create a new connection structure for the client socket

	/* TODO: Add socket to epoll. */
	w_epoll_add_ptr_in(epollfd, client_sockfd, conn); // add the client socket to the epoll instance
	// for monitoring input events

	/* TODO: Initialize HTTP_REQUEST parser. */
	http_parser_init(&conn->request_parser, HTTP_REQUEST); // initialize HTTP request parser from http_parser.h
	conn->request_parser.data = conn; // associate connection structure with parser data
}

void receive_data(struct connection *conn)
{
	/* TODO: Receive message on socket.
	 * Store message in recv_buffer in struct connection.
	 */
	// https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-recv-receive-data-socket
	// int recv(int socket, char *buffer, int length, int flags);
	int socket = conn->sockfd; // socket file descriptor
	char *buffer = conn->recv_buffer + conn->recv_len; // pointer to the buffer position to store received data
	size_t length = BUFSIZ - conn->recv_len; // remaining length in the buffer
	int flags = 0; // no special flags
	ssize_t bytes = recv(socket, buffer, length, flags); // receive data from socket

	if (bytes > 0) {// if data was received successfully
		conn->recv_len = conn->recv_len + bytes; // update received length
		conn->recv_buffer[conn->recv_len] = '\0';
	} else if (bytes == 0) { // if the connection was closed by the peer
		conn->state = STATE_CONNECTION_CLOSED;
	} else if (bytes < 0) { // if an error occurred during recv
		if (errno == EAGAIN || errno == EWOULDBLOCK) // if the socket is not ready for reading
		// EAGAIN The file descriptor is marked non-blocking and no data was read.
		// EWOULDBLOCK The file descriptor is marked non-blocking and the read
			return;
		conn->state = STATE_CONNECTION_CLOSED;
	}
}

int connection_open_file(struct connection *conn)
{
	/* TODO: Open file and update connection fields. */

	char full_path[BUFSIZ]; // buffer to hold the full file path
							// BUFSIZ represents the maximum size of the buffer (also used in aws.h in different structs)
	const char *root = AWS_DOCUMENT_ROOT; // AWS_DOCUMENT_ROOT is defined in aws.h as "./"
	const char *req_path = conn->request_path; // requested path from HTTP (starts with '/')
	const char *file_name = req_path + 1; // skip the leading '/'

	// https://www.geeksforgeeks.org/c/snprintf-c-library/
	// int snprintf(char *str, size_t size, const char *format, ...);
	// The snprintf() function sends formatted output to a string pointed to, by str.
	snprintf(full_path, BUFSIZ, "%s/%s", root, file_name); // construct the full file path
	conn->fd = open(full_path, O_RDONLY); // open file in read-only mode
	if (conn->fd < 0)
		return -1;

	struct stat st; // structure to hold file information (from sys/stat.h)
	// https://pubs.opengroup.org/onlinepubs/009696699/functions/fstat.html
	// int fstat(int fildes, struct stat *buf);

	fstat(conn->fd, &st); // get file status information
	conn->file_size = st.st_size; // store file size in connection structure
	conn->file_pos = 0; // initialize file position to 0
	return 0;
}

void connection_complete_async_io(struct connection *conn)
{
	/* TODO: Complete asynchronous operation; operation returns successfully.
	 * Prepare socket for sending.
	 */
	struct io_event events[1]; // array to hold completed I/O events
	struct timespec timeout = {0, 0}; // zero timeout for non-blocking check

	// https://stackoverflow.com/questions/29780028/io-getevents-returns-a-completion-event-twice
	// int io_getevents(aio_context_t ctx_id, long min_nr, long max_nr, struct io_event *events, struct timespec *timeout);
	// retrieves completed I/O events for the given AIO context
	int rc = io_getevents(ctx, 1, 1, events, &timeout);

	if (rc < 1) // if no events were retrieved
		return;

	struct io_event *event = &events[0]; // get the first completed event

	if (event->res > 0) { // if read operation was successful
		conn->send_len = event->res; // number of bytes read
		conn->send_pos = 0; // reset send position to 0
		conn->file_pos = conn->file_pos + event->res; // update file position
		conn->state = STATE_SENDING_DATA; // update state to SENDING_DATA
		w_epoll_update_ptr_out(epollfd, conn->sockfd, conn); // modify epoll to monitor output events
													// on the connection socket
	} else if (event->res == 0) { // end of file reached
		conn->state = STATE_DATA_SENT;
	} else { // error
		conn->state = STATE_CONNECTION_CLOSED;
	}
}

int parse_header(struct connection *conn)
{
	/* TODO: Parse the HTTP header and extract the file path. */
	/* Use mostly null settings except for on_path callback. */
	http_parser_settings settings_on_path = {
		.on_message_begin = 0,
		.on_header_field = 0,
		.on_header_value = 0,
		.on_path = aws_on_path_cb,
		.on_url = 0,
		.on_fragment = 0,
		.on_query_string = 0,
		.on_body = 0,
		.on_headers_complete = 0,
		.on_message_complete = 0
	};

	if (strstr(conn->recv_buffer, "\r\n\r\n") == NULL) // check if the end of headers is present
		return 0;

	// size_t http_parser_execute (http_parser *parser, const http_parser_settings *settings, const char *data, size_t len)
	http_parser_execute(&conn->request_parser, &settings_on_path, conn->recv_buffer, conn->recv_len);

	if (conn->have_path) // if a path was extracted from the headers
		return 1;
	return 0;
}

enum connection_state connection_send_static(struct connection *conn)
{
	/* TODO: Send static data using sendfile(2). */
	if (conn->fd < 0) // file not opened
		return STATE_CONNECTION_CLOSED; // close the connection

	// https://man7.org/linux/man-pages/man2/sendfile.2.html
	// ssize_t sendfile(int out_fd, int in_fd, off_t *offset, size_t count);
	off_t offset = conn->file_pos; // current position in the file
	int out_fd = conn->sockfd; // socket file descriptor
	int in_fd = conn->fd; // file descriptor for the file to send
	size_t count = conn->file_size - conn->file_pos; // remaining bytes to send
	//sendfile() copies data between one file descriptor and another.

	if (count == 0)
		return STATE_DATA_SENT;

	ssize_t sent = sendfile(out_fd, in_fd, &offset, count); // send file data to socket

	if (sent > 0) { // if data was sent successfully
		conn->file_pos = conn->file_pos + sent; // update file position

		if (conn->file_pos >= conn->file_size) // if the entire file has been sent
			return STATE_DATA_SENT;
		else // continue sending data
			return STATE_SENDING_DATA;
	} else if (sent < 0) { // if an error occurred during sendfile
		if (errno == EAGAIN || errno == EWOULDBLOCK) // if the socket is not ready for writing
		// EAGAIN The file descriptor is marked non-blocking and no data was written.
		// EWOULDBLOCK The file descriptor is marked non-blocking and the write would block.
			return STATE_SENDING_DATA; // continue sending later
		else
			return STATE_CONNECTION_CLOSED;
	}

	conn->file_pos = offset; // update file position after sending

	if (conn->file_pos >= conn->file_size) // if the entire file has been sent
		return STATE_DATA_SENT; // transition to DATA_SENT state

	return STATE_SENDING_DATA; // continue sending data
}

int connection_send_data(struct connection *conn)
{
	/* May be used as a helper function. */
	/* TODO: Send as much data as possible from the connection send buffer.
	 * Returns the number of bytes sent or -1 if an error occurred
	 */
	if (conn->send_pos >= conn->send_len) // if all data has already been sent
		return 0;

	// https://man7.org/linux/man-pages/man2/send.2.html
	//  ssize_t send(int sockfd, const void buf[size], size_t size, int flags);
	int sockfd = conn->sockfd; // socket file descriptor
	const void *buf = conn->send_buffer + conn->send_pos; // pointer to the buffer position to send data from
	size_t size = conn->send_len - conn->send_pos; // remaining data to send
	int flags = 0; // no special flags

	ssize_t sent = send(sockfd, buf, size, flags);

	if (sent > 0) {
		conn->send_pos = conn->send_pos + sent; // update send position
		if (conn->send_pos >= conn->send_len)
			return 0; // all data sent
		else
			return 1; // more data to send
	} else if (sent < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
		// EAGAIN The file descriptor is marked non-blocking and no data was written.
		// EWOULDBLOCK The file descriptor is marked non-blocking and the write would block.
			return 1;
		else
			return -1;
	}

	return 1;
}

int connection_send_dynamic(struct connection *conn)
{
	/* TODO: Read data asynchronously.
	 * Returns 0 on success and -1 on error.
	 */
	if (conn->send_pos >= conn->send_len) { // if all data in the send buffer has been sent
		if (conn->file_pos < conn->file_size) { // if there is more file data to read
			connection_start_async_io(conn); // start async I/O to read more data
			return 1;
		} else {
			return 0;  // all data sent
		}
	}

	// https://man7.org/linux/man-pages/man2/send.2.html
	// ssize_t send(int sockfd, const void buf[size], size_t size, int flags);
	int sockfd = conn->sockfd; // socket file descriptor
	const void *buf = conn->send_buffer + conn->send_pos; // pointer to the buffer position to send data from
	size_t size = conn->send_len - conn->send_pos; // remaining data to send
	int flags = 0; // no special flags
	ssize_t sent = send(sockfd, buf, size, flags);

	if (sent > 0) { // if data was sent successfully
		conn->send_pos = conn->send_pos + sent; // update send position

		if (conn->send_pos >= conn->send_len) { // if all data in the send buffer has been sent
			if (conn->file_pos >= conn->file_size) // if all file data has been read
				return 0; // all data sent
			// if there is more file data to read
			connection_start_async_io(conn); // start async I/O to read more data
			return 1;
		}
		return 1;; // more data to send
	} else if (sent < 0) { // if an error occurred during send
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return 1; // socket not ready for writing
		else
			return -1; // error
	}

	return 1;
}


void handle_input(struct connection *conn)
{
	/* TODO: Handle input information: may be a new message or notification of
	 * completion of an asynchronous I/O operation.
	 */

	switch (conn->state) {
	case STATE_INITIAL:
	case STATE_RECEIVING_DATA:
		receive_data(conn); // receive data on the connection
		conn->state = STATE_RECEIVING_DATA; // update state to RECEIVING_DATA

		if (parse_header(conn)) { // if the HTTP header has been fully received
								// parse the header to extract the file path
			conn->state = STATE_REQUEST_RECEIVED; // update state to REQUEST_RECEIVED

			if (conn->have_path == 0) { // if there is no path in the request
				conn->state = STATE_SENDING_404;
				connection_prepare_send_404(conn);
			} else if (connection_open_file(conn) < 0) { // try to open the requested file
												// if file opening failed (file not found or error)
				conn->state = STATE_SENDING_404; // update state to SENDING_404
				connection_prepare_send_404(conn); // prepare 404 response in send buffer
			} else { // if file opened successfully
				conn->res_type = connection_get_resource_type(conn); // determine resource type (static or dynamic)
				conn->state = STATE_SENDING_HEADER; // update state to SENDING_HEADER
				connection_prepare_send_reply_header(conn); // prepare response header in send buffer
			}

			w_epoll_update_ptr_out(epollfd, conn->sockfd, conn); // modify epoll to monitor output events
																// on the connection socket
			// from w_epoll.h it updates the epoll instance to monitor output events
		}
		break;

	case STATE_ASYNC_ONGOING:
		connection_complete_async_io(conn);
		break;
	default:
		printf("shouldn't get here %d\n", conn->state);
	}
}

void handle_output(struct connection *conn)
{
	/* TODO: Handle output information: may be a new valid requests or notification of
	 * completion of an asynchronous I/O operation or invalid requests.
	 */

	int rc; // return code

	switch (conn->state) {
	case STATE_SENDING_HEADER:
	case STATE_SENDING_404:
		rc = connection_send_data(conn); // send data from the connection send buffer
		if (rc == 0) { // if all data has been sent
			if (conn->state == STATE_SENDING_404) { // if sending 404 response
				conn->state = STATE_404_SENT; // update state to 404_SENT
			} else { // if sending normal response header
				conn->state = STATE_SENDING_DATA; // update state to SENDING_DATA

				if (conn->res_type == RESOURCE_TYPE_STATIC)// if resource type is static
					conn->state = connection_send_static(conn);// send static data
				else if (conn->res_type == RESOURCE_TYPE_DYNAMIC) // if resource type is dynamic
					connection_start_async_io(conn); // start async I/O for dynamic data
			}
		} else if (rc < 0) { // if there was an error sending data
			conn->state = STATE_CONNECTION_CLOSED; // update state to CONNECTION_CLOSED
		}
		break;

	case STATE_SENDING_DATA:
		if (conn->res_type == RESOURCE_TYPE_STATIC) {// if resource type is static
			conn->state = connection_send_static(conn); // send static data
		} else if (conn->res_type == RESOURCE_TYPE_DYNAMIC) { // if resource type is dynamic
			rc = connection_send_dynamic(conn); // send dynamic data
			if (rc == 0) // if all data has been sent
				conn->state = STATE_DATA_SENT; // update state to DATA_SENT
			else if (rc < 0) // if there was an error sending data
				conn->state = STATE_CONNECTION_CLOSED; // update state to CONNECTION_CLOSED
		}
		break;
	case STATE_ASYNC_ONGOING:
		connection_complete_async_io(conn);
		break;
	default:
		ERR("Unexpected state\n");
		exit(1);
	}
}

void handle_client(uint32_t event, struct connection *conn)
{
	/* TODO: Handle new client. There can be input and output connections.
	 * Take care of what happened at the end of a connection.
	 */
	// https://man7.org/linux/man-pages/man2/epoll_ctl.2.html
	// int epoll_ctl(int epfd, int op, int fd, struct epoll_event *_Nullable event);
	if (event & EPOLLIN) // if the bitmask EPOLLIN is set there is data to read
						// EPOLLIN The associated file is available for read(2) operations
		handle_input(conn); // handle input on the connection

	if (event & EPOLLOUT) // if the bitmask EPOLLOUT is set the socket is ready for writing
						//EPOLLOUT The associated file is available for write(2) operations.
		handle_output(conn); // handle output on the connection

	if (conn->state == STATE_DATA_SENT || conn->state == STATE_404_SENT || conn->state == STATE_CONNECTION_CLOSED)
		// defined in aws.h in enum connection_state
		// if the connection state indicates that data has been sent, a 404 response has been sent
		// or the connection is closed
		connection_remove(conn);
}

int main(void)
{
	int rc; // return code

	/* TODO: Initialize asynchronous operations. */

	// https://stackoverflow.com/questions/51436858/resources-associated-to-an-aio-context
	// https://docs.oracle.com/cd/E19957-01/816-5645-10/816-5645-10.pdf?utm_source=chatgpt.com
	// for general purpose .. the default value (128 requests).
	rc = io_setup(128, &ctx); // KAIO’s (kernel async I/O) special return codes must be returned from their source,
							// which has promised to call aio_complete(), all the way back to io_setup(),
							// which will call aio_complete() if it does not see the special error codes
							// from https://landley.net/kdocs/ols/2007/ols2007v1-pages-81-86.pdf
							// is in #include <libaio.h>

	/* TODO: Initialize multiplexing. */
	epollfd = w_epoll_create(); // from w_epoll.h it creates an epoll file descriptor that will be used
								// to monitor events on multiple file descriptors

	/* TODO: Create server socket. */
	// in aws.h AWS_LISTEN_PORT is defined as 8080
	// in sock_util.h DEFAULT_LISTEN_BACKLOG is defined as 5
	listenfd = tcp_create_listener(AWS_LISTEN_PORT, DEFAULT_LISTEN_BACKLOG); // from sock_util.h it creates a
																// TCP server socket listening on AWS_LISTEN_PORT

	/* TODO: Add server socket to epoll object*/
	rc = w_epoll_add_fd_in(epollfd, listenfd); // from w_epoll.h it adds the server socket to the epoll instance
												// to monitor incoming connection requests

	/* Uncomment the following line for debugging. */
	// dlog(LOG_INFO, "Server waiting for connections on port %d\n", AWS_LISTEN_PORT);

	/* server main loop */
	while (1) {
		struct epoll_event rev; // triggered event

		/* TODO: Wait for events. */
		rc = w_epoll_wait_infinite(epollfd, &rev); // from w_epoll.h it waits for events on the epoll instance
													// with infinite timeout and stores the triggered event in rev

		/* TODO: Switch event types; consider
		 *   - new connection requests (on server socket)
		 *   - socket communication (on connection sockets)
		 */
		if (rev.data.fd == listenfd) { // new connection request
			if (rev.events & EPOLLIN) // incoming connection request (the bitmask EPOLLIN is set)
				handle_new_connection(); // handle new connection request
		} else {
			// https://landley.net/kdocs/ols/2007/ols2007v1-pages-81-86.pdf
			// The KAIO interface can be provided by the kernel but implemented in terms of syslets instead of kiocbs.
			/* Existing client communication: passing the pointer from epoll data */
			handle_client(rev.events, (struct connection *)rev.data.ptr); // handle client communication
			// rev.data.ptr is a pointer to the connection structure associated with the client socket
			// rev.events is a bitmask indicating the type of event that occurred on the client socket
		}
	}

	return rc;
}
