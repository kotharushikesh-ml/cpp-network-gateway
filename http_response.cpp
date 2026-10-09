#include "http_response.hpp"

#include <sstream>

std::string HttpResponse::toString() const
{
    auto outputHeaders = headers;

    if (outputHeaders.find("Content-Length") == outputHeaders.end() &&
        outputHeaders.find("content-length") == outputHeaders.end())
    {
        outputHeaders["Content-Length"] = std::to_string(body.size());
    }

    if (outputHeaders.find("Content-Type") == outputHeaders.end() &&
        outputHeaders.find("content-type") == outputHeaders.end())
    {
        outputHeaders["Content-Type"] = "text/plain; charset=utf-8";
    }

    std::ostringstream out;
    out << "HTTP/1.1 " << statusCode << ' ' << statusText << "\r\n";

    for (const auto& header : outputHeaders)
        out << header.first << ": " << header.second << "\r\n";

    out << "\r\n" << body;
    return out.str();
}
