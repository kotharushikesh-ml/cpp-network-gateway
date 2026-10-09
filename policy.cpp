#include "policy.hpp"

PolicyResult TrafficPolicy::evaluate(const HttpRequest& request)
{
    if (request.path == "/admin")
        return PolicyResult::FORBIDDEN;

    if (request.path == "/hello" || request.path == "/proxy")
    {
        if (request.method == "GET")
            return PolicyResult::ALLOW;

        return PolicyResult::METHOD_NOT_ALLOWED;
    }

    // Unknown paths proceed to routing and receive 404.
    return PolicyResult::ALLOW;
}
