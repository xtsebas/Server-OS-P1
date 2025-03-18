#pragma once
#include "crow.h"
#include <unordered_map>
#include <mutex>
#include <string>

struct ConnectionData {
    std::string username;
    std::string uuid;
    crow::websocket::connection* conn;
};

class WebSocketHandler {
public:
    static void on_open(crow::websocket::connection& conn, const std::string& username);
    static void on_message(crow::websocket::connection& conn, const std::string& data, bool is_binary);
    static void on_close(crow::websocket::connection& conn, const std::string& reason, uint16_t code);
};