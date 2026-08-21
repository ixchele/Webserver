*This project has been created as part of the 42 curriculum by zbengued, aabouriz.*

# Webserv

## Description
The goal of this project is to write an HTTP/1.1 server from scratch in C++98. It is designed to act as a lightweight version of NGINX, capable of handling multiple concurrent connections asynchronously without crashing or leaking memory.

By building this server, we gained a deep understanding of network programming, the HTTP protocol, non-blocking I/O operations, and system-level file management. The server parses a custom configuration file to manage multiple virtual servers, routes requests based on longest-prefix matching, serves static files, generates dynamic directory listings (autoindex), and executes CGI scripts.

## Instructions

### Compilation
The project is built using `make`. It strictly compiles under C++98 standards with `-Wall -Wextra -Werror`.

```bash
# Compile the project
make

# Recompile from scratch
make re


# Execution

Run the server by providing a configuration file. If no file is provided, a default configuration will be used.
```bash
./webserv [path/to/config.conf]

```
# Usage Example

Start the server: ./webserv configs/default.conf

Open a web browser or use curl to test the endpoints:


```bash
curl -i http://localhost:8080/
curl -X POST -d "data=test" http://localhost:8080/upload

```


# Technical Choices & Features

Asynchronous I/O: Utilizes epoll for non-blocking socket monitoring, managed through a robust Client State Machine (IDLE, RECEIVING, SENDING).

Memory-Safe File Streaming: Static files (like large videos) are never loaded entirely into RAM. They are streamed to the client in 8Kb chunks using std::ifstream and temporary buffers.

Smart Configuration Routing: Implements a polymorphic CommonConfig structure inherited by ServerConfig and LocationConfig to handle cascading directives and longest-prefix route matching.

# Resources

Classic References:

    RFC 9112 (HTTP/1.1) - The official HTTP/1.1 protocol specification.

    Beej's Guide to Network Programming - Essential resource for understanding POSIX sockets.

    NGINX Documentation - Used as the reference behavior for configuration inheritance, longest-prefix matching, and autoindex generation.

    Linux manual pages (man epoll, man stat, man opendir).
    
    Linux programming interface

# AI Usage:
Artificial Intelligence (LLM) was used as an interactive thought partner and technical documentation assistant during this project, specifically for:

    Architecture Validation: Discussing and refining the C++ class structure, particularly validating the CommonConfig inheritance pattern and the state-machine design for epoll clients.

    System API Debugging: Clarifying the exact behavior of POSIX system calls (like the differences between stat() and access(), or safely reading directories with opendir() and DT_DIR).

    Implementation Strategies: Brainstorming the manual chunk-streaming strategy in the _send_data() method to ensure large files could be sent via non-blocking sockets without exceeding memory limits.

    Note: AI was not used to automatically generate the core logic or bypass the learning objectives; it acted as a peer for code review and architectural brainstorming.
