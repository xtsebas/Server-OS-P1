#pragma once
#include "crow.h"
#include <unordered_map>
#include <mutex>
#include <string>

enum class UserStatus { ACTIVO = 1, OCUPADO = 2, INACTIVO = 3 };

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

    // Manejo de estados y usuarios
    static void update_status(const std::string& username, UserStatus status);
    static std::string list_users();

    // Envío de mensaje (puede ser privado o broadcast)
    static void send_private_message(const std::string& sender, const std::string& recipient, const std::string& msg);
    static void send_broadcast(const std::string& sender, const std::string& msg);

private:
    // Funciones auxiliares para parsear binario
    static uint8_t read_uint8(const std::string& data, size_t& offset);
    static std::string read_string_8(const std::string& data, size_t& offset);

    // Manejadores para cada código de operación
    static void handle_list_users(crow::websocket::connection& conn);                 // op=1
    static void handle_change_status(crow::websocket::connection& conn, const std::string& sender, const std::string& data, size_t& offset); // op=3
    static void handle_send_message(crow::websocket::connection& conn, const std::string& sender, const std::string& data, size_t& offset);  // op=4
};
