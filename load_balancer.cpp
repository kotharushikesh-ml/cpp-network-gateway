#include "load_balancer.hpp"
#include "logger.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
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

int connectWithTimeout(const Backend& backend, int timeoutMs)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(backend.port));

    if (inet_pton(AF_INET, backend.host.c_str(), &address.sin_addr) != 1)
    {
        close(fd);
        return -1;
    }

    const int oldFlags = fcntl(fd, F_GETFL, 0);
    if (oldFlags < 0 || fcntl(fd, F_SETFL, oldFlags | O_NONBLOCK) < 0)
    {
        close(fd);
        return -1;
    }

    int result = connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    if (result < 0 && errno != EINPROGRESS)
    {
        close(fd);
        return -1;
    }

    if (result < 0)
    {
        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLOUT;

        result = poll(&pfd, 1, timeoutMs);
        if (result <= 0)
        {
            close(fd);
            return -1;
        }

        int socketError = 0;
        socklen_t length = sizeof(socketError);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketError, &length) < 0 ||
            socketError != 0)
        {
            close(fd);
            return -1;
        }
    }

    if (fcntl(fd, F_SETFL, oldFlags) < 0)
    {
        close(fd);
        return -1;
    }

    return fd;
}

std::string serviceUnavailable()
{
    const std::string body = "No backend healthy";
    return "HTTP/1.1 503 Service Unavailable\r\n"
           "Content-Type: text/plain; charset=utf-8\r\n"
           "Content-Length: " + std::to_string(body.size()) + "\r\n"
           "Connection: close\r\n\r\n" + body;
}
}

LoadBalancer::LoadBalancer(std::vector<Backend> backendList)
    : backends(std::move(backendList))
{
}

bool LoadBalancer::isBackendHealthy(const Backend& backend) const
{
    int fd = connectWithTimeout(backend, 500);
    if (fd < 0) return false;
    close(fd);
    return true;
}

int LoadBalancer::getHealthyBackendIndex()
{
    if (backends.empty()) return -1;

    for (std::size_t attempts = 0; attempts < backends.size(); ++attempts)
    {
        const std::size_t index = nextBackend;
        nextBackend = (nextBackend + 1) % backends.size();

        if (isBackendHealthy(backends[index]))
            return static_cast<int>(index);

        Logger::log(LogLevel::WARN,
                    "Backend unavailable: " + backends[index].host + ":" +
                    std::to_string(backends[index].port));
    }

    return -1;
}

std::string LoadBalancer::forward(const std::string& rawRequest)
{
    int index;
    Backend backend;

    {
        std::lock_guard<std::mutex> lock(mutex);
        index = getHealthyBackendIndex();
        if (index < 0)
            return serviceUnavailable();
        backend = backends[static_cast<std::size_t>(index)];
    }

    Logger::log(LogLevel::INFO,
                "Forwarding request to backend " + backend.host + ":" +
                std::to_string(backend.port));

    int fd = connectWithTimeout(backend, 1000);
    if (fd < 0)
    {
        Logger::log(LogLevel::WARN, "Backend connect failed after health check");
        return serviceUnavailable();
    }

    if (!sendAll(fd, rawRequest))
    {
        close(fd);
        return serviceUnavailable();
    }

    std::string response;
    char buffer[4096];

    while (true)
    {
        ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        response.append(buffer, static_cast<std::size_t>(n));

        if (response.size() > 2 * 1024 * 1024)
            break;
    }

    close(fd);

    if (response.empty())
        return serviceUnavailable();

    return response;
}
