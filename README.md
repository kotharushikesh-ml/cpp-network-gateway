# C++ Network Gateway

A multithreaded HTTP gateway built using modern C++ and POSIX sockets.

## Features

- TCP socket-based HTTP server
- HTTP request parsing and Content-Length handling
- HTTP Keep-Alive for local routes
- Thread pool for concurrent client connections
- Round-robin load balancing
- Two backend HTTP servers
- Basic TCP-connect backend health checks
- Thread-safe request logging
- Basic traffic policies and HTTP error responses

## Architecture

Client
  |
  v
Gateway (Port 8080)
  |
  v
Round-Robin Load Balancer
  |                 |
  v                 v
Backend 1         Backend 2
Port 9001         Port 9002

## Technologies

- C++
- TCP/IP
- POSIX sockets
- HTTP/1.1
- Multithreading
- Mutexes and condition variables
- Linux / WSL

## Compilation

Compile the backends:

    g++ backend1.cpp -o backend1
    g++ backend2.cpp -o backend2

Compile the gateway:

    g++ server.cpp http_request.cpp http_response.cpp \
        thread_pool.cpp load_balancer.cpp logger.cpp policy.cpp \
        -o server -pthread

## Running

Start each program in a separate terminal:

    ./backend1
    ./backend2
    ./server

## Testing

    curl -i http://127.0.0.1:8080/hello
    curl -i http://127.0.0.1:8080/proxy
    curl -i http://127.0.0.1:8080/admin
    curl -i http://127.0.0.1:8080/unknown
    curl -i -X POST http://127.0.0.1:8080/hello

## Limitations

This is an educational networking project, not a production-ready
HTTP proxy or security gateway. It does not implement TLS or full
HTTP/1.1 protocol support.