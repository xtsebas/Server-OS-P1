#include "../include/server.h"
#include "../include/websocket_handler.h"
#include "../include/logger.h"

void setup_routes(App& app)
{
    Logger::getInstance().startLogging();


    CROW_WEBSOCKET_ROUTE(app, "/")
        .onaccept([](const crow::request &req, void **userdata)
                  {
                      const char *name = req.url_params.get("name");

                      if (!name || std::string(name).empty())
                      {
                          CROW_LOG_INFO << "Conexión rechazada: Falta el nombre de usuario.";
                          return false;
                      }

                      *userdata = new std::string(name);
                      return true;
                  })
        .onopen([](crow::websocket::connection &conn)
                {
                    std::string username = *(static_cast<std::string *>(conn.userdata()));
                    WebSocketHandler::on_open(conn, username);
                })
        .onmessage(WebSocketHandler::on_message)
        .onclose([](crow::websocket::connection &conn, const std::string &reason, uint16_t code)
                 {
                    WebSocketHandler::on_close(conn, reason, code);

                    if (conn.userdata())
                    {
                        delete static_cast<std::string *>(conn.userdata());
                        conn.userdata(nullptr);
                    } });
                
}


void start_server()
{
    App app;
    setup_routes(app);
    app.bindaddr("0.0.0.0").port(18080).multithreaded().run();
}
