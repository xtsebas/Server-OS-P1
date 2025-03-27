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

// -------------- Parsers binarios auxiliares ---------------
uint8_t WebSocketHandler::read_uint8(const std::string& data, size_t& offset) {
    if (offset >= data.size()) {
        throw std::runtime_error("read_uint8 out of range");
    }
    uint8_t val = static_cast<uint8_t>(data[offset]);
    offset += 1;
    return val;
}

// Lee un string precedido de un byte (longitud)
std::string WebSocketHandler::read_string_8(const std::string& data, size_t& offset) {
    uint8_t length = read_uint8(data, offset); // longitud
    if (offset + length > data.size()) {
        throw std::runtime_error("read_string_8 out of range");
    }
    std::string s = data.substr(offset, length);
    offset += length;
    return s;
}

// ----------------------------------------------------------

// Manejador op=1 (Listar usuarios)
void WebSocketHandler::handle_list_users(crow::websocket::connection& conn) {
    // A discreción, podemos usar la ruta HTTP /users. Pero si el protocolo define binario, enviamos binario.
    // Por simplicidad, enviamos un texto enumerando usuarios.
    std::string userList = list_users();
    conn.send_binary(std::string(1, (char)1) + userList); 
    // Ejemplo: primer byte es 1 (mismo opcode) o un "código" de respuesta
}

// Manejador op=3 (Cambiar estado)
void WebSocketHandler::handle_change_status(crow::websocket::connection& conn, const std::string& sender, const std::string& data, size_t& offset) {
    // Protocolo dice: Nombre, Estado
    // Pero ya tenemos "sender" (no hace falta nombre?)
    // Sin embargo, si el doc dice que el payload lleva Nombre, parsealo.

    // Ejemplo: parse Nombre (opcional)
    // std::string name = read_string_8(data, offset);

    // parse estado
    uint8_t raw_status = read_uint8(data, offset); // 1=ACTIVO,2=OCUPADO,3=INACTIVO
    UserStatus newStatus = (raw_status == 1) ? UserStatus::ACTIVO :
                           (raw_status == 2) ? UserStatus::OCUPADO :
                           UserStatus::INACTIVO;

    update_status(sender, newStatus);
}

// Manejador op=4 (Enviar mensaje)
void WebSocketHandler::handle_send_message(crow::websocket::connection& conn, const std::string& sender, const std::string& data, size_t& offset) {
    // En el protocolo: Enviar mensaje => [Dest, Msg]
    std::string destino = read_string_8(data, offset);
    std::string mensaje = read_string_8(data, offset);

    if (destino == "~") {
        // broadcast
        send_broadcast(sender, mensaje);
    } else {
        // privado
        send_private_message(sender, destino, mensaje);
    }
}

// ----------------------------------------------------------

void WebSocketHandler::on_open(crow::websocket::connection &conn, const std::string &username) {
    std::lock_guard<std::mutex> lock(connections_mutex);
    std::string client_ip = conn.get_remote_ip();

    if (connections.find(username) != connections.end()) {
        conn.send_text("Error: El nombre de usuario ya está en uso.");
        conn.close("Nombre duplicado.");
        return;
    }

    // Generar UUID
    std::string user_uuid = generate_uuid();
    connections[username] = {username, user_uuid, &conn, UserStatus::ACTIVO};

    Logger::getInstance().log("Nueva conexión: " + username + " (UUID: " + user_uuid + ") desde " + client_ip);

    // Notificar a otros
    for (auto &[_, conn_data] : connections) {
        if (conn_data.conn && conn_data.conn != &conn) {
            conn_data.conn->send_text("Usuario conectado: " + username);
        }
    }
}

// -------------- on_message con protocolo binario -----------
void WebSocketHandler::on_message(crow::websocket::connection &conn, const std::string &data, bool is_binary) {
    // Si no es binario, ignorar o cerrar
    if (!is_binary) {
        Logger::getInstance().log("Mensaje de texto recibido. El protocolo exige binario, se ignora.");
        return;
    }

    // Encontrar "sender"
    std::string sender = "Desconocido";
    {
        std::lock_guard<std::mutex> lock(connections_mutex);
        for (auto &[uname, conn_data] : connections) {
            if (conn_data.conn == &conn) {
                sender = uname;
                break;
            }
        }
    }

    try {
        size_t offset = 0;
        // Primer byte => opcode
        uint8_t opcode = read_uint8(data, offset);
        Logger::getInstance().log("Binario op=" + std::to_string(opcode) + " from " + sender);

        switch(opcode) {
            case 1: // Listar usuarios
                handle_list_users(conn);
                break;
            case 3: // Cambiar estado
                handle_change_status(conn, sender, data, offset);
                break;
            case 4: // Enviar mensaje
                handle_send_message(conn, sender, data, offset);
                break;
            default:
                Logger::getInstance().log("Opcode desconocido: " + std::to_string(opcode));
                break;
        }
    } catch (std::exception& e) {
        Logger::getInstance().log("Error parseando binario: " + std::string(e.what()));
    }
}
// -----------------------------------------------------------

void WebSocketHandler::on_close(crow::websocket::connection &conn, const std::string &reason, uint16_t code) {
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

void WebSocketHandler::update_status(const std::string &username, UserStatus status) {
    std::lock_guard<std::mutex> lock(connections_mutex);
    auto it = connections.find(username);
    if (it != connections.end()) {
        it->second.status = status;

        std::string status_str = (status == UserStatus::ACTIVO) ? "ACTIVO" :
                                 (status == UserStatus::OCUPADO) ? "OCUPADO" :
                                 "INACTIVO";

        std::string message = username + " cambió su estado a " + status_str;
        Logger::getInstance().log(message);

        // Notificar a todos
        for (auto &[_, conn_data] : connections) {
            if (conn_data.conn) {
                conn_data.conn->send_text(message);
            }
        }
    }
}

std::string WebSocketHandler::list_users() {
    std::ostringstream oss;
    {
        std::lock_guard<std::mutex> lock(connections_mutex);
        oss << "Usuarios conectados:\n";
        for (const auto &[username, conn_data] : connections) {
            std::string status_str = (conn_data.status == UserStatus::ACTIVO) ? "ACTIVO" :
                                     (conn_data.status == UserStatus::OCUPADO) ? "OCUPADO" : "INACTIVO";
            oss << "- " << username << " (" << status_str << ")\n";
        }
    }
    return oss.str();
}

// Enviar mensaje privado (ya existía)
void WebSocketHandler::send_private_message(const std::string& sender, const std::string& recipient, const std::string& msg)
{
    std::lock_guard<std::mutex> lock(connections_mutex);

    auto it = connections.find(recipient);
    if (it == connections.end()) {
        // Avisar error al emisor
        auto se = connections.find(sender);
        if (se != connections.end() && se->second.conn) {
            se->second.conn->send_text("Error: '" + recipient + "' no conectado.\n");
        }
        return;
    }
    // El destinatario existe
    auto& recip_conn = it->second.conn;
    if (recip_conn) {
        recip_conn->send_text("[PRIVADO] " + sender + ": " + msg + "\n");
    }

    // Retro al emisor
    auto se2 = connections.find(sender);
    if (se2 != connections.end() && se2->second.conn) {
        se2->second.conn->send_text("[PRIVADO a " + recipient + "] " + sender + ": " + msg + "\n");
    }
}

// Broadcast (opcional, si Destino=="~")
void WebSocketHandler::send_broadcast(const std::string& sender, const std::string& msg) {
    std::lock_guard<std::mutex> lock(connections_mutex);
    for (auto& [uname, conn_data] : connections) {
        if (conn_data.conn) {
            conn_data.conn->send_text(sender + ": " + msg + "\n");
        }
    }
}
