#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <string>

enum class LogLevel { INFO, WARN, ERROR };

class Logger
{
public:
    static void log(LogLevel level, const std::string& message);
};

#endif
