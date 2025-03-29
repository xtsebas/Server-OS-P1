#pragma once
#include <unordered_map>
#include <string>
#include <vector>
#include "websocket_handler.h"

extern std::unordered_map<std::string, ConnectionData> connections;
extern std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> chat_history;
extern std::unordered_map<std::string, UserStatus> last_user_status;
extern std::mutex connections_mutex;
extern std::vector<std::pair<std::string, std::string>> general_chat_history;
extern std::condition_variable inactivity_cv;
extern std::mutex inactivity_mutex;
extern bool user_marked_inactive;