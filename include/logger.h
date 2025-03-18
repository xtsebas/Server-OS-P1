#pragma once
#include <queue>
#include <mutex>
#include <fstream>
#include <string>
#include <thread>
#include <condition_variable>

class Logger {
public:
    static Logger& getInstance();

    void log(const std::string& message);
    void startLogging();
    void stopLogging();

private:
    Logger();
    ~Logger();

    void processLogs();
    std::queue<std::string> logQueue;
    std::mutex queueMutex;
    std::condition_variable condition;
    std::ofstream logFile;
    bool running;
};
