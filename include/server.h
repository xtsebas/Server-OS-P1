#pragma once
#include "crow.h"
#include "validate_middleware.h"

using App = crow::App<ValidateMiddleware>;

void setup_routes(App& app);
void start_server();
