#pragma once
#include <unordered_map>
#include <string>
#include "websocket_handler.h"

extern std::unordered_map<std::string, ConnectionData> connections;
extern std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> chat_history;
extern std::unordered_map<std::string, UserStatus> last_user_status;
