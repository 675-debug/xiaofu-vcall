#include "ChatHandler.h"
#include "JoinHandler.h"

ChatHandler::ChatHandler(DbManager* manager, JoinHandler* onlineUsers)
    : db(manager), joinHandler(onlineUsers) {}

bool ChatHandler::createMessage(const std::string& sender, const std::string& receiver,
                                const std::string& content, ChatMessage& message) {
    if (!db || sender.empty() || receiver.empty() || content.empty()) return false;
    message.sender = sender;
    message.receiver = receiver;
    message.content = content;
    return db->saveMessage(message);
}

bool ChatHandler::loadHistory(const std::string& username, const std::string& peer,
                              std::vector<ChatMessage>& messages) const {
    return db && !username.empty() && !peer.empty() && db->loadConversation(username, peer, messages);
}

bool ChatHandler::deleteConversation(const std::string& username, const std::string& peer) const {
    return db && !username.empty() && !peer.empty() && db->deleteConversation(username, peer);
}

bool ChatHandler::clearAllMessages(const std::string& username) const {
    return db && !username.empty() && db->deleteAllMessages(username);
}

int ChatHandler::receiverFd(const std::string& username) const {
    return joinHandler ? joinHandler->fdOf(username) : -1;
}
