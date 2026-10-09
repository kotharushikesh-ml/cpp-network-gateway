#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

namespace
{
bool sendAll(int fd, const std::string& data)
{
    std::size_t sent = 0;
    while (sent < data.size())
    {
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}
}

int main()
{
    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) { perror("socket"); return 1; }

    int reuse = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(9001);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(serverFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0)
    { perror("bind"); close(serverFd); return 1; }

    if (listen(serverFd, 64) < 0)
    { perror("listen"); close(serverFd); return 1; }

    std::cout << "Backend 1 listening on 127.0.0.1:9001\n";

    while (true)
    {
        int clientFd = accept(serverFd, nullptr, nullptr);
        if (clientFd < 0) { if (errno == EINTR) continue; perror("accept"); continue; }

        char buffer[8192];
        ssize_t n = recv(clientFd, buffer, sizeof(buffer), 0);
        (void)n;

        const std::string body = "Hello from Backend 1!";
        const std::string response =
            "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " +
            std::to_string(body.size()) +
            "\r\nConnection: close\r\n\r\n" + body;

        sendAll(clientFd, response);
        close(clientFd);
    }
}
