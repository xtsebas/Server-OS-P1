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



void test_on_open_and_duplicate()
{

    connections.clear();

    // 1) Primer usuario: alice
    MockConnection conn_alice("127.0.0.1");
    WebSocketHandler::on_open(conn_alice, "alice");
    assert(connections.size() == 1 && "on_open: No se registró alice");
    assert(!conn_alice.closed && "on_open: alice cerrada indebidamente");

    // 2) Conectar duplicado: alice de nuevo
    MockConnection conn_alice2("127.0.0.1");
    WebSocketHandler::on_open(conn_alice2, "alice");
    assert(conn_alice2.closed && "on_open: Nombre duplicado no fue cerrado");
    assert(connections.size() == 1 && "on_open: se insertó duplicado erroneamente");

    // 3) Reconexión: dejar conn_alice a nullptr y reabrir
    connections["alice"].conn = nullptr;
    MockConnection conn_alice_re("127.0.0.1");
    WebSocketHandler::on_open(conn_alice_re, "alice");
    // Reconexión
    assert(!conn_alice_re.closed && " on_open: reconexión erroneamente cerrada");
    assert(connections["alice"].conn == &conn_alice_re && "on_open: reconexión no reusó la misma entry");
    std::cout << "test_on_open_and_duplicate\n";
}

void test_list_users()
{

    MockConnection conn_bob("127.0.0.1");
    WebSocketHandler::on_open(conn_bob, "bob");
    assert(!conn_bob.closed);

    size_t old_count = conn_bob.sent_messages.size();
    WebSocketHandler::handle_list_users(conn_bob);
    assert(conn_bob.sent_messages.size() == old_count + 1 && "handle_list_users no envió nada");


    std::string payload = conn_bob.sent_messages.back();
    check_opcode(payload, 0x51, "test_list_users");

    size_t offset = 1;
    uint8_t n = (uint8_t)payload[offset++];
    std::cout << "test_list_users — payload users: " << (int)n << " vs connections.size(): " << connections.size() << "\n";
    std::cout << "Esperaba " << connections.size() << " usuarios, recibí: " << (int)n << "\n";
    assert(n == connections.size() && "handle_list_users: número de usuarios no coincide");

    for (int i = 0; i < n; i++)
    {
        std::string name = get_string_8(payload, offset);
        uint8_t st = (uint8_t)payload[offset++];
    }

    std::cout << "test_list_users\n";
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

    std::string data;
    data.push_back((char)0x02);
    data.push_back((char)5);
    data += "alice";

    size_t old_count = conn_bob.sent_messages.size();


    WebSocketHandler::on_message(conn_bob, data, true); 


    assert(conn_bob.sent_messages.size() == old_count + 1 && "get_user_info no envió nada");

    std::string payload = conn_bob.sent_messages.back();

    std::cout << "Payload recibido (hex): ";
    for (unsigned char c : payload) {
        printf("%02X ", c);
    }
    std::cout << "\n";

    check_opcode(payload, 0x52, "test_handle_get_user_info");

    size_t offset = 1;
    std::string name = get_string_8(payload, offset);
    uint8_t st = (uint8_t)payload[offset++];
    std::string ip_address = get_string_8(payload, offset);

    assert(name == "alice" && "get_user_info: nombre no coincide");
    assert(st == 1 && "get_user_info: status no es ACTIVO");
    assert(ip_address == "192.168.1.10" && "get_user_info: IP no coincide");

    std::cout << "test_handle_get_user_info\n";
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

    auto simulate_status_change = [&](uint8_t new_status_byte, UserStatus expected_status) {
        std::string data;
        data.push_back((char)0x03);       // Opcode cambio de estado
        data.push_back((char)new_status_byte);  // Estado deseado

        size_t old_count = conn.sent_messages.size();

        WebSocketHandler::on_message(conn, data, true);
        assert(connections["alice"].status == expected_status && "❌ Estado no actualizado correctamente");

        assert(conn.sent_messages.size() > old_count && "❌ No se notificó cambio de estado");
        std::string payload = conn.sent_messages.back();
        check_opcode(payload, 0x54, "Cambio de estado");

        size_t offset = 1;
        std::string name = get_string_8(payload, offset);
        uint8_t st = (uint8_t)payload[offset++];

        assert(name == "alice" && st == new_status_byte && "❌ Payload incorrecto");
    };

    // ACTIVO -> OCUPADO
    simulate_status_change(2, UserStatus::OCUPADO);

    // OCUPADO -> ACTIVO
    simulate_status_change(1, UserStatus::ACTIVO);

    // ACTIVO -> INACTIVO
    simulate_status_change(3, UserStatus::INACTIVO);

    // INACTIVO -> OCUPADO
    simulate_status_change(2, UserStatus::OCUPADO);

    // OCUPADO -> INACTIVO
    simulate_status_change(3, UserStatus::INACTIVO);

    // INACTIVO -> ACTIVO
    simulate_status_change(1, UserStatus::ACTIVO);

    std::cout << "test_handle_change_status\n";
}


void test_handle_send_message()
{
    std::cout << "test_handle_send_message\n";

    connections.clear();
    chat_history.clear();

    std::cout << "test_handle_send_message 1\n";

    MockConnection conn_alice("127.0.0.1");
    connections["alice"] = ConnectionData{
        "alice", "uuid-alice", &conn_alice,
        UserStatus::ACTIVO, std::chrono::steady_clock::now()
    };

    std::cout << "test_handle_send_message 2\n";

    MockConnection conn_bob("127.0.0.1");
    connections["bob"] = ConnectionData{
        "bob", "uuid-bob", &conn_bob,
        UserStatus::ACTIVO, std::chrono::steady_clock::now()
    };

    std::cout << "test_handle_send_message 3\n";

    std::string data;
    data.push_back((char)0x04);        
    data.push_back((char)3); data += "bob";   
    data.push_back((char)4); data += "hola";  

    std::cout << "test_handle_send_message 4\n";

    size_t old_count = conn_bob.sent_messages.size();

    WebSocketHandler::on_message(conn_alice, data, true);

    assert(conn_bob.sent_messages.size() == old_count + 1 && "❌ handle_send_message no envió nada a bob");

    std::string payload = conn_bob.sent_messages.back();
    check_opcode(payload, 0x55, "test_handle_send_message to bob");

    size_t offset = 1;
    std::string origin = get_string_8(payload, offset);
    std::string msg = get_string_8(payload, offset);

    assert(origin == "alice" && msg == "hola" && "❌ handle_send_message: contenido incorrecto");

    std::cout << "test_handle_send_message\n";
}


void test_handle_get_history()
{
    std::cout << "test_handle_get_history\n";

    connections.clear();
    chat_history.clear();

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

    std::string chat_id = (std::string("alice") < "bob") ? "alice|bob" : "bob|alice";
    chat_history[chat_id].push_back({"alice", "hola"});
    chat_history[chat_id].push_back({"bob", "respuesta"});

    std::string data;
    data.push_back((char)0x05);         
    data.push_back((char)3);            
    data += "bob";

    size_t old_count = conn_alice.sent_messages.size();

    WebSocketHandler::on_message(conn_alice, data, true);

    assert(conn_alice.sent_messages.size() == old_count + 1 && "handle_get_history no envió nada");

    std::string payload = conn_alice.sent_messages.back();
    check_opcode(payload, 0x56, "test_handle_get_history");

    size_t offset = 1;
    uint8_t num = (uint8_t)payload[offset++];

    std::cout << "test_handle_get_history recibió " << (int)num << " mensajes\n";
    assert(num >= 2 && "handle_get_history: no se recibieron los mensajes esperados");

    for (int i = 0; i < num; i++) {
        std::string autor = get_string_8(payload, offset);
        std::string contenido = get_string_8(payload, offset);
        std::cout << "- " << autor << ": " << contenido << "\n";
    }

    std::cout << "test_handle_get_history\n";
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

    // Cambiar estado de Alice a OCUPADO
    std::string data;
    data.push_back((char)0x03); 
    data.push_back((char)2);   
    WebSocketHandler::on_message(conn_alice, data, true);
    assert(connections["alice"].status == UserStatus::OCUPADO && "Cambio de estado fallido");

    // Simular desconexión de Alice
    WebSocketHandler::on_close(conn_alice, "Cerrando", 1000);
    assert(last_user_status["alice"] == UserStatus::OCUPADO && "Estado no guardado al desconectar");

    // Simular reconexión de Alice
    MockConnection conn_alice_re("127.0.0.1");
    WebSocketHandler::on_open(conn_alice_re, "alice");
    assert(connections["alice"].status == UserStatus::OCUPADO && "Estado no restaurado correctamente al reconectar");

    std::cout << "test_keep_status\n";
}

void test_handle_get_history_messages()
{
    std::cout << "test_handle_get_history_messages\n";

    connections.clear();
    chat_history.clear();
    general_chat_history.clear();

    MockConnection conn_alice("127.0.0.1");
    WebSocketHandler::on_open(conn_alice, "alice");
    MockConnection conn_bob("127.0.0.1");
    WebSocketHandler::on_open(conn_bob, "bob");

    // Enviar mensaje a chat general
    std::string msg_general;
    msg_general.push_back((char)0x04);
    msg_general.push_back((char)1); // len "~"
    msg_general += "~";
    msg_general.push_back((char)5);
    msg_general += "hello";

    std::cout << "Enviando mensaje al chat general...\n";
    WebSocketHandler::on_message(conn_alice, msg_general, true);
    assert(general_chat_history.size() == 1 && "Mensaje no guardado en el chat general");

    // Enviar mensaje privado a Bob
    std::string msg_privado;
    msg_privado.push_back((char)0x04);
    msg_privado.push_back((char)3); // len "bob"
    msg_privado += "bob";
    msg_privado.push_back((char)4);
    msg_privado += "hola";

    std::cout << "Enviando mensaje privado a Bob...\n";
    WebSocketHandler::on_message(conn_alice, msg_privado, true);
    std::string chat_id = "alice|bob";
    assert(chat_history[chat_id].size() == 1 && "Mensaje privado no guardado correctamente");

    // Solicitar historial de chat general
    std::string req_general;
    req_general.push_back((char)0x05);
    req_general.push_back((char)1);
    req_general += "~";

    std::cout << "Solicitando historial del chat general...\n";
    size_t old_count = conn_alice.sent_messages.size();
    WebSocketHandler::on_message(conn_alice, req_general, true);
    assert(conn_alice.sent_messages.size() == old_count + 1 && "No se envió historial de chat general");

    // Solicitar historial privado
    std::string req_privado;
    req_privado.push_back((char)0x05);
    req_privado.push_back((char)3);
    req_privado += "bob";

    std::cout << "Solicitando historial privado...\n";
    old_count = conn_alice.sent_messages.size();
    WebSocketHandler::on_message(conn_alice, req_privado, true);
    assert(conn_alice.sent_messages.size() == old_count + 1 && "No se envió historial de chat privado");

    std::cout << "test_handle_get_history_messages\n";
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
                    std::cout << "Usuario " + username + " marcado como INACTIVO (inactivo por " 
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

    std::cout << "test_inactivity\n";
}

extern bool testing_mode;
int main()
{
    testing_mode = true;
    Logger::getInstance().startLogging();
    std::cout << "Iniciando test_server...\n";

    try
    {
        test_on_open_and_duplicate();
        test_list_users();
        test_handle_get_user_info();
        test_handle_change_status();
        test_handle_send_message();
        test_handle_get_history();
        test_keep_status();
        test_handle_get_history_messages();
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
