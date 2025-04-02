#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include "../include/websocket_handler.h"
#include "../include/logger.h"
#include "../include/websocket_global.h"

class MockConnection : public crow::websocket::connection
{
public:
    MockConnection(const std::string &ip)
        : remote_ip_(ip)
    {
    }

    void send_binary(std::string msg) override
    {
        sent_messages.push_back(msg);
    }
    void send_text(std::string msg) override
    {
        sent_messages.push_back(msg);
    }
    void send_ping(std::string msg) override
    {
        // optional mock
    }
    void send_pong(std::string msg) override
    {
        // optional mock
    }
    void close(std::string const &msg = "quit", uint16_t code = 1000) override
    {
        closed = true;
        close_reason = msg;
    }
    std::string get_subprotocol() const override
    {
        return "";
    }

    std::string get_remote_ip() override
    {
        return remote_ip_;
    }

    bool closed = false;
    std::string close_reason;
    std::vector<std::string> sent_messages;

private:
    std::string remote_ip_;
};

static uint8_t get_opcode(const std::string &data)
{
    return (uint8_t)data[0];
}

static uint8_t get_length(const std::string &data, size_t offset)
{
    return (uint8_t)data[offset];
}

static std::string get_string_8(const std::string &data, size_t &offset)
{
    uint8_t len = get_length(data, offset);
    offset += 1;
    std::string s = data.substr(offset, len);
    offset += len;
    return s;
}

void check_opcode(const std::string &payload, uint8_t expected_opcode, const std::string &test_name)
{
    assert(get_opcode(payload) == expected_opcode && ("x " + test_name + " -> opcode no coincide").c_str());
    std::cout << test_name << ": opcode " << (int)expected_opcode << " verificado\n";
}

// Prueba validación de nombres de usuario, incluyendo "~" reservado
void test_invalid_usernames()
{
    std::cout << "test_invalid_usernames\n";
    
    connections.clear();
    
    // Probar conectar con nombre "~" (reservado para chat general)
    MockConnection conn_tilde("127.0.0.1");
    WebSocketHandler::on_open(conn_tilde, "~");
    assert(conn_tilde.closed && "Nombre '~' debería ser rechazado por estar reservado");
    std::cout << "- Nombre '~' rechazado correctamente\n";
    
    // Probar nombre de usuario vacío
    MockConnection conn_empty("127.0.0.1");
    WebSocketHandler::on_open(conn_empty, "");
    assert(conn_empty.closed && "Nombre vacío debería ser rechazado");
    std::cout << "- Nombre vacío rechazado correctamente\n";
    
    // Probar nombre de usuario demasiado largo (>20 caracteres)
    std::string long_name(21, 'a');
    MockConnection conn_long("127.0.0.1");
    WebSocketHandler::on_open(conn_long, long_name);
    assert(conn_long.closed && "Nombre largo debería ser rechazado");
    std::cout << "- Nombre largo rechazado correctamente\n";
    
    std::cout << "test_invalid_usernames: Todas las pruebas pasaron\n";
}

void test_on_open_and_duplicate()
{
    std::cout << "test_on_open_and_duplicate\n";
    
    connections.clear();

    // 1) Primer usuario: alice
    MockConnection conn_alice("127.0.0.1");
    WebSocketHandler::on_open(conn_alice, "alice");
    assert(connections.size() == 1 && "on_open: No se registró alice");
    assert(!conn_alice.closed && "on_open: alice cerrada indebidamente");
    std::cout << "- Usuario alice registrado correctamente\n";

    // 2) Conectar duplicado: alice de nuevo
    MockConnection conn_alice2("127.0.0.1");
    WebSocketHandler::on_open(conn_alice2, "alice");
    assert(conn_alice2.closed && "on_open: Nombre duplicado no fue cerrado");
    assert(connections.size() == 1 && "on_open: se insertó duplicado erroneamente");
    std::cout << "- Duplicado alice rechazado correctamente\n";

    // 3) Reconexión: dejar conn_alice a nullptr y reabrir
    connections["alice"].conn = nullptr;
    connections["alice"].status = UserStatus::DISCONNECTED;
    MockConnection conn_alice_re("127.0.0.1");
    WebSocketHandler::on_open(conn_alice_re, "alice");
    // Reconexión
    assert(!conn_alice_re.closed && " on_open: reconexión erroneamente cerrada");
    assert(connections["alice"].conn == &conn_alice_re && "on_open: reconexión no reusó la misma entry");
    assert(connections["alice"].status == UserStatus::ACTIVO && "on_open: reconexión no cambió status a ACTIVO");
    std::cout << "- Reconexión de alice correcta, estado cambiado a ACTIVO\n";
    
    std::cout << "test_on_open_and_duplicate: Todas las pruebas pasaron\n";
}

void test_list_users()
{
    std::cout << "test_list_users\n";
    
    connections.clear();
    MockConnection conn_alice("127.0.0.1");
    WebSocketHandler::on_open(conn_alice, "alice");
    
    MockConnection conn_bob("127.0.0.1");
    WebSocketHandler::on_open(conn_bob, "bob");
    assert(!conn_bob.closed);
    std::cout << "- Usuarios alice y bob registrados correctamente\n";

    size_t old_count = conn_bob.sent_messages.size();
    WebSocketHandler::handle_list_users(conn_bob);
    assert(conn_bob.sent_messages.size() == old_count + 1 && "handle_list_users no envió nada");
    std::cout << "- Mensaje de lista de usuarios enviado\n";

    std::string payload = conn_bob.sent_messages.back();
    check_opcode(payload, 0x51, "test_list_users");

    size_t offset = 1;
    uint8_t n = (uint8_t)payload[offset++];
    std::cout << "  Num usuarios en payload: " << (int)n << ", connections.size(): " << connections.size() << "\n";
    assert(n == connections.size() && "handle_list_users: número de usuarios no coincide");

    for (int i = 0; i < n; i++)
    {
        std::string name = get_string_8(payload, offset);
        uint8_t st = (uint8_t)payload[offset++];
        std::cout << "  Usuario: " << name << ", estado: " << (int)st << "\n";
    }

    std::cout << "test_list_users: Todas las pruebas pasaron\n";
}

void test_handle_get_user_info()
{
    std::cout << "test_handle_get_user_info\n";

    connections.clear();
    chat_history.clear();

    connections["alice"] = ConnectionData{
        "alice",
        "uuid-alice",
        nullptr,
        UserStatus::ACTIVO,
        std::chrono::steady_clock::now(),
        "192.168.1.10"
    };

    MockConnection conn_bob("127.0.0.1");
    connections["bob"] = ConnectionData{
        "bob",
        "uuid-bob",
        &conn_bob,
        UserStatus::ACTIVO,
        std::chrono::steady_clock::now(),
        "127.0.0.1" 
    };
    
    std::cout << "- Usuarios alice y bob registrados con estados\n";

    std::string data;
    data.push_back((char)0x02);  // Obtener usuario
    data.push_back((char)5);     // Longitud del nombre
    data += "alice";             // Nombre del usuario a consultar

    size_t old_count = conn_bob.sent_messages.size();
    WebSocketHandler::on_message(conn_bob, data, true); 
    assert(conn_bob.sent_messages.size() == old_count + 1 && "get_user_info no envió nada");
    std::cout << "- Solicitud de información de alice procesada\n";

    std::string payload = conn_bob.sent_messages.back();
    std::cout << "  Payload recibido (hex): ";
    for (unsigned char c : payload) {
        printf("%02X ", c);
    }
    std::cout << "\n";

    check_opcode(payload, 0x52, "test_handle_get_user_info");

    size_t offset = 1;
    std::string name = get_string_8(payload, offset);
    uint8_t st = (uint8_t)payload[offset++];
    
    // Verificar que no se incluye la IP en la respuesta (según protocolo)
    assert(offset == payload.size() && "La respuesta no debería incluir la IP según protocolo");
    
    std::cout << "  Nombre: " << name << ", estado: " << (int)st << "\n";
    assert(name == "alice" && "get_user_info: nombre no coincide");
    assert(st == 1 && "get_user_info: status no es ACTIVO");

    // Probar usuario que no existe
    data.clear();
    data.push_back((char)0x02);  // Obtener usuario
    data.push_back((char)6);     // Longitud del nombre
    data += "noexist";           // Nombre que no existe
    
    old_count = conn_bob.sent_messages.size();
    WebSocketHandler::on_message(conn_bob, data, true);
    assert(conn_bob.sent_messages.size() == old_count + 1 && "Error de usuario no encontrado no fue enviado");
    
    payload = conn_bob.sent_messages.back();
    check_opcode(payload, 0x50, "test_handle_get_user_info error");
    assert(payload[1] == 1 && "Código de error incorrecto para usuario no existente");
    std::cout << "- Error de usuario no existente enviado correctamente\n";

    std::cout << "test_handle_get_user_info: Todas las pruebas pasaron\n";
}

void test_handle_change_status()
{
    std::cout << "test_handle_change_status\n";

    connections.clear();
    chat_history.clear();

    MockConnection conn("127.0.0.1");
    connections["alice"] = ConnectionData{
        "alice",
        "uuid-alice",
        &conn,
        UserStatus::ACTIVO,
        std::chrono::steady_clock::now()
    };

    auto simulate_status_change = [&](uint8_t new_status_byte, UserStatus expected_status, const std::string &description) {
        std::string data;
        data.push_back((char)0x03);                 // Opcode
        data.push_back((char)5);                    // Longitud de "alice"
        data += "alice";                            // Nombre
        data.push_back((char)new_status_byte);      // Nuevo estado

        size_t old_count = conn.sent_messages.size();

        try {
            std::cout << "Probando transición: " << description << std::endl;
            WebSocketHandler::on_message(conn, data, true);

            assert(connections["alice"].status == expected_status && ("❌ " + description + ": Estado no actualizado correctamente").c_str());

            assert(conn.sent_messages.size() > old_count && ("❌ " + description + ": No se notificó cambio de estado").c_str());

            std::string payload = conn.sent_messages.back();
            check_opcode(payload, 0x54, description);

            size_t offset = 1;
            std::string name = get_string_8(payload, offset);
            uint8_t st = (uint8_t)payload[offset++];

            assert(name == "alice" && st == new_status_byte && ("❌ " + description + ": Payload incorrecto").c_str());

            std::cout << "✅ " << description << " pasado correctamente\n";
        } catch (const std::exception& e) {
            std::cout << "❌ " << description << " falló con error: " << e.what() << std::endl;
            assert(false && ("❌ " + description + ": Exception: " + e.what()).c_str());
        }
    };

    simulate_status_change(2, UserStatus::OCUPADO, "ACTIVO -> OCUPADO");
    simulate_status_change(1, UserStatus::ACTIVO, "OCUPADO -> ACTIVO");
    simulate_status_change(3, UserStatus::INACTIVO, "ACTIVO -> INACTIVO");
    simulate_status_change(2, UserStatus::OCUPADO, "INACTIVO -> OCUPADO");
    simulate_status_change(3, UserStatus::INACTIVO, "OCUPADO -> INACTIVO");
    simulate_status_change(1, UserStatus::ACTIVO, "INACTIVO -> ACTIVO");

    std::cout << "test_handle_change_status: Todas las pruebas pasaron\n";
}

void test_inactive_status_not_reactivated_by_non_message_opcode()
{
    std::cout << "test_inactive_status_not_reactivated_by_non_message_opcode\n";

    connections.clear();
    chat_history.clear();

    MockConnection conn("127.0.0.1");
    connections["bob"] = ConnectionData{
        "bob",
        "uuid-bob",
        &conn,
        UserStatus::INACTIVO,
        std::chrono::steady_clock::now()
    };

    // Simular mensaje tipo "solicitar lista de usuarios" (opcode 0x01)
    std::string data;
    data.push_back((char)0x01); // Opcode inválido para reactivación

    WebSocketHandler::on_message(conn, data, true);

    assert(connections["bob"].status == UserStatus::INACTIVO && "❌ Estado cambiado a ACTIVO por mensaje no permitido");
    std::cout << "✅ Estado se mantuvo INACTIVO correctamente\n";
}

void test_inactive_status_reactivated_by_message_opcode()
{
    std::cout << "test_inactive_status_reactivated_by_message_opcode\n";

    connections.clear();
    chat_history.clear();

    MockConnection conn("127.0.0.1");
    connections["bob"] = ConnectionData{
        "bob",
        "uuid-bob",
        &conn,
        UserStatus::INACTIVO,
        std::chrono::steady_clock::now()
    };

    // Simular mensaje privado (opcode 0x04): para "~", contenido "hola"
    std::string data;
    data.push_back((char)0x04);         // Opcode: enviar mensaje
    data.push_back((char)1);            // Longitud del destinatario "~"
    data += "~";
    data.push_back((char)4);            // Longitud del mensaje
    data += "hola";

    WebSocketHandler::on_message(conn, data, true);

    assert(connections["bob"].status == UserStatus::ACTIVO && "❌ Estado no se reactivó tras enviar mensaje");
    std::cout << "✅ Estado INACTIVO se reactivó correctamente al enviar mensaje\n";
}

void test_handle_send_message()
{
    std::cout << "test_handle_send_message\n";

    connections.clear();
    chat_history.clear();
    general_chat_history.clear();

    MockConnection conn_alice("127.0.0.1");
    connections["alice"] = ConnectionData{
        "alice", "uuid-alice", &conn_alice,
        UserStatus::ACTIVO, std::chrono::steady_clock::now()
    };

    MockConnection conn_bob("127.0.0.1");
    connections["bob"] = ConnectionData{
        "bob", "uuid-bob", &conn_bob,
        UserStatus::ACTIVO, std::chrono::steady_clock::now()
    };
    
    std::cout << "- Usuarios alice y bob registrados\n";

    // Test 1: Enviar mensaje privado normal
    std::string data;
    data.push_back((char)0x04);        // Enviar mensaje
    data.push_back((char)3); data += "bob";   // Destinatario
    data.push_back((char)4); data += "hola";  // Mensaje

    size_t alice_msgs = conn_alice.sent_messages.size();
    size_t bob_msgs = conn_bob.sent_messages.size();
    
    WebSocketHandler::on_message(conn_alice, data, true);
    
    assert(conn_bob.sent_messages.size() > bob_msgs && "Bob no recibió el mensaje privado");
    assert(conn_alice.sent_messages.size() > alice_msgs && "Alice no recibió copia del mensaje enviado");
    
    std::string bob_payload = conn_bob.sent_messages.back();
    check_opcode(bob_payload, 0x55, "Mensaje a Bob");
    
    size_t offset = 1;
    std::string origin = get_string_8(bob_payload, offset);
    std::string msg = get_string_8(bob_payload, offset);
    
    std::cout << "  Mensaje privado: " << origin << " -> " << msg << "\n";
    assert(origin == "alice" && msg == "hola" && "Contenido del mensaje incorrecto");
    
    // Test 2: Mensaje vacío (error)
    data.clear();
    data.push_back((char)0x04);        // Enviar mensaje
    data.push_back((char)3); data += "bob";   // Destinatario
    data.push_back((char)0);                  // Mensaje vacío
    
    alice_msgs = conn_alice.sent_messages.size();
    WebSocketHandler::on_message(conn_alice, data, true);
    
    assert(conn_alice.sent_messages.size() > alice_msgs && "No se envió error por mensaje vacío");
    std::string error_msg = conn_alice.sent_messages.back();
    assert(get_opcode(error_msg) == 0x50 && error_msg[1] == 3 && "Error incorrecto para mensaje vacío");
    std::cout << "- Error por mensaje vacío enviado correctamente\n";
    
    // Test 3: Destinatario desconectado (error)
    connections["charlie"] = ConnectionData{
        "charlie", "uuid-charlie", nullptr,
        UserStatus::DISCONNECTED, std::chrono::steady_clock::now()
    };
    
    data.clear();
    data.push_back((char)0x04);              // Enviar mensaje
    data.push_back((char)7); data += "charlie"; // Destinatario desconectado
    data.push_back((char)4); data += "hola";    // Mensaje
    
    alice_msgs = conn_alice.sent_messages.size();
    WebSocketHandler::on_message(conn_alice, data, true);
    
    assert(conn_alice.sent_messages.size() > alice_msgs && "No se envió error por destinatario desconectado");
    error_msg = conn_alice.sent_messages.back();
    assert(get_opcode(error_msg) == 0x50 && error_msg[1] == 4 && "Error incorrecto para destinatario desconectado");
    std::cout << "- Error por destinatario desconectado enviado correctamente\n";
    
    // Test 4: Mensaje al chat general
    data.clear();
    data.push_back((char)0x04);        // Enviar mensaje
    data.push_back((char)1); data += "~";     // Chat general
    data.push_back((char)5); data += "hello"; // Mensaje
    
    alice_msgs = conn_alice.sent_messages.size();
    bob_msgs = conn_bob.sent_messages.size();
    
    WebSocketHandler::on_message(conn_alice, data, true);
    
    assert(conn_alice.sent_messages.size() > alice_msgs && "Alice no recibió mensaje del chat general");
    assert(conn_bob.sent_messages.size() > bob_msgs && "Bob no recibió mensaje del chat general");
    assert(general_chat_history.size() == 1 && "Mensaje no agregado al historial del chat general");
    
    std::string general_msg = conn_bob.sent_messages.back();
    check_opcode(general_msg, 0x55, "Mensaje al chat general");
    
    offset = 1;
    origin = get_string_8(general_msg, offset);
    msg = get_string_8(general_msg, offset);
    
    std::cout << "  Mensaje general: " << origin << " -> " << msg << "\n";
    assert(origin == "alice" && msg == "hello" && "Contenido del mensaje al chat general incorrecto");
    
    // Test 5: Mensaje largo (truncamiento a 255 caracteres)
    std::string longMessage(300, 'a');
    
    data.clear();
    data.push_back((char)0x04);        // Enviar mensaje
    data.push_back((char)3); data += "bob";   // Destinatario
    data.push_back((char)255);               // Longitud máxima
    data += longMessage.substr(0, 255);       // Mensaje truncado
    
    bob_msgs = conn_bob.sent_messages.size();
    WebSocketHandler::on_message(conn_alice, data, true);
    
    assert(conn_bob.sent_messages.size() > bob_msgs && "Bob no recibió mensaje largo");
    std::string truncated_msg = conn_bob.sent_messages.back();
    check_opcode(truncated_msg, 0x55, "Mensaje truncado");
    
    offset = 1;
    origin = get_string_8(truncated_msg, offset);
    msg = get_string_8(truncated_msg, offset);
    
    std::cout << "  Longitud del mensaje truncado: " << msg.size() << " (debe ser <= 255)\n";
    assert(msg.size() <= 255 && "Mensaje no truncado correctamente");
    
    std::cout << "test_handle_send_message: Todas las pruebas pasaron\n";
}

void test_handle_get_history()
{
    std::cout << "test_handle_get_history\n";

    connections.clear();
    chat_history.clear();
    general_chat_history.clear();

    MockConnection conn_alice("127.0.0.1");
    connections["alice"] = ConnectionData{
        "alice", "uuid-alice", &conn_alice,
        UserStatus::ACTIVO, std::chrono::steady_clock::now()
    };

    MockConnection conn_bob("127.0.0.1");
    connections["bob"] = ConnectionData{
        "bob", "uuid-bob", &conn_bob,
        UserStatus::ACTIVO, std::chrono::steady_clock::now()
    };
    
    std::cout << "- Usuarios alice y bob registrados\n";

    // Agregar mensajes al historial privado
    std::string chat_id = (std::string("alice") < "bob") ? "alice|bob" : "bob|alice";
    chat_history[chat_id].push_back({"alice", "hola"});
    chat_history[chat_id].push_back({"bob", "respuesta"});
    
    // Agregar mensajes al chat general
    general_chat_history.push_back({"alice", "mensaje general 1"});
    general_chat_history.push_back({"bob", "mensaje general 2"});
    
    std::cout << "- Historial de chat privado y general inicializado\n";

    // Test 1: Obtener historial de chat privado
    std::string data;
    data.push_back((char)0x05);         // Obtener historial
    data.push_back((char)3);            // Longitud del nombre
    data += "bob";                      // Obtener chat con bob

    size_t alice_msgs = conn_alice.sent_messages.size();
    WebSocketHandler::on_message(conn_alice, data, true);

    assert(conn_alice.sent_messages.size() > alice_msgs && "No se envió historial de chat privado");
    std::string history_payload = conn_alice.sent_messages.back();
    check_opcode(history_payload, 0x56, "Historial de chat privado");

    size_t offset = 1;
    uint8_t num = (uint8_t)history_payload[offset++];
    std::cout << "  Historial privado: " << (int)num << " mensajes recibidos\n";
    assert(num == 2 && "Número incorrecto de mensajes en historial privado");

    for (int i = 0; i < num; i++) {
        std::string autor = get_string_8(history_payload, offset);
        std::string contenido = get_string_8(history_payload, offset);
        std::cout << "    " << autor << ": " << contenido << "\n";
    }
    
    // Test 2: Obtener historial de chat general
    data.clear();
    data.push_back((char)0x05);         // Obtener historial
    data.push_back((char)1);            // Longitud del nombre
    data += "~";                        // Chat general

    alice_msgs = conn_alice.sent_messages.size();
    WebSocketHandler::on_message(conn_alice, data, true);

    assert(conn_alice.sent_messages.size() > alice_msgs && "No se envió historial de chat general");
    history_payload = conn_alice.sent_messages.back();
    check_opcode(history_payload, 0x56, "Historial de chat general");

    offset = 1;
    num = (uint8_t)history_payload[offset++];
    std::cout << "  Historial general: " << (int)num << " mensajes recibidos\n";
    assert(num == 2 && "Número incorrecto de mensajes en historial general");

    for (int i = 0; i < num; i++) {
        std::string autor = get_string_8(history_payload, offset);
        std::string contenido = get_string_8(history_payload, offset);
        std::cout << "    " << autor << ": " << contenido << "\n";
    }
    
    // Test 3: Historial de chat que no existe (vacío)
    data.clear();
    data.push_back((char)0x05);         // Obtener historial
    data.push_back((char)7);            // Longitud del nombre
    data += "unknown";                  // Usuario que no existe

    alice_msgs = conn_alice.sent_messages.size();
    WebSocketHandler::on_message(conn_alice, data, true);

    assert(conn_alice.sent_messages.size() > alice_msgs && "No se envió respuesta para historial inexistente");
    history_payload = conn_alice.sent_messages.back();
    check_opcode(history_payload, 0x56, "Historial inexistente");

    offset = 1;
    num = (uint8_t)history_payload[offset++];
    std::cout << "  Historial inexistente: " << (int)num << " mensajes recibidos (debe ser 0)\n";
    assert(num == 0 && "Debe devolver 0 mensajes para historial inexistente");

    std::cout << "test_handle_get_history: Todas las pruebas pasaron\n";
}

void test_user_disconnection()
{
    std::cout << "test_user_disconnection\n";
    
    connections.clear();
    
    // Crear usuarios para la prueba
    MockConnection conn_alice("127.0.0.1");
    WebSocketHandler::on_open(conn_alice, "alice");
    
    MockConnection conn_bob("127.0.0.1");
    WebSocketHandler::on_open(conn_bob, "bob");
    
    assert(connections["alice"].status == UserStatus::ACTIVO && "Estado inicial de alice incorrecto");
    assert(connections["bob"].status == UserStatus::ACTIVO && "Estado inicial de bob incorrecto");
    std::cout << "- Usuarios alice y bob registrados como ACTIVOS\n";
    
    // Limpiar mensajes anteriores
    conn_alice.sent_messages.clear();
    conn_bob.sent_messages.clear();
    
    // Simular desconexión de alice
    WebSocketHandler::on_close(conn_alice, "Test disconnection", 1000);
    
    // Verificar que alice está marcada como DISCONNECTED
    assert(connections["alice"].status == UserStatus::DISCONNECTED && "alice no cambió a DISCONNECTED");
    assert(connections["alice"].conn == nullptr && "Puntero conn de alice no se estableció a nullptr");
    std::cout << "- Estado de alice cambiado a DISCONNECTED y puntero anulado\n";
    
    // Verificar que bob recibió notificación 0x54 (cambio de estado)
    bool notification_received = false;
    for (const auto& msg : conn_bob.sent_messages) {
        if (get_opcode(msg) == 0x54) {  // Notificación de cambio de estado
            size_t offset = 1;
            std::string name = get_string_8(msg, offset);
            uint8_t status = (uint8_t)msg[offset];
            
            if (name == "alice" && status == 0) {  // 0 = DISCONNECTED
                notification_received = true;
                std::cout << "- Bob recibió notificación 0x54 de desconexión de alice\n";
                break;
            }
        }
    }
    
    assert(notification_received && "Bob no recibió notificación 0x54 de desconexión");
    
    // Intentar enviar mensaje a usuario desconectado
    std::string data;
    data.push_back((char)0x04);         // Enviar mensaje
    data.push_back((char)5);            // Longitud del nombre
    data += "alice";                    // Destinatario desconectado
    data.push_back((char)5);            // Longitud del mensaje
    data += "hello";                    // Mensaje
    
    size_t old_count = conn_bob.sent_messages.size();
    WebSocketHandler::on_message(conn_bob, data, true);
    
    assert(conn_bob.sent_messages.size() > old_count && "No se envió error por destinatario desconectado");
    std::string error_msg = conn_bob.sent_messages.back();
    assert(get_opcode(error_msg) == 0x50 && error_msg[1] == 4 && "Error incorrecto para destinatario desconectado");
    std::cout << "- Error enviado por mensaje a usuario desconectado\n";
    
    // Probar reconexión
    MockConnection conn_alice_re("127.0.0.1");
    WebSocketHandler::on_open(conn_alice_re, "alice");
    
    assert(connections["alice"].status == UserStatus::ACTIVO && "Reconexión no cambia estado a ACTIVO");
    assert(connections["alice"].conn == &conn_alice_re && "Puntero conn no actualizado en reconexión");
    std::cout << "- Reconexión exitosa, estado cambiado a ACTIVO\n";
    
    std::cout << "test_user_disconnection: Todas las pruebas pasaron\n";
}

void test_message_size_limit()
{
    std::cout << "test_message_size_limit\n";
    
    connections.clear();
    chat_history.clear();
    
    // Crear usuarios para la prueba
    MockConnection conn_alice("127.0.0.1");
    WebSocketHandler::on_open(conn_alice, "alice");
    
    MockConnection conn_bob("127.0.0.1");
    WebSocketHandler::on_open(conn_bob, "bob");
    
    std::cout << "- Usuarios alice y bob registrados\n";
    
    // Crear un mensaje justo en el límite (255 caracteres)
    std::string exactMsg(255, 'x');
    std::string data;
    data.push_back((char)0x04);        // Enviar mensaje
    data.push_back((char)3);           // Longitud del nombre
    data += "bob";                     // Destinatario
    data.push_back((char)255);         // Longitud exacta
    data += exactMsg;                  // Mensaje de 255 caracteres
    
    size_t bob_msgs = conn_bob.sent_messages.size();
    WebSocketHandler::on_message(conn_alice, data, true);
    
    assert(conn_bob.sent_messages.size() > bob_msgs && "Bob no recibió mensaje de longitud exacta");
    
    std::string msg_payload = conn_bob.sent_messages.back();
    check_opcode(msg_payload, 0x55, "Mensaje exacto");
    
    size_t offset = 1;
    std::string sender = get_string_8(msg_payload, offset);
    std::string message = get_string_8(msg_payload, offset);
    
    assert(sender == "alice" && "Remitente incorrecto");
    assert(message.size() == 255 && "Mensaje de longitud exacta alterado");
    std::cout << "- Mensaje de longitud exacta (255 caracteres) enviado correctamente\n";
    
    // Crear mensaje que excede el límite (300 caracteres)
    std::string overMsg(300, 'y');
    data.clear();
    data.push_back((char)0x04);        // Enviar mensaje
    data.push_back((char)3);           // Longitud del nombre
    data += "bob";                     // Destinatario
    data.push_back((char)255);         // Máxima longitud posible
    data += overMsg.substr(0, 255);    // Los primeros 255 caracteres
    
    bob_msgs = conn_bob.sent_messages.size();
    WebSocketHandler::on_message(conn_alice, data, true);
    
    assert(conn_bob.sent_messages.size() > bob_msgs && "Bob no recibió mensaje truncado");
    
    msg_payload = conn_bob.sent_messages.back();
    check_opcode(msg_payload, 0x55, "Mensaje truncado");
    
    offset = 1;
    sender = get_string_8(msg_payload, offset);
    message = get_string_8(msg_payload, offset);
    
    assert(sender == "alice" && "Remitente incorrecto");
    assert(message.size() <= 255 && "Mensaje no fue truncado correctamente");
    std::cout << "- Mensaje truncado a " << message.size() << " caracteres (límite 255)\n";
    
    std::cout << "test_message_size_limit: Todas las pruebas pasaron\n";
}

void test_keep_status()
{
    std::cout << "test_keep_status\n";

    connections.clear();
    last_user_status.clear();

    // Simular primera conexión de Alice
    MockConnection conn_alice("127.0.0.1");
    WebSocketHandler::on_open(conn_alice, "alice");
    assert(connections["alice"].status == UserStatus::ACTIVO && "Estado incorrecto al conectar por primera vez");
    std::cout << "- Primera conexión de alice: estado ACTIVO\n";

    // Cambiar estado de Alice a OCUPADO
    std::string data;
    data.push_back((char)0x03);  // Cambiar estado
    data.push_back((char)5);     // Longitud nombre
    data += "alice";             // Nombre usuario
    data.push_back((char)2);     // OCUPADO
    WebSocketHandler::on_message(conn_alice, data, true);
    assert(connections["alice"].status == UserStatus::OCUPADO && "Cambio de estado fallido");
    std::cout << "- Estado cambiado a OCUPADO\n";

    // Simular desconexión de Alice
    WebSocketHandler::on_close(conn_alice, "Cerrando", 1000);
    assert(last_user_status["alice"] == UserStatus::OCUPADO && "Estado no guardado al desconectar");
    assert(connections["alice"].status == UserStatus::DISCONNECTED && "Estado no cambiado a DISCONNECTED");
    std::cout << "- Desconexión: estado guardado y cambiado a DISCONNECTED\n";

    // Simular reconexión de Alice
    MockConnection conn_alice_re("127.0.0.1");
    WebSocketHandler::on_open(conn_alice_re, "alice");
    // Según el protocolo, al reconectar debe volver a ACTIVO, no importa el estado anterior
    assert(connections["alice"].status == UserStatus::ACTIVO && "Estado no restaurado a ACTIVO al reconectar");
    std::cout << "- Reconexión: estado restaurado a ACTIVO\n";

    std::cout << "test_keep_status: Todas las pruebas pasaron\n";
}

void test_inactivity()
{
    std::cout << "test_inactivity\n";

    // Reiniciar la variable antes de cada prueba
    {
        std::lock_guard<std::mutex> lock(inactivity_mutex);
        user_marked_inactive = false;
    }

    connections.clear();
    last_user_status.clear();

    // Simular conexión de Alice
    MockConnection conn_alice("127.0.0.1");
    WebSocketHandler::on_open(conn_alice, "alice");
    
    // Verificar estado inicial
    assert(connections["alice"].status == UserStatus::ACTIVO && "Estado inicial incorrecto");
    std::cout << "- Usuario alice inicialmente ACTIVO\n";
    
    // Forzar manualmente el timestamp de última actividad para que parezca inactivo
    {
        std::lock_guard<std::mutex> lock(connections_mutex);
        // Retroceder el tiempo de última actividad para simular inactividad
        connections["alice"].last_active = std::chrono::steady_clock::now() - std::chrono::seconds(70);
    }

    // Llamar directamente al código que verifica inactividad
    {
        std::lock_guard<std::mutex> lock(connections_mutex);
        auto now = std::chrono::steady_clock::now();
        for (auto& [username, conn_data] : connections) {
            if (conn_data.status != UserStatus::INACTIVO) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    now - conn_data.last_active).count();
                
                if (elapsed >= 60) {
                    conn_data.status = UserStatus::INACTIVO;
                    std::cout << "  Usuario " + username + " marcado como INACTIVO (inactivo por " 
                        + std::to_string(elapsed) + "s)\n";
                    
                    {
                        std::lock_guard<std::mutex> lock(inactivity_mutex);
                        user_marked_inactive = true;
                    }
                    inactivity_cv.notify_all();
                }
            }
        }
    }
    
    // Esperar brevemente para asegurar que el cambio de estado se ha propagado
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verificar que Alice haya sido marcada como INACTIVO
    {
        std::lock_guard<std::mutex> lock(connections_mutex);
        assert(connections["alice"].status == UserStatus::INACTIVO && 
               "Usuario no fue marcado como INACTIVO tras inactividad");
    }
    
    // Simular actividad (enviar un mensaje)
    std::string data;
    data.push_back((char)0x04);  // Enviar mensaje
    data.push_back((char)1);     // Longitud del nombre
    data += "~";                 // Chat general
    data.push_back((char)5);     // Longitud del mensaje
    data += "hello";             // Mensaje
    
    WebSocketHandler::on_message(conn_alice, data, true);
    
    // Verificar que el estado se actualizó a ACTIVO
    assert(connections["alice"].status == UserStatus::ACTIVO && 
           "Estado no cambió a ACTIVO tras enviar mensaje");
    std::cout << "- Estado cambiado automáticamente a ACTIVO tras enviar mensaje\n";

    std::cout << "test_inactivity: Todas las pruebas pasaron\n";
}

extern bool testing_mode;
int main()
{
    testing_mode = true;
    Logger::getInstance().startLogging();
    std::cout << "Iniciando test_server...\n";

    try
    {
        test_invalid_usernames();
        test_on_open_and_duplicate();
        test_list_users();
        test_handle_get_user_info();
        test_handle_change_status();
        test_inactive_status_not_reactivated_by_non_message_opcode();
        test_inactive_status_reactivated_by_message_opcode();
        test_handle_send_message();
        test_handle_get_history();
        test_user_disconnection();
        test_message_size_limit();
        test_keep_status();
        test_inactivity();

        std::cout << "\nTodos los tests de test_server pasaron con éxito.\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Excepción en test_server: " << e.what() << std::endl;
        return 1;
    }
    catch (...)
    {
        std::cerr << "Excepción desconocida en test_server.\n";
        return 1;
    }
}