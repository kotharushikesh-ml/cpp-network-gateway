#ifndef POLICY_HPP
#define POLICY_HPP

#include "http_request.hpp"

enum class PolicyResult
{
    ALLOW,
    METHOD_NOT_ALLOWED,
    FORBIDDEN
};

class TrafficPolicy
{
public:
    static PolicyResult evaluate(const HttpRequest& request);
};

#endif
