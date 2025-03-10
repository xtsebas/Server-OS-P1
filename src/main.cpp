#include "crow.h"
#include <unordered_map>
#include <mutex>
#include <thread>

// Estructura para almacenar la información de cada conexión
struct ConnectionData
{
    std::string username;
    crow::websocket::connection* conn;
};

// Mapa de conexiones activas
std::unordered_map<std::string, ConnectionData> connections;
std::mutex connections_mutex;

// Función para manejar una conexión WebSocket en un hilo separado
void handle_client(crow::websocket::connection* conn, std::string client_ip)
{
    CROW_LOG_INFO << "Nuevo hilo manejando la conexión de: " << client_ip;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CROW_LOG_INFO << "Hilo finalizado para: " << client_ip;
}

int main()
{
    crow::SimpleApp app;

    // Ruta raíz de prueba
    CROW_ROUTE(app, "/")([]()
                         { return "WebSocket Server is running!"; });

    // Nueva ruta: /hello
    CROW_ROUTE(app, "/hello")([]()
                              { return "Hello, this is another route!"; });

    // Nueva ruta: /json
    CROW_ROUTE(app, "/json")([]()
                             {
        crow::json::wvalue response;
        response["message"] = "This is a JSON response";
        response["status"] = "success";
        return response; });

    // Configuración de WebSockets
    CROW_WEBSOCKET_ROUTE(app, "/ws")
        .onopen([](crow::websocket::connection& conn)
                {
                    std::lock_guard<std::mutex> lock(connections_mutex);
                    std::string client_ip = conn.get_remote_ip();

                    // Verificar si ya está conectado
                    if (connections.find(client_ip) != connections.end())
                    {
                        conn.send_text("Error: Ya hay una sesión activa para esta IP.");
                        conn.close("Sesión duplicada");
                        return;
                    }

                    // Guardar conexión
                    connections[client_ip] = {client_ip, &conn};
                    CROW_LOG_INFO << "Nueva conexión desde: " << client_ip;

                    // Crear un hilo para manejar esta conexión
                    std::thread(handle_client, &conn, client_ip).detach();
                })
        .onmessage([](crow::websocket::connection& conn, const std::string& data, bool is_binary)
                   {
                       std::lock_guard<std::mutex> lock(connections_mutex);
                       std::string client_ip = conn.get_remote_ip();

                       CROW_LOG_INFO << "Mensaje recibido de " << client_ip << ": " << data;

                       // Reenviar mensaje a todos los clientes conectados
                       for (auto &[ip, conn_data] : connections)
                       {
                           if (conn_data.conn)
                           {
                               conn_data.conn->send_text(client_ip + ": " + data);
                           }
                       }
                   })
        .onclose([](crow::websocket::connection& conn, const std::string& reason)
                 {
                     std::lock_guard<std::mutex> lock(connections_mutex);
                     std::string client_ip = conn.get_remote_ip();
                     connections.erase(client_ip);
                     CROW_LOG_INFO << "Conexión cerrada: " << client_ip << " - " << reason;
                 });

    // Configurar puerto, multihilo y ejecutar la app
    app.bindaddr("0.0.0.0").port(18080).multithreaded().run();
}
