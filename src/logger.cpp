#include "../include/logger.h"
#include <iostream>
#include <chrono>
#include <ctime>

Logger::Logger() {
    logFile.open("server_logs.txt", std::ios::app);
    if (!logFile.is_open()) {
        std::cerr << "Error: No se pudo abrir el archivo de logs." << std::endl;
    }
    running = true;
}

Logger::~Logger() {
    stopLogging();
    if (logFile.is_open()) {
        logFile.close();
    }
}

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

void Logger::log(const std::string& message) {
    std::lock_guard<std::mutex> lock(queueMutex);
    
    // Obtener timestamp
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char timeBuffer[20];
    strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", localtime(&now));

    logQueue.push("[" + std::string(timeBuffer) + "] " + message);
    condition.notify_one();
}

void Logger::startLogging() {
    std::thread(&Logger::processLogs, this).detach();
}

void Logger::stopLogging() {
    running = false;
    condition.notify_one();
}

void Logger::processLogs() {
    while (running) {
        std::unique_lock<std::mutex> lock(queueMutex);
        condition.wait(lock, [this]() { return !logQueue.empty() || !running; });

        while (!logQueue.empty()) {
            logFile << logQueue.front() << std::endl;
            logQueue.pop();
        }

        logFile.flush();
    }
}
