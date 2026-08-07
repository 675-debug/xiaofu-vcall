#pragma once
#include "../db/DbManager.h"
#include <string>
#include <vector>

class JoinHandler;

class ChatHandler {
public:
    ChatHandler(DbManager* manager, JoinHandler* onlineUsers);

    bool createMessage(const std::string& sender, const std::string& receiver,
                       const std::string& content, ChatMessage& message);
    bool loadHistory(const std::string& username, const std::string& peer,
                     std::vector<ChatMessage>& messages) const;
    bool deleteConversation(const std::string& username, const std::string& peer) const;
    bool clearAllMessages(const std::string& username) const;
    int receiverFd(const std::string& username) const;

private:
    DbManager* db;
    JoinHandler* joinHandler;
};
