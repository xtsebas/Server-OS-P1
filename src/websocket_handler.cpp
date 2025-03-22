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
std::string generate_uuid() {
    std::ostringstream ss;
    static int counter = 0;
    ss << std::hex << std::time(nullptr) << counter++;
    return ss.str();
}

void WebSocketHandler::on_open(crow::websocket::connection& conn, const std::string& username) {
    std::lock_guard<std::mutex> lock(connections_mutex);
    std::string client_ip = conn.get_remote_ip();

    if (connections.find(username) != connections.end()) {
        conn.send_text("Error: El nombre de usuario ya está en uso.");
        conn.close("Nombre duplicado.");
        return;
    }

    // Generar UUID para el usuario
    std::string user_uuid = generate_uuid();

    // Guardar la conexión
    connections[username] = {username, user_uuid, &conn};
    Logger::getInstance().log("Nueva conexión: " + username + " (UUID: " + user_uuid + ") desde " + client_ip);

    // Notificar a otros usuarios
    for (auto& [_, conn_data] : connections) {
        if (conn_data.conn && conn_data.conn != &conn) {
            conn_data.conn->send_text("Usuario conectado: " + username);
        }
    }
}

void WebSocketHandler::on_message(crow::websocket::connection& conn, const std::string& data, bool is_binary) {
    std::lock_guard<std::mutex> lock(connections_mutex);

    std::string sender = "Desconocido";
    for (auto& [username, conn_data] : connections) {
        if (conn_data.conn == &conn) {
            sender = username;
            break;
        }
    }

    Logger::getInstance().log("Mensaje recibido de " + sender + ": " + data);

    // Reenviar mensaje a todos los usuarios conectados
    for (auto& [_, conn_data] : connections) {
        if (conn_data.conn) {
            conn_data.conn->send_text(sender + ": " + data);
        }
    }
}

void WebSocketHandler::on_close(crow::websocket::connection& conn, const std::string& reason, uint16_t code) {
    std::lock_guard<std::mutex> lock(connections_mutex);

    std::string disconnected_user;
    for (auto it = connections.begin(); it != connections.end(); ++it) {
        if (it->second.conn == &conn) {
            disconnected_user = it->first;
            connections.erase(it);
            break;
        }
    }

    Logger::getInstance().log("Conexión cerrada: " + disconnected_user + " - " + reason + " (Código " + std::to_string(code) + ")");
}