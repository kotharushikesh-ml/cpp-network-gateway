#ifndef LOAD_BALANCER_HPP
#define LOAD_BALANCER_HPP

#include <mutex>
#include <string>
#include <vector>

struct Backend
{
    std::string host;
    int port;
};

class LoadBalancer
{
public:
    explicit LoadBalancer(std::vector<Backend> backendList);
    std::string forward(const std::string& rawRequest);

private:
    std::vector<Backend> backends;
    std::size_t nextBackend = 0;
    std::mutex mutex;

    int getHealthyBackendIndex();
    bool isBackendHealthy(const Backend& backend) const;
};

#endif
