#pragma once
#include "crow.h"
#include "../include/websocket_handler.h"

struct ValidateMiddleware
{
    struct context {}; // requerido por Crow

    void before_handle(crow::request& req, crow::response& res, context&)
    {
        if (req.url == "/" && req.method == "GET"_method &&
            req.headers.find("upgrade") == req.headers.end())
        {
            const char* name = req.url_params.get("name");

            if (!name || std::string(name).empty() || std::string(name) == "~") {
                res.code = 400;
                res.write("Nombre inválido");
                res.end();
                return;
            }

            {
                std::lock_guard<std::mutex> lock(connections_mutex);
                auto it = connections.find(name);
                if (it != connections.end() && it->second.status != UserStatus::DISCONNECTED) {
                    res.code = 400;
                    res.write("Usuario ya conectado o activo");
                    res.end();
                    return;
                }
            }

            res.code = 200;
            res.write("OK");
            res.end();
            return;
        }
    }

    void after_handle(crow::request&, crow::response&, context&) {
        // no se usa
    }
};
