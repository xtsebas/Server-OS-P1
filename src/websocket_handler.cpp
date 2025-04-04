#include "../include/websocket_handler.h"
#include "../include/logger.h"
#include "../include/websocket_global.h"
#include <sstream>
#include <iostream>
#include <ctime>
#include <mutex>
#include <condition_variable>

bool testing_mode = false;
std::unordered_map<std::string, ConnectionData> connections;
std::mutex connections_mutex;
std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> chat_history;
std::unordered_map<std::string, UserStatus> last_user_status;
std::vector<std::pair<std::string, std::string>> general_chat_history;
std::condition_variable inactivity_cv;
std::mutex inactivity_mutex;
bool user_marked_inactive = false;

// Constantes según el protocolo
const uint8_t MAX_MESSAGE_LENGTH = 255;
const char* GENERAL_CHAT = "~";

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
    payload.push_back((char)50);  // Código de ERROR (antes 0x50, ahora 50)
    payload.push_back((char)error_code);
    conn.send_binary(payload);
    
    Logger::getInstance().log("Enviando error " + std::to_string(error_code) + " al cliente");
}

static uint8_t userStatusToByte(UserStatus st)
{
    switch (st)
    {
    case UserStatus::DISCONNECTED:
        return 0;
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
    Logger::getInstance().log("Enviando 53 a todos excepto: " + username);

    std::string payload;
    payload.push_back((char)53);  // Usuario se acaba de registrar (antes 0x53, ahora 53)
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
    payload.push_back((char)54);  // Usuario cambió estatus (antes 0x54, ahora 54)
    payload.push_back((char)username.size());
    payload += username;
    payload.push_back((char)userStatusToByte(st));
    
    Logger::getInstance().log("Notificando cambio de estado de " + username + " a " + 
                            std::to_string(userStatusToByte(st)));
    
    for (auto &[_, conn_data] : connections)
    {
        if (conn_data.conn)
        {
            Logger::getInstance().log("Enviando 54 a: " + conn_data.username);
            conn_data.conn->send_binary(payload);
        }
    }
}

void WebSocketHandler::notify_new_message(const std::string &sender, const std::string &msg, bool is_private, const std::string &recipient)
{
    std::string payload;
    payload.push_back((char)55);  // Código de Recibió mensaje (antes 0x55, ahora 55)
    payload.push_back((char)sender.size());
    payload += sender;
    payload.push_back((char)msg.size());
    payload += msg;

    auto send_task = std::async(std::launch::async, [payload, is_private, sender, recipient]() {
        Logger::getInstance().log("Iniciando envío desde un thread separado. Thread ID: " + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
        if (is_private)
        {
            std::lock_guard<std::mutex> lock(connections_mutex);
            auto itA = connections.find(sender);
            auto itB = connections.find(recipient);
            if (itA != connections.end() && itA->second.conn)
            {
                Logger::getInstance().log("Enviando 55 de " + sender + " a " + sender);
                itA->second.conn->send_binary(payload);
            }
            if (itB != connections.end() && itB->second.conn)
            {
                Logger::getInstance().log("Enviando 55 de " + sender + " a " + recipient);
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
                    Logger::getInstance().log("Enviando 55 de " + sender + " a " + uname);
                    cd.conn->send_binary(payload);
                }
            }
        }
        Logger::getInstance().log("Envío completado desde el thread. Thread ID: " + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
    });
    send_task.wait();
}

void WebSocketHandler::handle_list_users(crow::websocket::connection &conn)
{   
    Logger::getInstance().log("ENTRO A HANDLE LIST USERS");
    std::lock_guard<std::mutex> lock(connections_mutex);
    std::string payload;
    payload.push_back((char)51);  // Código de respuesta a listar usuarios (antes 0x51, ahora 51)
    payload.push_back((char)connections.size());
    for (const auto &[username, conn_data] : connections)
    {
        payload.push_back((char)username.size());
        payload += username;
        payload.push_back((char)userStatusToByte(conn_data.status));
    }
    Logger::getInstance().log("Enviando 51 a " + conn.get_remote_ip() + " (" + std::to_string(connections.size()) + " usuarios)");
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
        send_error(conn, 1);  // Error: el usuario no existe
        return;
    }
    
    UserStatus st = it->second.status;
    
    // Crear payload según el protocolo (solo nombre y status)
    std::string payload;
    payload.push_back((char)52);  // Código de respuesta a obtener usuario (antes 0x52, ahora 52)
    payload.push_back((char)requested_name.size());
    payload += requested_name;
    payload.push_back((char)userStatusToByte(st));
    
    Logger::getInstance().log("Enviando 52 info de usuario: " + requested_name + " (estado = " + 
                            std::to_string(userStatusToByte(st)) + ")");    
    conn.send_binary(payload);
}

void WebSocketHandler::handle_change_status(crow::websocket::connection &conn, const std::string &sender, const std::string &data, size_t &offset)
{
    std::string username = read_string_8(data, offset);
    uint8_t raw_status = read_uint8(data, offset);
    
    // Verificar que el usuario solo puede cambiar su propio estado
    if (username != sender)
    {
        Logger::getInstance().log("Error: " + sender + " intentó cambiar el estado de " + username);
        send_error(conn, 2);  // Estado inválido
        return;
    }

    Logger::getInstance().log("Cambio de estado solicitado para " + username + ": " + std::to_string(raw_status));

    UserStatus oldStatus = UserStatus::DISCONNECTED;
    {
        std::lock_guard<std::mutex> lock(connections_mutex);
        auto it = connections.find(username);
        if (it != connections.end()) {
            oldStatus = it->second.status;
        }
    }

    UserStatus newStatus;
    switch (raw_status)
    {
    case 0:
        newStatus = UserStatus::DISCONNECTED;
        break;
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
        send_error(conn, 2);  // Estado inválido
        return;
    }
    Logger::getInstance().log("Transición de estado: " + std::to_string(userStatusToByte(oldStatus)) + " -> " + std::to_string(raw_status));

    update_status(sender, newStatus);
}

void WebSocketHandler::handle_send_message(crow::websocket::connection &conn, const std::string &sender, const std::string &data, size_t &offset)
{
    std::string destino = read_string_8(data, offset);
    std::string mensaje = read_string_8(data, offset);
    Logger::getInstance().log("Mensaje recibido de " + sender + " para " + destino + ": " + mensaje);
    
    // Verificar que el mensaje no esté vacío
    if (mensaje.empty())
    {
        send_error(conn, 3);  // Mensaje vacío
        return;
    }
    
    // Verificar si el mensaje excede la longitud máxima
    if (mensaje.size() > MAX_MESSAGE_LENGTH)
    {
        Logger::getInstance().log("Mensaje truncado por exceder longitud máxima: " + std::to_string(mensaje.size()) + " > " + std::to_string(MAX_MESSAGE_LENGTH));
        mensaje = mensaje.substr(0, MAX_MESSAGE_LENGTH);
    }
    
    // Enviar al chat general o a un usuario específico
    if (destino == GENERAL_CHAT)
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

    if (target == GENERAL_CHAT)
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
    payload.push_back((char)56);  // Código de respuesta a obtener historial (antes 0x56, ahora 56)
    payload.push_back((char)num_msgs);

    for (size_t i = 0; i < num_msgs; i++)
    {
        const auto &[author, text] = messages[i];
        payload.push_back((char)author.size());
        payload += author;
        payload.push_back((char)text.size());
        payload += text;
    }

    Logger::getInstance().log("Enviando 56 historial (" + std::to_string(num_msgs) + " mensajes)");
    conn.send_binary(payload);
}

void WebSocketHandler::on_open(crow::websocket::connection &conn, const std::string &username)
{
    std::string client_ip = conn.get_remote_ip();
    std::string user_uuid;

    // Validar nombre de usuario
    if (username.empty() || username.size() > 20 || username == GENERAL_CHAT)
    {
        Logger::getInstance().log("Conexión rechazada: Nombre de usuario inválido: " + username);
        conn.send_text("Error: Nombre de usuario inválido o reservado.");
        conn.close("Nombre inválido.");
        return;
    }

    bool is_reconnection = false;
    UserStatus status_to_notify = UserStatus::ACTIVO;

    {
        std::lock_guard<std::mutex> lock(connections_mutex);

        auto it = connections.find(username);
        if (it != connections.end())
        {
            if (it->second.conn != nullptr)
            {
                Logger::getInstance().log("Conexión rechazada: Nombre duplicado: " + username);
                conn.send_text("Error: Nombre duplicado.");
                conn.close("Duplicado.");
                return;
            }
            else
            {
                it->second.conn = &conn;
                it->second.last_active = std::chrono::steady_clock::now();
                it->second.ip_address = client_ip;
                is_reconnection = true;
                status_to_notify = it->second.status;
                
                // Si estaba desconectado, cambiar a activo según protocolo
                if (it->second.status == UserStatus::DISCONNECTED) {
                    it->second.status = UserStatus::ACTIVO;
                    status_to_notify = UserStatus::ACTIVO;
                }
            }
        }
        else
        {
            user_uuid = generate_uuid();
            if (last_user_status.find(username) != last_user_status.end())
            {
                status_to_notify = last_user_status[username];
                if (status_to_notify == UserStatus::DISCONNECTED) {
                    status_to_notify = UserStatus::ACTIVO;
                }
            }
            connections[username] = {
                username, 
                user_uuid, 
                &conn, 
                status_to_notify, 
                std::chrono::steady_clock::now(),
                client_ip
            };
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

    static bool monitor_started = false;
    if (!monitor_started)
    {
        WebSocketHandler::start_inactivity_monitor();
        WebSocketHandler::start_disconnection_cleanup();
        monitor_started = true;
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
    
    // Verificar longitud mínima para un mensaje válido
    if (data.size() < 1) {
        Logger::getInstance().log("Mensaje demasiado corto para ser válido");
        return;
    }
    
    std::string sender = "Desconocido";
    std::string usuario_a_reactivar;
    uint8_t opcode = 0;
    
    // Leer el opcode del mensaje antes de procesar
    if (data.size() >= 1) {
        opcode = (uint8_t)data[0];
    }

    {
        std::lock_guard<std::mutex> lock(connections_mutex);
        for (auto &[uname, conn_data] : connections)
        {
            if (conn_data.conn == &conn)
            {
                sender = uname;
                conn_data.last_active = std::chrono::steady_clock::now();
    
                // Solo considerar la reactivación si el mensaje es de tipo "enviar mensaje" (opcode 4)
                if (conn_data.status == UserStatus::INACTIVO && opcode == 4) {
                    usuario_a_reactivar = uname;
                    Logger::getInstance().log("Usuario " + uname + " será reactivado por enviar un mensaje");
                } else if (conn_data.status == UserStatus::INACTIVO) {
                    Logger::getInstance().log("Usuario " + uname + " mantiene estado INACTIVO (opcode=" + 
                                            std::to_string(opcode) + ", no es mensaje)");
                }
    
                Logger::getInstance().log("Actualizando tiempo de actividad para " + sender);
                break;
            }
        }
    }
    
    // Fuera del lock, reactivar si es un mensaje de chat (opcode 4)
    if (!usuario_a_reactivar.empty()) {
        update_status(usuario_a_reactivar, UserStatus::ACTIVO);
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
    std::string disconnected_user;
    
    {
        std::lock_guard<std::mutex> lock(connections_mutex);

        for (auto &[username, conn_data] : connections)
        {
            if (conn_data.conn == &conn)
            {
                // Guardar estado anterior y luego cambiar a DISCONNECTED
                last_user_status[username] = conn_data.status;
                conn_data.status = UserStatus::DISCONNECTED;
                conn_data.conn = nullptr; 
                disconnected_user = username;
                Logger::getInstance().log("Usuario desconectado: " + username + " - " + reason + " (Código " + std::to_string(code) + ")");
                break;
            }
        }
    }

    if (!disconnected_user.empty())
    {
        // Notificar a todos los usuarios utilizando el código 54 (cambio de estado)
        notify_user_status_change(disconnected_user, UserStatus::DISCONNECTED);
    }
}

void WebSocketHandler::update_status(const std::string &username, UserStatus status, bool notify)
{
    bool user_found = false;
    bool should_notify = false;
    UserStatus oldStatus = UserStatus::DISCONNECTED;

    {
        std::lock_guard<std::mutex> lock(connections_mutex);
        auto it = connections.find(username);
        if (it != connections.end())
        {
            oldStatus = it->second.status;
            it->second.status = status;
            it->second.last_active = std::chrono::steady_clock::now();
            user_found = true;
            should_notify = notify || it->second.conn != nullptr;  // Always notify if connected
            Logger::getInstance().log(username + " cambió su estado de " + 
                std::to_string(userStatusToByte(oldStatus)) + " a " + 
                std::to_string(userStatusToByte(status)));
        }
    }

    if (should_notify)
    {
        try {
            notify_user_status_change(username, status);
        } catch (const std::exception& e) {
            Logger::getInstance().log("Error al notificar cambio de estado: " + std::string(e.what()));
        }
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
        if (it == connections.end() || !it->second.conn || it->second.status == UserStatus::DISCONNECTED)
        {
            auto se = connections.find(sender);
            if (se != connections.end() && se->second.conn)
            {
                send_error(*se->second.conn, 4);  // Destinatario desconectado
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
            
            std::vector<std::string> users_to_notify;

            {
                std::lock_guard<std::mutex> lock(connections_mutex);
                auto now = std::chrono::steady_clock::now();

                for (auto& [username, conn_data] : connections) {
                    if (conn_data.conn &&
                        conn_data.status != UserStatus::INACTIVO &&
                        conn_data.status != UserStatus::DISCONNECTED) {
                        
                        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - conn_data.last_active).count();

                        if (elapsed >= 60) {
                            conn_data.status = UserStatus::INACTIVO;
                            Logger::getInstance().log("Usuario " + username + " marcado como INACTIVO (inactivo por " + std::to_string(elapsed) + "s)");
                            
                            {
                                std::lock_guard<std::mutex> lock(inactivity_mutex);
                                user_marked_inactive = true;
                            }

                            users_to_notify.push_back(username);
                        }
                    }
                }
            }

            for (const auto& username : users_to_notify) {
                notify_user_status_change(username, UserStatus::INACTIVO);
            }

            if (!users_to_notify.empty()) {
                inactivity_cv.notify_all();
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                {
                    std::lock_guard<std::mutex> lock(inactivity_mutex);
                    user_marked_inactive = false;
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
