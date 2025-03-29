#include "../include/websocket_handler.h"
#include "../include/logger.h"
#include "../include/websocket_global.h"
#include <sstream>
#include <iostream>
#include <ctime>
#include <mutex>

bool testing_mode = false;
std::unordered_map<std::string, ConnectionData> connections;
std::mutex connections_mutex;
std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> chat_history;
std::unordered_map<std::string, UserStatus> last_user_status;
std::vector<std::pair<std::string, std::string>> general_chat_history;

std::string generate_uuid()
{
    std::ostringstream ss;
    static int counter = 0;
    ss << std::hex << std::time(nullptr) << counter++;
    return ss.str();
}

static void send_error(crow::websocket::connection &conn, uint8_t error_code)
{
    std::string payload;
    payload.push_back((char)0x50);
    payload.push_back((char)error_code);
    conn.send_binary(payload);
}

static uint8_t userStatusToByte(UserStatus st)
{
    switch (st)
    {
    case UserStatus::ACTIVO:
        return 1;
    case UserStatus::OCUPADO:
        return 2;
    case UserStatus::INACTIVO:
        return 3;
    }
    return 0;
}

uint8_t WebSocketHandler::read_uint8(const std::string &data, size_t &offset)
{
    if (offset >= data.size())
    {
        throw std::runtime_error("read_uint8 out of range");
    }
    uint8_t val = static_cast<uint8_t>(data[offset]);
    offset += 1;
    return val;
}

std::string WebSocketHandler::read_string_8(const std::string &data, size_t &offset)
{
    uint8_t length = read_uint8(data, offset);
    if (offset + length > data.size())
    {
        throw std::runtime_error("read_string_8 out of range");
    }
    std::string s = data.substr(offset, length);
    offset += length;
    return s;
}

void WebSocketHandler::notify_user_joined(const std::string &username, UserStatus st)
{   
    if (testing_mode) return;
    Logger::getInstance().log("ENTRO A NOTIFY ");
    std::lock_guard<std::mutex> lock(connections_mutex);
    Logger::getInstance().log("Enviando 0x53 a todos excepto: " + username);

    std::string payload;
    payload.push_back((char)0x53);
    payload.push_back((char)username.size());
    payload += username;
    payload.push_back((char)userStatusToByte(st));

    for (auto &[uname, conn_data] : connections)
    {
        Logger::getInstance().log("¿Enviar a " + uname + "? conn=" + (conn_data.conn ? "sí" : "no"));

        if (uname != username && conn_data.conn)
        {
            Logger::getInstance().log("Notificando a: " + uname + " sobre ingreso de " + username);
            conn_data.conn->send_binary(payload);
        }
    }
}

void WebSocketHandler::notify_user_status_change(const std::string &username, UserStatus st)
{
    std::lock_guard<std::mutex> lock(connections_mutex);
    std::string payload;
    payload.push_back((char)0x54);
    payload.push_back((char)username.size());
    payload += username;
    payload.push_back((char)userStatusToByte(st));
    for (auto &[_, conn_data] : connections)
    {
        if (conn_data.conn)
        {
            Logger::getInstance().log("Enviando 0x54 a: " + conn_data.username);
            conn_data.conn->send_binary(payload);
        }
    }
}

void WebSocketHandler::notify_new_message(const std::string &sender, const std::string &msg, bool is_private, const std::string &recipient)
{
    std::string payload;
    payload.push_back((char)0x55);
    payload.push_back((char)sender.size());
    payload += sender;
    payload.push_back((char)msg.size());
    payload += msg;
    if (is_private)
    {
        std::lock_guard<std::mutex> lock(connections_mutex);
        auto itA = connections.find(sender);
        auto itB = connections.find(recipient);
        if (itA != connections.end() && itA->second.conn)
        {
            Logger::getInstance().log("Enviando 0x55 de " + sender + " a " + (is_private ? recipient : "todos") + ": " + msg);
            itA->second.conn->send_binary(payload);
        }
        if (itB != connections.end() && itB->second.conn)
        {
            Logger::getInstance().log("Enviando 0x55 de " + sender + " a " + (is_private ? recipient : "todos") + ": " + msg);
            itB->second.conn->send_binary(payload);
        }
    }
    else
    {
        std::lock_guard<std::mutex> lock(connections_mutex);
        for (auto &[uname, cd] : connections)
        {
            if (cd.conn)
            {
                Logger::getInstance().log("Enviando 0x55 de " + sender + " a " + (is_private ? recipient : "todos") + ": " + msg);
                cd.conn->send_binary(payload);
            }
        }
    }
}

void WebSocketHandler::handle_list_users(crow::websocket::connection &conn)
{   
    Logger::getInstance().log("ENTRO A HANDLE LIST USERS");
    std::lock_guard<std::mutex> lock(connections_mutex);
    std::string payload;
    payload.push_back((char)0x51);
    payload.push_back((char)connections.size());
    for (const auto &[username, conn_data] : connections)
    {
        payload.push_back((char)username.size());
        payload += username;
        payload.push_back((char)userStatusToByte(conn_data.status));
    }
    Logger::getInstance().log("Enviando 0x51 a " + conn.get_remote_ip() + " (" + std::to_string(connections.size()) + " usuarios)");
    Logger::getInstance().log("Payload: " + std::to_string(payload.size()) + " bytes");
    conn.send_binary(payload);
}

void WebSocketHandler::handle_get_user_info(crow::websocket::connection &conn, const std::string &data, size_t &offset)
{
    std::string requested_name = read_string_8(data, offset);
    std::lock_guard<std::mutex> lock(connections_mutex);
    auto it = connections.find(requested_name);
    if (it == connections.end())
    {
        send_error(conn, 1);
        return;
    }
    UserStatus st = it->second.status;
    std::string payload;
    payload.push_back((char)0x52);
    payload.push_back((char)requested_name.size());
    payload += requested_name;
    payload.push_back((char)userStatusToByte(st));
    Logger::getInstance().log("Enviando 0x52 info de usuario: " + requested_name + " (estado = " + std::to_string(userStatusToByte(st)) + ")");
    conn.send_binary(payload);
}

void WebSocketHandler::handle_change_status(crow::websocket::connection &conn, const std::string &sender, const std::string &data, size_t &offset)
{
    uint8_t raw_status = read_uint8(data, offset);
    UserStatus newStatus;
    switch (raw_status)
    {
    case 1:
        newStatus = UserStatus::ACTIVO;
        break;
    case 2:
        newStatus = UserStatus::OCUPADO;
        break;
    case 3:
        newStatus = UserStatus::INACTIVO;
        break;
    default:
        send_error(conn, 2);
        return;
    }
    update_status(sender, newStatus);
}

void WebSocketHandler::handle_send_message(crow::websocket::connection &conn, const std::string &sender, const std::string &data, size_t &offset)
{
    std::string destino = read_string_8(data, offset);
    std::string mensaje = read_string_8(data, offset);
    Logger::getInstance().log("Mensaje recibido de " + sender + " para " + destino + ": " + mensaje);
    if (mensaje.empty())
    {
        send_error(conn, 3);
        return;
    }
    if (destino == "~")
    {
        send_broadcast(sender, mensaje);
    }
    else
    {
        send_private_message(sender, destino, mensaje);
    }
}

void WebSocketHandler::handle_get_history(crow::websocket::connection &conn, const std::string &sender, const std::string &data, size_t &offset)
{
    std::string target = read_string_8(data, offset);
    std::vector<std::pair<std::string, std::string>> messages;

    if (target == "~")
    {
        // Obtener historial del chat general
        std::lock_guard<std::mutex> lock(connections_mutex);
        messages = general_chat_history;
    }
    else
    {
        // Obtener historial de chat privado
        std::string chat_id = (sender < target) ? (sender + "|" + target) : (target + "|" + sender);
        auto it = chat_history.find(chat_id);
        if (it != chat_history.end())
        {
            messages = it->second;
        }
    }

    uint8_t num_msgs = (messages.size() > 255) ? 255 : static_cast<uint8_t>(messages.size());
    std::string payload;
    payload.push_back((char)0x56);
    payload.push_back((char)num_msgs);

    for (size_t i = 0; i < num_msgs; i++)
    {
        const auto &[author, text] = messages[i];
        payload.push_back((char)author.size());
        payload += author;
        payload.push_back((char)text.size());
        payload += text;
    }

    Logger::getInstance().log("Enviando 0x56 historial (" + std::to_string(num_msgs) + " mensajes)");
    conn.send_binary(payload);
}


void WebSocketHandler::on_open(crow::websocket::connection &conn, const std::string &username)
{
    std::string client_ip = conn.get_remote_ip();
    std::string user_uuid;

    bool is_reconnection = false;
    UserStatus status_to_notify = UserStatus::ACTIVO;

    {
        std::lock_guard<std::mutex> lock(connections_mutex);

        if (username.empty() || username.size() > 20)
        {
            conn.send_text("Error: Nombre de usuario inválido.");
            conn.close("Nombre inválido.");
            return;
        }

        auto it = connections.find(username);
        if (it != connections.end())
        {
            if (it->second.conn != nullptr)
            {
                conn.send_text("Error: Nombre duplicado.");
                conn.close("Duplicado.");
                return;
            }
            else
            {
                it->second.conn = &conn;
                it->second.last_active = std::chrono::steady_clock::now();
                is_reconnection = true;
                status_to_notify = it->second.status;
            }
        }
        else
        {
            user_uuid = generate_uuid();
            if (last_user_status.find(username) != last_user_status.end())
            {
                status_to_notify = last_user_status[username];
            }
            connections[username] = {username, user_uuid, &conn, status_to_notify, std::chrono::steady_clock::now()};
        }
    }

    if (is_reconnection)
    {
        Logger::getInstance().log("Reconexión de: " + username + " IP=" + client_ip);
    }
    else
    {
        Logger::getInstance().log("Nueva conexión: " + username + " (UUID: " + user_uuid + ") desde " + client_ip);
        Logger::getInstance().log("Tamaño actual de conexiones: " + std::to_string(connections.size()));
        std::lock_guard<std::mutex> lock(connections_mutex);
        for (const auto &[uname, cd] : connections)
        {
            Logger::getInstance().log(" - " + uname + " conn=" + (cd.conn ? "sí" : "no"));
        }
    }

    notify_user_joined(username, status_to_notify);
}

void WebSocketHandler::on_message(crow::websocket::connection &conn, const std::string &data, bool is_binary)
{
    Logger::getInstance().log("on_message: is_binary=" + std::string(is_binary ? "true" : "false"));
    if (!is_binary)
    {
        Logger::getInstance().log("Mensaje de texto recibido. El protocolo exige binario, se ignora.");
        return;
    }
    std::string sender = "Desconocido";
    {
        std::lock_guard<std::mutex> lock(connections_mutex);
        for (auto &[uname, conn_data] : connections)
        {
            if (conn_data.conn == &conn)
            {
                sender = uname;
                conn_data.last_active = std::chrono::steady_clock::now();
                break;
            }
        }
    }
    try
    {
        size_t offset = 0;
        uint8_t opcode = read_uint8(data, offset);
        Logger::getInstance().log("Binario op=" + std::to_string(opcode) + " from " + sender);
        switch (opcode)
        {
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
    }
    catch (std::exception &e)
    {
        Logger::getInstance().log("Error parseando binario: " + std::string(e.what()));
    }
}

void WebSocketHandler::on_close(crow::websocket::connection &conn, const std::string &reason, uint16_t code)
{
    std::lock_guard<std::mutex> lock(connections_mutex);

    std::string disconnected_user;
    for (auto &[username, conn_data] : connections)
    {
        if (conn_data.conn == &conn)
        {
            last_user_status[username] = conn_data.status;
            conn_data.conn = nullptr; 
            disconnected_user = username;
            Logger::getInstance().log("Usuario desconectado (temporal): " + username + " - " + reason + " (Código " + std::to_string(code) + ")");
            break;
        }
    }

    if (!disconnected_user.empty())
    {
        std::string payload;
        payload.push_back((char)0x57);
        payload.push_back((char)disconnected_user.size());
        payload += disconnected_user;

        for (auto &[_, cd] : connections)
        {
            if (cd.conn)
            {
                Logger::getInstance().log("Notificando 0x57 desconexión de " + disconnected_user);
                cd.conn->send_binary(payload);
            }
        }
    }
}

void WebSocketHandler::update_status(const std::string &username, UserStatus status, bool notify)
{
    bool user_found = false;

    {
        std::lock_guard<std::mutex> lock(connections_mutex);
        auto it = connections.find(username);
        if (it != connections.end())
        {
            it->second.status = status;
            it->second.last_active = std::chrono::steady_clock::now();
            Logger::getInstance().log(username + " cambió su estado");
            user_found = true;
        }
    }

    if (notify && user_found)
    {
        notify_user_status_change(username, status);
    }
}

std::string WebSocketHandler::list_users()
{
    std::ostringstream oss;
    {
        std::lock_guard<std::mutex> lock(connections_mutex);
        oss << "Usuarios conectados:\n";
        for (const auto &[username, conn_data] : connections)
        {
            uint8_t st = userStatusToByte(conn_data.status);
            oss << "- " << username << " (status=" << (int)st << ")\n";
        }
    }
    return oss.str();
}

void WebSocketHandler::send_private_message(const std::string &sender, const std::string &recipient, const std::string &msg)
{
    bool can_notify = false;

    {
        std::lock_guard<std::mutex> lock(connections_mutex);

        auto it = connections.find(recipient);
        if (it == connections.end() || !it->second.conn)
        {
            auto se = connections.find(sender);
            if (se != connections.end() && se->second.conn)
            {
                std::string errorPayload;
                errorPayload.push_back((char)0x50);
                errorPayload.push_back((char)0x04); 
                se->second.conn->send_binary(errorPayload);
            }
            return;
        }

        std::string chat_id = (sender < recipient) ? (sender + "|" + recipient) : (recipient + "|" + sender);
        chat_history[chat_id].push_back({sender, msg});

        can_notify = true;
    }

    if (can_notify)
    {
        notify_new_message(sender, msg, true, recipient);
    }
}

void WebSocketHandler::send_broadcast(const std::string &sender, const std::string &msg)
{
    {
        std::lock_guard<std::mutex> lock(connections_mutex);
        general_chat_history.push_back({sender, msg});
    }
    notify_new_message(sender, msg, false, "");
}

void WebSocketHandler::start_inactivity_monitor()
{
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

                        Logger::getInstance().log("Usuario " + username + " marcado como INACTIVO (inactivo por " + std::to_string(elapsed) + "s)");

                        notify_user_status_change(username, UserStatus::INACTIVO);
                    }
                }
            }
        }
    }).detach();
}


void WebSocketHandler::start_disconnection_cleanup()
{
    std::thread([] {
        while (true) {
            std::this_thread::sleep_for(std::chrono::minutes(1));
            std::lock_guard<std::mutex> lock(connections_mutex);
            auto now = std::chrono::steady_clock::now();

            for (auto it = connections.begin(); it != connections.end(); ) {
                if (it->second.conn == nullptr) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - it->second.last_active).count();
                    if (elapsed >= 5) {
                        Logger::getInstance().log("Eliminando usuario desconectado por más de 5 min: " + it->first);
                        it = connections.erase(it);
                        continue;
                    }
                }
                ++it;
            }
        }
    }).detach();
}
