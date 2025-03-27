#include "../include/websocket_handler.h"
#include "../include/logger.h"
#include <sstream>
#include <iostream>
#include <ctime>
#include <mutex>

std::unordered_map<std::string, ConnectionData> connections;
std::mutex connections_mutex;
static std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> chat_history;

std::string generate_uuid() {
    std::ostringstream ss;
    static int counter = 0;
    ss << std::hex << std::time(nullptr) << counter++;
    return ss.str();
}

static void send_error(crow::websocket::connection& conn, uint8_t error_code) {
    std::string payload;
    payload.push_back((char)0x50);
    payload.push_back((char)error_code);
    conn.send_binary(payload);
}

static uint8_t userStatusToByte(UserStatus st) {
    switch (st) {
        case UserStatus::ACTIVO:   return 1;
        case UserStatus::OCUPADO:  return 2;
        case UserStatus::INACTIVO: return 3;
    }
    return 0;
}

uint8_t WebSocketHandler::read_uint8(const std::string& data, size_t& offset) {
    if (offset >= data.size()) {
        throw std::runtime_error("read_uint8 out of range");
    }
    uint8_t val = static_cast<uint8_t>(data[offset]);
    offset += 1;
    return val;
}

std::string WebSocketHandler::read_string_8(const std::string& data, size_t& offset) {
    uint8_t length = read_uint8(data, offset);
    if (offset + length > data.size()) {
        throw std::runtime_error("read_string_8 out of range");
    }
    std::string s = data.substr(offset, length);
    offset += length;
    return s;
}

void WebSocketHandler::notify_user_joined(const std::string& username, UserStatus st) {
    std::lock_guard<std::mutex> lock(connections_mutex);
    std::string payload;
    payload.push_back((char)0x53);
    payload.push_back((char)username.size());
    payload += username;
    payload.push_back((char)userStatusToByte(st));
    for (auto& [_, conn_data] : connections) {
        if (conn_data.conn) {
            conn_data.conn->send_binary(payload);
        }
    }
}

void WebSocketHandler::notify_user_status_change(const std::string& username, UserStatus st) {
    std::lock_guard<std::mutex> lock(connections_mutex);
    std::string payload;
    payload.push_back((char)0x54);
    payload.push_back((char)username.size());
    payload += username;
    payload.push_back((char)userStatusToByte(st));
    for (auto& [_, conn_data] : connections) {
        if (conn_data.conn) {
            conn_data.conn->send_binary(payload);
        }
    }
}

void WebSocketHandler::notify_new_message(const std::string& sender, const std::string& msg, bool is_private, const std::string& recipient) {
    std::string payload;
    payload.push_back((char)0x55);
    payload.push_back((char)sender.size());
    payload += sender;
    payload.push_back((char)msg.size());
    payload += msg;
    if (is_private) {
        std::lock_guard<std::mutex> lock(connections_mutex);
        auto itA = connections.find(sender);
        auto itB = connections.find(recipient);
        if (itA != connections.end() && itA->second.conn) {
            itA->second.conn->send_binary(payload);
        }
        if (itB != connections.end() && itB->second.conn) {
            itB->second.conn->send_binary(payload);
        }
    } else {
        std::lock_guard<std::mutex> lock(connections_mutex);
        for (auto& [uname, cd] : connections) {
            if (cd.conn) {
                cd.conn->send_binary(payload);
            }
        }
    }
}

void WebSocketHandler::handle_list_users(crow::websocket::connection& conn) {
    std::string list = list_users();
    conn.send_binary(std::string(1, (char)1) + list);
}

void WebSocketHandler::handle_get_user_info(crow::websocket::connection& conn, const std::string& data, size_t& offset) {
    std::string requested_name = read_string_8(data, offset);
    std::lock_guard<std::mutex> lock(connections_mutex);
    auto it = connections.find(requested_name);
    if (it == connections.end()) {
        send_error(conn, 1);
        return;
    }
    UserStatus st = it->second.status;
    std::string payload;
    payload.push_back((char)0x52);
    payload.push_back((char)requested_name.size());
    payload += requested_name;
    payload.push_back((char)userStatusToByte(st));
    conn.send_binary(payload);
}

void WebSocketHandler::handle_change_status(crow::websocket::connection& conn, const std::string& sender, const std::string& data, size_t& offset) {
    uint8_t raw_status = read_uint8(data, offset);
    UserStatus newStatus;
    switch(raw_status) {
        case 1: newStatus = UserStatus::ACTIVO; break;
        case 2: newStatus = UserStatus::OCUPADO; break;
        case 3: newStatus = UserStatus::INACTIVO; break;
        default:
            send_error(conn, 2);
            return;
    }
    update_status(sender, newStatus);
}

void WebSocketHandler::handle_send_message(crow::websocket::connection& conn, const std::string& sender, const std::string& data, size_t& offset) {
    std::string destino = read_string_8(data, offset);
    std::string mensaje = read_string_8(data, offset);
    if (mensaje.empty()) {
        send_error(conn, 3);
        return;
    }
    if (destino == "~") {
        send_broadcast(sender, mensaje);
    } else {
        send_private_message(sender, destino, mensaje);
    }
}

void WebSocketHandler::handle_get_history(crow::websocket::connection& conn, const std::string& sender, const std::string& data, size_t& offset) {
    std::string target = read_string_8(data, offset);
    std::string chat_id = (target == "~") 
        ? "~" 
        : ((sender < target) ? (sender + "|" + target) : (target + "|" + sender));

    auto it = chat_history.find(chat_id);
    if (it == chat_history.end()) {
        std::string empty;
        empty.push_back((char)0x56);
        empty.push_back((char)0);
        conn.send_binary(empty);
        return;
    }
    const auto& messages = it->second;
    uint8_t num_msgs = (messages.size() > 255) ? 255 : static_cast<uint8_t>(messages.size());
    std::string payload;
    payload.push_back((char)0x56);
    payload.push_back((char)num_msgs);
    for (size_t i = 0; i < num_msgs; i++) {
        const auto& [author, text] = messages[i];
        payload.push_back((char)author.size());
        payload += author;
        payload.push_back((char)text.size());
        payload += text;
    }
    conn.send_binary(payload);
}

void WebSocketHandler::on_open(crow::websocket::connection &conn, const std::string &username) {
    std::lock_guard<std::mutex> lock(connections_mutex);
    std::string client_ip = conn.get_remote_ip();
    if (connections.find(username) != connections.end()) {
        conn.send_text("Error: El nombre de usuario ya está en uso.");
        conn.close("Nombre duplicado.");
        return;
    }
    std::string user_uuid = generate_uuid();
    connections[username] = {username, user_uuid, &conn, UserStatus::ACTIVO};
    Logger::getInstance().log("Nueva conexión: " + username + " (UUID: " + user_uuid + ") desde " + client_ip);
    notify_user_joined(username, UserStatus::ACTIVO);
}

void WebSocketHandler::on_message(crow::websocket::connection &conn, const std::string &data, bool is_binary) {
    if (!is_binary) {
        Logger::getInstance().log("Mensaje de texto recibido. El protocolo exige binario, se ignora.");
        return;
    }
    std::string sender = "Desconocido";
    {
        std::lock_guard<std::mutex> lock(connections_mutex);
        for (auto &[uname, conn_data] : connections) {
            if (conn_data.conn == &conn) {
                sender = uname;
                conn_data.last_active = std::chrono::steady_clock::now();
                break;
            }
        }
    }
    try {
        size_t offset = 0;
        uint8_t opcode = read_uint8(data, offset);
        Logger::getInstance().log("Binario op=" + std::to_string(opcode) + " from " + sender);
        switch(opcode) {
            case 1:
                handle_list_users(conn);
                break;
            case 2:
                handle_get_user_info(conn, data, offset);
                break;
            case 3:
                handle_change_status(conn, sender, data, offset);
                break;
            case 4:
                handle_send_message(conn, sender, data, offset);
                break;
            case 5:
                handle_get_history(conn, sender, data, offset);
                break;
            default:
                Logger::getInstance().log("Opcode desconocido: " + std::to_string(opcode));
                break;
        }
    } catch (std::exception& e) {
        Logger::getInstance().log("Error parseando binario: " + std::string(e.what()));
    }
}

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

void WebSocketHandler::update_status(const std::string &username, UserStatus status, bool notify) {
    std::lock_guard<std::mutex> lock(connections_mutex);
    auto it = connections.find(username);
    if (it != connections.end()) {
        it->second.status = status;
        it->second.last_active = std::chrono::steady_clock::now();
        Logger::getInstance().log(username + " cambió su estado");
        if (notify) {
            notify_user_status_change(username, status);
        }
    }
}


std::string WebSocketHandler::list_users() {
    std::ostringstream oss;
    {
        std::lock_guard<std::mutex> lock(connections_mutex);
        oss << "Usuarios conectados:\n";
        for (const auto &[username, conn_data] : connections) {
            uint8_t st = userStatusToByte(conn_data.status);
            oss << "- " << username << " (status=" << (int)st << ")\n";
        }
    }
    return oss.str();
}

void WebSocketHandler::send_private_message(const std::string& sender, const std::string& recipient, const std::string& msg) {
    std::lock_guard<std::mutex> lock(connections_mutex);
    auto it = connections.find(recipient);
    if (it == connections.end()) {
        auto se = connections.find(sender);
        if (se != connections.end() && se->second.conn) {
            se->second.conn->send_text("Error: '" + recipient + "' no conectado.\n");
        }
        return;
    }
    std::string chat_id = (sender < recipient) ? (sender + "|" + recipient) : (recipient + "|" + sender);
    chat_history[chat_id].push_back({sender, msg});
    notify_new_message(sender, msg, true, recipient);
}

void WebSocketHandler::send_broadcast(const std::string& sender, const std::string& msg) {
    std::lock_guard<std::mutex> lock(connections_mutex);
    chat_history["~"].push_back({sender, msg});
    notify_new_message(sender, msg, false, "");
}

void WebSocketHandler::start_inactivity_monitor() {
    std::thread([] {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            std::lock_guard<std::mutex> lock(connections_mutex);
            auto now = std::chrono::steady_clock::now();
            for (auto& [username, conn_data] : connections) {
                if (conn_data.status != UserStatus::INACTIVO) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - conn_data.last_active).count();
                    if (elapsed >= 60) {
                        conn_data.status = UserStatus::INACTIVO;
                        notify_user_status_change(username, UserStatus::INACTIVO);
                    }
                }
            }
        }
    }).detach();
}

