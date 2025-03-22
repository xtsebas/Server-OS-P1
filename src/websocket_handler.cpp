#include "../include/websocket_handler.h"
#include "../include/logger.h"
#include <sstream>
#include <iostream>
#include <ctime>
#include <mutex>

// Mapa de conexiones activas
std::unordered_map<std::string, ConnectionData> connections;
std::mutex connections_mutex;

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

    // Generar UUID para el usuario
    std::string user_uuid = generate_uuid();

    // Guardar la conexión
    connections[username] = {username, user_uuid, &conn, UserStatus::ACTIVO};
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
    std::lock_guard<std::mutex> lock(connections_mutex);

    std::string sender = "Desconocido";
    for (auto &[username, conn_data] : connections)
    {
        if (conn_data.conn == &conn)
        {
            sender = username;
            break;
        }
    }

    Logger::getInstance().log("Mensaje recibido de " + sender + ": " + data);

    // Intentar parsear JSON (estado)
    auto json = crow::json::load(data);
    if (json && json.has("type") && json["type"].s() == "status_update")
    {
        std::string new_status = json["status"].s();
        if (new_status == "ACTIVO")
            update_status(sender, UserStatus::ACTIVO);
        else if (new_status == "OCUPADO")
            update_status(sender, UserStatus::OCUPADO);
        else if (new_status == "INACTIVO")
            update_status(sender, UserStatus::INACTIVO);
        return;
    }

    // Broadcast normal
    for (auto &[_, conn_data] : connections)
    {
        if (conn_data.conn)
        {
            conn_data.conn->send_text(sender + ": " + data);
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
