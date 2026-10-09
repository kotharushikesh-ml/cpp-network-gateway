#include "http_request.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace
{
std::string lowerCopy(std::string value)
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
}

bool parseRequest(const std::string& raw, HttpRequest& request)
{
    request = HttpRequest{};

    const std::size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) return false;

    std::istringstream input(raw.substr(0, headerEnd));
    std::string requestLine;

    if (!std::getline(input, requestLine)) return false;
    if (!requestLine.empty() && requestLine.back() == '\r')
        requestLine.pop_back();

    std::istringstream lineParser(requestLine);
    if (!(lineParser >> request.method >> request.path >> request.version))
        return false;

    std::string extra;
    if (lineParser >> extra) return false;

    if (request.version != "HTTP/1.1" && request.version != "HTTP/1.0")
        return false;

    std::string line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        const std::size_t colon = line.find(':');
        if (colon == std::string::npos || colon == 0) return false;

        const std::string name = lowerCopy(trim(line.substr(0, colon)));
        const std::string value = trim(line.substr(colon + 1));

        if (name.empty()) return false;

        // Reject duplicate headers in this simple implementation.
        if (request.headers.find(name) != request.headers.end())
            return false;

        request.headers[name] = value;
    }

    request.body = raw.substr(headerEnd + 4);
    return true;
}
