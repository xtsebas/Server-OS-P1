#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include "../include/websocket_handler.h"
#include "../include/logger.h"
#include "../include/websocket_global.h"

/*
 MockConnection simula una crow::websocket::connection
 para poder capturar lo que el servidor enviaría,
 sin necesidad de un WebSocket real.
*/

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

/*
 Utilities to parse the first opcode, length, etc.
 From your existing read_uint8 / read_string_8 logic.
*/

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

/*
 Helper for checking buffer content easily
*/

void check_opcode(const std::string &payload, uint8_t expected_opcode, const std::string &test_name)
{
    assert(get_opcode(payload) == expected_opcode && ("❌ " + test_name + " -> opcode no coincide").c_str());
    std::cout << "✅ " << test_name << ": opcode " << (int)expected_opcode << " verificado\n";
}

/*
 We'll run a series of small functions, each returning void and using 'assert'.
 If any assert fails, the program ends with an error.
*/

void test_on_open_and_duplicate()
{
    /*
      Limpieza de 'connections' global y 'chat_history' si tu WebSocketHandler lo requiere.
      Asume que 'connections' y 'chat_history' son extern o static en websocket_handler.cpp
    */
    connections.clear();

    // 1) Primer usuario: alice
    MockConnection conn_alice("127.0.0.1");
    WebSocketHandler::on_open(conn_alice, "alice");
    assert(connections.size() == 1 && "❌ on_open: No se registró alice");
    assert(!conn_alice.closed && "❌ on_open: alice cerrada indebidamente");

    // 2) Conectar duplicado: alice de nuevo
    MockConnection conn_alice2("127.0.0.1");
    WebSocketHandler::on_open(conn_alice2, "alice");
    assert(conn_alice2.closed && "❌ on_open: Nombre duplicado no fue cerrado");
    assert(connections.size() == 1 && "❌ on_open: se insertó duplicado erroneamente");

    // 3) Reconexión: dejar conn_alice a nullptr y reabrir
    connections["alice"].conn = nullptr;
    MockConnection conn_alice_re("127.0.0.1");
    WebSocketHandler::on_open(conn_alice_re, "alice");
    // Reconexión
    assert(!conn_alice_re.closed && "❌ on_open: reconexión erroneamente cerrada");
    assert(connections["alice"].conn == &conn_alice_re && "❌ on_open: reconexión no reusó la misma entry");
    std::cout << "✅ test_on_open_and_duplicate\n";
}

void test_list_users()
{
    // asume que 'alice' sigue en connections
    // Add 'bob'
    MockConnection conn_bob("127.0.0.1");
    WebSocketHandler::on_open(conn_bob, "bob");
    assert(!conn_bob.closed);

    // 2) Llamar handle_list_users
    size_t old_count = conn_bob.sent_messages.size();
    WebSocketHandler::handle_list_users(conn_bob);
    assert(conn_bob.sent_messages.size() == old_count + 1 && "❌ handle_list_users no envió nada");

    // parse the last message
    std::string payload = conn_bob.sent_messages.back();
    check_opcode(payload, 0x51, "test_list_users");

    size_t offset = 1;
    uint8_t n = (uint8_t)payload[offset++];
    std::cout << "📊 test_list_users — payload users: " << (int)n << " vs connections.size(): " << connections.size() << "\n";
    std::cout << "Esperaba " << connections.size() << " usuarios, recibí: " << (int)n << "\n";
    assert(n == connections.size() && "❌ handle_list_users: número de usuarios no coincide");

    for (int i = 0; i < n; i++)
    {
        std::string name = get_string_8(payload, offset);
        uint8_t st = (uint8_t)payload[offset++];
        // std::cout << "User " << i << " => " << name << ", st=" << (int)st << "\n";
        // Podrías checar valores exactos
    }

    std::cout << "✅ test_list_users\n";
}

void test_handle_get_user_info()
{
    // bob pide info de alice
    MockConnection conn_bob("127.0.0.1");
    connections["bob"].conn = &conn_bob;
    size_t old_count = conn_bob.sent_messages.size();

    // build data => [0x02][len 'alice']['alice']
    std::string data;
    data.push_back((char)0x02);
    data.push_back((char)5);
    data += "alice";

    // offset sim
    size_t offset = 0;
    // call handle_get_user_info
    WebSocketHandler::handle_get_user_info(conn_bob, data, offset);

    assert(conn_bob.sent_messages.size() == old_count + 1 && "❌ get_user_info no envió nada");
    std::string payload = conn_bob.sent_messages.back();
    check_opcode(payload, 0x52, "test_handle_get_user_info");
    offset = 1;
    std::string name = get_string_8(payload, offset);
    uint8_t st = (uint8_t)payload[offset++];
    assert(name == "alice" && "❌ get_user_info: nombre no coincide");
    std::cout << "✅ test_handle_get_user_info\n";
}

void test_handle_change_status()
{
    // Cambiar status de 'alice' => op=3 => newStatus=2 => OCUPADO
    MockConnection conn_alice("127.0.0.1");
    connections["alice"].conn = &conn_alice;

    // data => [0x03][uint8_status=2]
    std::string data;
    data.push_back((char)0x03);
    data.push_back((char)2);

    size_t offset = 0;
    WebSocketHandler::handle_change_status(conn_alice, "alice", data, offset);

    // Revisa si 'alice' cambió a status=2
    assert(connections["alice"].status == UserStatus::OCUPADO && "❌ handle_change_status: alice no pasó a OCUPADO");

    // Revisa notificación 0x54
    assert(!conn_alice.sent_messages.empty() && "❌ handle_change_status: no se envió nada al propio alice");
    std::string payload = conn_alice.sent_messages.back();
    check_opcode(payload, 0x54, "test_handle_change_status");
    offset = 1;
    std::string name = get_string_8(payload, offset);
    uint8_t st = (uint8_t)payload[offset++];
    assert(name == "alice" && st == 2 && "❌ handle_change_status: datos de notificación incorrectos");

    std::cout << "✅ test_handle_change_status\n";
}

void test_handle_send_message()
{
    // mandar un msg => [dest, msg]
    MockConnection conn_alice("127.0.0.1");
    connections["alice"].conn = &conn_alice;

    MockConnection conn_bob("127.0.0.1");
    connections["bob"].conn = &conn_bob;

    // data => [0x04][len 'bob']['bob'][len 'hola']['hola']
    std::string data;
    data.push_back((char)0x04);
    data.push_back((char)3);
    data += "bob";
    data.push_back((char)4);
    data += "hola";

    size_t offset = 0;
    WebSocketHandler::handle_send_message(conn_alice, "alice", data, offset);

    // Revisa si 'bob' recibió (check conn_bob.sent_messages)
    assert(!conn_bob.sent_messages.empty() && "❌ handle_send_message no envió nada a bob");
    std::string payload = conn_bob.sent_messages.back();
    check_opcode(payload, 0x55, "test_handle_send_message to bob");

    offset = 1;
    std::string origin = get_string_8(payload, offset);
    std::string msg = get_string_8(payload, offset);
    assert(origin == "alice" && msg == "hola" && "❌ handle_send_message: contenido incorrecto");
    std::cout << "✅ test_handle_send_message\n";
}

void test_handle_get_history()
{
    // Se asume que hay algun historial
    // Por ejemplo, handle_send_message ya guardó 'hola' en 'alice|bob'
    // data => [0x05][len chat][chat]
    MockConnection conn_alice("127.0.0.1");
    connections["alice"].conn = &conn_alice;

    std::string data;
    data.push_back((char)0x05);
    data.push_back((char)3);
    data += "bob"; // chat "bob"

    size_t offset = 0;
    WebSocketHandler::handle_get_history(conn_alice, "alice", data, offset);

    // Revisa lo último enviado a alice
    assert(!conn_alice.sent_messages.empty() && "❌ handle_get_history no envió nada");
    std::string payload = conn_alice.sent_messages.back();
    check_opcode(payload, 0x56, "test_handle_get_history");
    // parsear la data => [0x56][num][ (len_user, user, len_msg, msg)*n ]
    offset = 1;
    uint8_t n = (uint8_t)payload[offset++];
    assert(n > 0 && "❌ handle_get_history: sin mensajes? esperabamos >=1");
    std::cout << "✅ test_handle_get_history\n";
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

        std::cout << "\n✅ Todos los tests de test_server pasaron con éxito.\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "❌ Excepción en test_server: " << e.what() << std::endl;
        return 1;
    }
    catch (...)
    {
        std::cerr << "❌ Excepción desconocida en test_server.\n";
        return 1;
    }
}
