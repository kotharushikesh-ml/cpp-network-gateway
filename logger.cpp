#include "logger.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>

namespace
{
std::mutex logMutex;
}

void Logger::log(LogLevel level, const std::string& message)
{
    std::lock_guard<std::mutex> lock(logMutex);

    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);

    const char* label = "INFO";
    if (level == LogLevel::WARN) label = "WARN";
    if (level == LogLevel::ERROR) label = "ERROR";

    std::tm timeInfo{};
    localtime_r(&time, &timeInfo);

    std::cout << '[' << std::put_time(&timeInfo, "%Y-%m-%d %H:%M:%S")
              << "] [" << label << "] " << message << std::endl;
}
