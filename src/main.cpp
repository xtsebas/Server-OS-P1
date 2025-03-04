#include "crow.h"

int main()
{
    crow::SimpleApp app; // Define la aplicación Crow

    // Ruta raíz
    CROW_ROUTE(app, "/")([](){
        return "Hello world";
    });

    // Nueva ruta: /hello
    CROW_ROUTE(app, "/hello")([](){
        return "Hello, this is another route!";
    });

    // Nueva ruta: /json
    CROW_ROUTE(app, "/json")([](){
        crow::json::wvalue response;
        response["message"] = "This is a JSON response";
        response["status"] = "success";
        return response;
    });

    // Configurar puerto, multihilo y ejecutar la app
    app.bindaddr("127.0.0.1").port(18080).multithreaded().run();
}
