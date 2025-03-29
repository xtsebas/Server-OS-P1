#pragma once
#include "crow.h"
#include <unordered_map>
#include <mutex>
#include <string>
#include <chrono>
#include <thread>

enum class UserStatus { ACTIVO = 1, OCUPADO = 2, INACTIVO = 3 };

struct ConnectionData {
    std::string username;
    std::string uuid;
    crow::websocket::connection* conn;
    UserStatus status;
    std::chrono::steady_clock::time_point last_active;
};

class WebSocketHandler {
public:
    static void on_open(crow::websocket::connection& conn, const std::string& username);
    static void on_message(crow::websocket::connection& conn, const std::string& data, bool is_binary);
    static void on_close(crow::websocket::connection& conn, const std::string& reason, uint16_t code);
    static void update_status(const std::string& username, UserStatus status, bool notify = true);
    static std::string list_users();
    static void start_inactivity_monitor();
    static void start_disconnection_cleanup();
    static void handle_list_users(crow::websocket::connection& conn);
    static void handle_get_user_info(crow::websocket::connection& conn, const std::string& data, size_t& offset);
    static void handle_change_status(crow::websocket::connection& conn, const std::string& sender, const std::string& data, size_t& offset);
    static void handle_send_message(crow::websocket::connection& conn, const std::string& sender, const std::string& data, size_t& offset);
    static void handle_get_history(crow::websocket::connection& conn, const std::string& sender, const std::string& data, size_t& offset);
    static uint8_t read_uint8(const std::string& data, size_t& offset);
    static std::string read_string_8(const std::string& data, size_t& offset);
    static void notify_user_joined(const std::string& username, UserStatus st);
    static void notify_user_status_change(const std::string& username, UserStatus st);
    static void notify_new_message(const std::string& sender, const std::string& msg, bool is_private, const std::string& recipient);
    static void send_private_message(const std::string& sender, const std::string& recipient, const std::string& msg);
    static void send_broadcast(const std::string& sender, const std::string& msg);
};
