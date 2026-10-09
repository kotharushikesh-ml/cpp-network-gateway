#include "http_request.hpp"
#include "http_response.hpp"
#include "thread_pool.hpp"
#include "load_balancer.hpp"
#include "logger.hpp"
#include "policy.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <unistd.h>

namespace
{
constexpr std::size_t MAX_REQUEST_SIZE = 1024 * 1024;
constexpr std::size_t MAX_HEADER_SIZE = 16 * 1024;
constexpr int MAX_REQUESTS_PER_CONNECTION = 100;

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

std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string trim(const std::string& value)
{
    const auto first = value.find_first_not_of(" \t");
    if (first == std::string::npos) return "";
    const auto last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1);
}

// Reads one request and preserves any extra bytes for the next request.
// This basic reader supports Content-Length bodies, not chunked transfer encoding.
bool readNextHttpRequest(int clientFd, std::string& pending, std::string& raw)
{
    std::size_t headerEnd = std::string::npos;

    while ((headerEnd = pending.find("\r\n\r\n")) == std::string::npos)
    {
        if (pending.size() >= MAX_HEADER_SIZE) return false;

        char buffer[4096];
        ssize_t n = recv(clientFd, buffer, sizeof(buffer), 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;

        pending.append(buffer, static_cast<std::size_t>(n));
        if (pending.size() > MAX_REQUEST_SIZE) return false;
    }

    const std::size_t headerSize = headerEnd + 4;
    if (headerSize > MAX_HEADER_SIZE) return false;

    const std::string headerText = pending.substr(0, headerEnd);
    const std::size_t firstLineEnd = headerText.find("\r\n");
    if (firstLineEnd == std::string::npos) return false;

    std::size_t contentLength = 0;
    bool foundContentLength = false;
    std::size_t pos = firstLineEnd + 2;

    while (pos < headerText.size())
    {
        std::size_t lineEnd = headerText.find("\r\n", pos);
        if (lineEnd == std::string::npos) lineEnd = headerText.size();

        const std::string line = headerText.substr(pos, lineEnd - pos);
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos || colon == 0) return false;

        const std::string name = toLower(trim(line.substr(0, colon)));
        const std::string value = trim(line.substr(colon + 1));

        if (name == "transfer-encoding")
            return false;

        if (name == "content-length")
        {
            if (foundContentLength || value.empty()) return false;

            for (unsigned char c : value)
                if (!std::isdigit(c)) return false;

            char* end = nullptr;
            errno = 0;
            unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
            if (errno == ERANGE || end == value.c_str() || *end != '\0')
                return false;
            if (parsed > MAX_REQUEST_SIZE) return false;

            contentLength = static_cast<std::size_t>(parsed);
            foundContentLength = true;
        }

        pos = lineEnd + 2;
    }

    if (contentLength > MAX_REQUEST_SIZE - headerSize) return false;
    const std::size_t totalSize = headerSize + contentLength;

    while (pending.size() < totalSize)
    {
        char buffer[4096];
        ssize_t n = recv(clientFd, buffer, sizeof(buffer), 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;

        pending.append(buffer, static_cast<std::size_t>(n));
        if (pending.size() > MAX_REQUEST_SIZE) return false;
    }

    raw = pending.substr(0, totalSize);
    pending.erase(0, totalSize);
    return true;
}

bool wantsKeepAlive(const HttpRequest& request)
{
    const auto it = request.headers.find("connection");
    const std::string connection =
        it == request.headers.end() ? "" : toLower(it->second);

    if (request.version == "HTTP/1.1")
        return connection != "close";

    return connection == "keep-alive";
}

HttpResponse makeResponse(int code, const std::string& reason,
                          const std::string& body, bool keepAlive)
{
    HttpResponse response;
    response.statusCode = code;
    response.statusText = reason;
    response.body = body;
    response.headers["Content-Type"] = "text/plain; charset=utf-8";
    response.headers["Connection"] = keepAlive ? "keep-alive" : "close";
    if (keepAlive)
        response.headers["Keep-Alive"] = "timeout=5, max=100";
    return response;
}

void handleClient(int clientFd, LoadBalancer& loadBalancer)
{
    timeval timeout{};
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    Logger::log(LogLevel::INFO, "Client connected");

    std::string pending;
    int requestsHandled = 0;

    while (requestsHandled < MAX_REQUESTS_PER_CONNECTION)
    {
        std::string raw;
        if (!readNextHttpRequest(clientFd, pending, raw))
            break;

        HttpRequest request;
        if (!parseRequest(raw, request))
        {
            const auto response = makeResponse(400, "Bad Request",
                                               "Malformed HTTP request", false);
            sendAll(clientFd, response.toString());
            Logger::log(LogLevel::WARN, "Malformed HTTP request -> 400");
            break;
        }

        ++requestsHandled;
        bool keepAlive = wantsKeepAlive(request) &&
                         requestsHandled < MAX_REQUESTS_PER_CONNECTION;

        const PolicyResult policy = TrafficPolicy::evaluate(request);

        if (policy == PolicyResult::FORBIDDEN)
        {
            HttpResponse response = makeResponse(
                403, "Forbidden", "Access denied", keepAlive);
            const std::string text = response.toString();
            sendAll(clientFd, text);
            Logger::log(LogLevel::WARN,
                        request.method + " " + request.path + " -> 403");
            if (!keepAlive) break;
            continue;
        }

        if (policy == PolicyResult::METHOD_NOT_ALLOWED)
        {
            HttpResponse response = makeResponse(
                405, "Method Not Allowed", "Method not allowed", keepAlive);
            response.headers["Allow"] = "GET";
            const std::string text = response.toString();
            sendAll(clientFd, text);
            Logger::log(LogLevel::WARN,
                        request.method + " " + request.path + " -> 405");
            if (!keepAlive) break;
            continue;
        }

        if (request.path == "/proxy")
        {
            // Backends currently return Connection: close, so proxy is close-only.
            const std::string response = loadBalancer.forward(raw);
            sendAll(clientFd, response);
            Logger::log(LogLevel::INFO, request.method + " /proxy -> proxied");
            break;
        }

        HttpResponse response;

        if (request.path == "/hello" && request.method == "GET")
        {
            response = makeResponse(200, "OK", "Hello from C++ Gateway!", keepAlive);
            Logger::log(LogLevel::INFO, "GET /hello -> 200");
        }
        else
        {
            response = makeResponse(404, "Not Found", "Not Found", keepAlive);
            Logger::log(LogLevel::WARN,
                        request.method + " " + request.path + " -> 404");
        }

        const std::string responseText = response.toString();
        if (!sendAll(clientFd, responseText))
            break;

        if (!keepAlive)
            break;
    }

    close(clientFd);
    Logger::log(LogLevel::INFO, "Client disconnected");
}
}

int main()
{
    const int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0)
    {
        perror("socket");
        return 1;
    }

    int reuse = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(8080);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(serverFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0)
    {
        perror("bind");
        close(serverFd);
        return 1;
    }

    if (listen(serverFd, 128) < 0)
    {
        perror("listen");
        close(serverFd);
        return 1;
    }

    std::vector<Backend> backends = {
        {"127.0.0.1", 9001},
        {"127.0.0.1", 9002}
    };
    LoadBalancer loadBalancer(backends);
    ThreadPool pool(4);

    Logger::log(LogLevel::INFO, "Gateway listening on 127.0.0.1:8080");

    while (true)
    {
        sockaddr_in clientAddress{};
        socklen_t clientLength = sizeof(clientAddress);
        int clientFd = accept(serverFd,
                              reinterpret_cast<sockaddr*>(&clientAddress),
                              &clientLength);

        if (clientFd < 0)
        {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }

        pool.enqueue([clientFd, &loadBalancer]
        {
            handleClient(clientFd, loadBalancer);
        });
    }

    close(serverFd);
    return 0;
}
