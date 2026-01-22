# Asynchronous Web Server (AWS)

## 📋 Overview

The **Asynchronous Web Server (AWS)** is a high-performance Linux-based web server designed to serve files efficiently by leveraging advanced I/O paradigms. The primary goal of this project is to minimize CPU overhead and memory latency through asynchronous operations and zero-copy data transfer.

### Key Technical Features:

* **I/O Multiplexing**: Utilizes the `epoll` API for high-scale, event-driven management of multiple simultaneous client connections.
* **Zero-Copying**: Implements `sendfile` for static content, bypassing user-space data buffering to accelerate throughput.
* **Asynchronous File I/O**: Employs Linux AIO (`io_setup`, `io_submit`) for dynamic file processing, ensuring the server remains responsive during heavy disk operations.
* **Non-blocking Sockets**: All network operations are performed on non-blocking sockets to prevent thread starvation and maximize scalability.
* **Connection State Machine**: Each client session is governed by a dedicated state machine to track the lifecycle of HTTP requests and responses.


## 🏗️ Server Architecture

The server categorizes content based on its location within the `AWS_DOCUMENT_ROOT` directory:

1. **Static Files (`/static/`)**:
* Designed for assets that require no post-processing.
* **Mechanism**: Handled via `sendfile` (Zero-copy) for maximum efficiency.


2. **Dynamic Files (`/dynamic/`)**:
* Designed for files that theoretically require server-side processing.
* **Mechanism**: Read from disk using the **Asynchronous API (AIO)** and pushed to clients via non-blocking sockets.


### HTTP Implementation:

* Implements a functional subset of the **HTTP/1.1** protocol.
* **Response Codes**: `200 OK` for successful retrievals and `404 Not Found` for invalid paths.
* **Parsing**: Utilizes a callback-based `http-parser` to extract resource paths and headers efficiently.

## 📂 Project Structure

```text
.
├── aws.c               # Core server implementation (epoll loop, connection logic)
├── aws.h               # Macros, data structures, and configuration (port, root dir)
├── http-parser/        # External HTTP parsing library
├── tests/              # Automated testing suite
└── Makefile            # Build instructions

```

## 🛠️ Installation & Testing

### Prerequisites

* A Linux-based environment (Kernel support for `epoll` and `eventfd` is required).
* `gcc` compiler and `make` utility.

### Compilation

Build the executable by running the following command in the root directory:

```bash
make

```

### Running the Automated Suite

The testing suite validates server functionality, API usage (sendfile, epoll, io_submit), and monitors for memory leaks.

```bash
cd tests/
make check

```

To execute a specific test case (e.g., Test 31):

```bash
./_test/run_test.sh 31

```


## ⚙️ Technical Deep-Dive

### Connection State Machine

To manage asynchronous events, each `connection` structure maintains a state:

* `STATE_RECEIVING`: Reading and parsing the incoming HTTP request.
* `STATE_SENDING_HEADER`: Constructing and transmitting the HTTP response header.
* `STATE_SENDING_DATA`: Streaming the file content (via `sendfile` or `AIO`).
* `STATE_CLOSING`: Releasing resources and terminating the socket connection.

### Advanced Linux APIs Utilized:

* **Multiplexing**: `epoll_create`, `epoll_ctl`, `epoll_wait`.
* **Zero-Copy**: `sendfile`.
* **Async I/O**: `io_setup`, `io_submit`, `io_getevents`, `eventfd`.

---

## 📝 Performance & Scalability

By combining event-driven multiplexing with asynchronous disk access, this server mitigates the "C10k problem." It minimizes context switching and memory copies, making it significantly more efficient than traditional thread-per-connection models.


**Developed by:** Daria-Ioana Drăghici]

**Project:** Operating Systems - Advanced Asynchronous Web Server Implementation

Would you like me to generate a **Short Description** or a list of **Topics/Tags** for your GitHub repository settings?
