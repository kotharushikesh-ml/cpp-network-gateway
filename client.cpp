#include <iostream>
#include <cstring>

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>

int main()
{
    // 1. Create socket
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (client_fd == -1) {
        std::cerr << "Socket creation failed\n";
        return 1;
    }

    // 2. Server address
    sockaddr_in server_addr{};

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);

    inet_pton(
        AF_INET,
        "127.0.0.1",
        &server_addr.sin_addr
    );

    // 3. Connect to server
    if (connect(
            client_fd,
            (struct sockaddr*)&server_addr,
            sizeof(server_addr)
        ) == -1) {

        std::cerr << "Connection failed\n";
        close(client_fd);
        return 1;
    }

    std::cout << "Connected to server!\n";

    // 4. Send HTTP request
    const char* message =
        "GET /hello HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n";

    send(
        client_fd,
        message,
        strlen(message),
        0
    );

    // 5. Receive HTTP response
    char buffer[1024];

    ssize_t bytes_received = recv(
        client_fd,
        buffer,
        sizeof(buffer) - 1,
        0
    );

    if (bytes_received > 0) {

        buffer[bytes_received] = '\0';

        std::cout << "Server response:\n";
        std::cout << buffer << '\n';
    }

    // 6. Close connection
    close(client_fd);

    return 0;
}