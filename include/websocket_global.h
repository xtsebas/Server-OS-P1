#pragma once
#include <unordered_map>
#include <string>
#include "websocket_handler.h"

extern std::unordered_map<std::string, ConnectionData> connections;
// Si tienes un chat_history, también extern.
