#include "../include/websocket_handler.h"
#include "../include/logger.h"
#include <sstream>
#include <iostream>
#include <ctime>
#include <mutex>

// Mapa de conexiones activas
std::unordered_map<std::string, ConnectionData> connections;
std::mutex connections_mutex;

// Mapa que recuerda el último estado del usuario, incluso si está desconectado
std::unordered_map<std::string, UserStatus> user_last_status;

// Función auxiliar para generar un UUID
std::string generate_uuid()
{
    std::ostringstream ss;
    static int counter = 0;
    ss << std::hex << std::time(nullptr) << counter++;
    return ss.str();
}

void WebSocketHandler::on_open(crow::websocket::connection &conn, const std::string &username)
{
    std::lock_guard<std::mutex> lock(connections_mutex);
    std::string client_ip = conn.get_remote_ip();

    if (connections.find(username) != connections.end())
    {
        conn.send_text("Error: El nombre de usuario ya está en uso.");
        conn.close("Nombre duplicado.");
        return;
    }
    
    // Obtener estado anterior (si existe), sino ACTIVO
    UserStatus estado_inicial = UserStatus::ACTIVO;
    if (user_last_status.count(username) > 0) {
        estado_inicial = user_last_status[username];
    }

    // Generar UUID para el usuario
    std::string user_uuid = generate_uuid();

    // Guardar la conexión
    connections[username] = {username, user_uuid, &conn, estado_inicial};
    Logger::getInstance().log("Nueva conexión: " + username + " (UUID: " + user_uuid + ") desde " + client_ip);

    // Notificar a otros usuarios
    for (auto &[_, conn_data] : connections)
    {
        if (conn_data.conn && conn_data.conn != &conn)
        {
            conn_data.conn->send_text("Usuario conectado: " + username);
        }
    }
}

void WebSocketHandler::on_message(crow::websocket::connection &conn, const std::string &data, bool is_binary)
{
    std::string sender = "Desconocido";

    // Buscamos al usuario sin mutex aquí
    {
        std::lock_guard<std::mutex> lock(connections_mutex);
        for (auto &[username, conn_data] : connections)
        {
            if (conn_data.conn == &conn)
            {
                sender = username;
                break;
            }
        }
    }

    Logger::getInstance().log("Mensaje recibido de " + sender + ": " + data);

    auto json = crow::json::load(data);
    if (json) {
        if (json.has("type") && json["type"].s() == "status_update") {
            std::string new_status = json["status"].s();
            if (new_status == "ACTIVO")
                update_status(sender, UserStatus::ACTIVO);
            else if (new_status == "OCUPADO")
                update_status(sender, UserStatus::OCUPADO);
            else if (new_status == "INACTIVO")
                update_status(sender, UserStatus::INACTIVO);
            return;
        }

        if (json.has("type") && json["type"].s() == "private") {
            if (!json.has("to") || !json.has("message")) {
                Logger::getInstance().log("ERROR: mensaje privado malformado.");
                return;
            }

            std::string recipient = json["to"].s();
            std::string msg = json["message"].s();

            Logger::getInstance().log("DEBUG: Se detectó mensaje privado de " + sender +
                " hacia " + recipient + " contenido: " + msg);

            send_private_message(sender, recipient, msg);
            return;
        }
    }

    // Broadcast normal
    std::lock_guard<std::mutex> lock(connections_mutex);
    for (auto &[_, conn_data] : connections)
    {
        if (conn_data.conn)
        {
            conn_data.conn->send_text(sender + ": " + data + "\n");
        }
    }
}

void WebSocketHandler::on_close(crow::websocket::connection &conn, const std::string &reason, uint16_t code)
{
    std::lock_guard<std::mutex> lock(connections_mutex);

    std::string disconnected_user;
    for (auto it = connections.begin(); it != connections.end(); ++it)
    {
        if (it->second.conn == &conn)
        {
            disconnected_user = it->first;
            user_last_status[disconnected_user] = it->second.status;
            connections.erase(it);
            break;
        }
    }

    Logger::getInstance().log("Conexión cerrada: " + disconnected_user + " - " + reason + " (Código " + std::to_string(code) + ")");
}

void WebSocketHandler::update_status(const std::string &username, UserStatus status)
{
    auto it = connections.find(username);
    if (it != connections.end())
    {
        it->second.status = status;

        std::string status_str = (status == UserStatus::ACTIVO) ? "ACTIVO" : (status == UserStatus::OCUPADO) ? "OCUPADO"
                                                                                                             : "INACTIVO";

        std::string message = username + " cambió su estado a " + status_str;
        Logger::getInstance().log(message);

        for (auto &[_, conn_data] : connections)
        {
            if (conn_data.conn)
            {
                conn_data.conn->send_text(message);
            }
        }
    }
}

std::string WebSocketHandler::list_users()
{
    std::ostringstream oss;
    std::lock_guard<std::mutex> lock(connections_mutex);
    oss << "Usuarios conectados:\n";
    for (const auto &[username, conn_data] : connections)
    {
        std::string status_str = (conn_data.status == UserStatus::ACTIVO) ? "ACTIVO" : (conn_data.status == UserStatus::OCUPADO) ? "OCUPADO"
                                                                                                                                 : "INACTIVO";
        oss << "- " << username << " (" << status_str << ")\n";
    }
    return oss.str();
}

void WebSocketHandler::send_private_message(const std::string& sender, const std::string& recipient, const std::string& msg)
{
    Logger::getInstance().log("DEBUG: send_private_message FROM=" + sender +
                          " TO=" + recipient + " MSG=" + msg);

    crow::websocket::connection* recipient_conn = nullptr;
    crow::websocket::connection* sender_conn = nullptr;

    {
        std::lock_guard<std::mutex> lock(connections_mutex);

        // Buscar destinatario
        auto it = connections.find(recipient);
        if (it != connections.end()) {
            Logger::getInstance().log("AQUI SI ENTRO1");
            recipient_conn = it->second.conn;
        }

        // Buscar emisor
        auto sender_it = connections.find(sender);
        if (sender_it != connections.end()) {
            Logger::getInstance().log("AQUI SI ENTRO2");
            sender_conn = sender_it->second.conn;
        }

        // Si destinatario no existe, enviar error al emisor y salir
        if (!recipient_conn) {
            Logger::getInstance().log("AQUI SI ENTRO3");
            if (sender_conn) {
                Logger::getInstance().log("AQUI SI ENTRO4");
                sender_conn->send_text("Error: El usuario '" + recipient + "' no existe o está desconectado.\n");
            }
            return;
        }
    }
    Logger::getInstance().log("AQUI SI ENTRO5");
    // Enviar al destinatario
    if (recipient_conn) {
        Logger::getInstance().log("AQUI SI ENTRO6");
        recipient_conn->send_text("[PRIVADO] " + sender + ": " + msg + "\n");
    }

    // Confirmación al emisor
    if (sender_conn) {
        Logger::getInstance().log("AQUI SI ENTRO7");
        sender_conn->send_text("[PRIVADO a " + recipient + "] " + sender + ": " + msg + "\n");
    }
}