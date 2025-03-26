#pragma once
#include "crow.h"
#include <unordered_map>
#include <mutex>
#include <string>

enum class UserStatus { ACTIVO, OCUPADO, INACTIVO };

struct ConnectionData {
    std::string username;
    std::string uuid;
    crow::websocket::connection* conn;
    UserStatus status;
};

class WebSocketHandler {
public:
    static void on_open(crow::websocket::connection& conn, const std::string& username);
    static void on_message(crow::websocket::connection& conn, const std::string& data, bool is_binary);
    static void on_close(crow::websocket::connection& conn, const std::string& reason, uint16_t code);

    static void update_status(const std::string& username, UserStatus status);
    static std::string list_users();

    static void send_private_message(const std::string& sender, const std::string& recipient, const std::string& msg);
};

// Mapeo del estado de un user creado
extern std::unordered_map<std::string, UserStatus> user_last_status;

// Historial de mensajes en memoria
extern std::vector<std::string> general_chat_history;
extern std::map<std::pair<std::string, std::string>, std::vector<std::string>> private_chat_history;