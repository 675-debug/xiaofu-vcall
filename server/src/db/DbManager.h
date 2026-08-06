#pragma once
#include <string>
#include <vector>
#include <sqlite3.h>

struct ChatMessage {
    long long id = 0;
    std::string sender;
    std::string receiver;
    std::string content;
    std::string sentAt;
};

struct ContactProfile {
    std::string username;
    std::string nickname;
    int avatarSeed = 0;
};

struct FriendRequest {
    std::string senderUsername;
    std::string nickname;
    int avatarSeed = 0;
    std::string createdAt;
};

class DbManager {
public:
    DbManager();
    ~DbManager();

    bool open(const std::string& dbPath);
    bool createTables();
    bool insertUser(const std::string& username, const std::string& passwordHash,
                    const std::string& email, const std::string& nickname = "", int avatarSeed = 0);
    bool findUser(const std::string& username, std::string& passwordHash);
    bool saveProfile(const std::string& username, const std::string& nickname, int avatarSeed);
    bool addContact(const std::string& ownerUsername, const std::string& contactUsername);
    bool loadContacts(const std::string& ownerUsername, std::vector<ContactProfile>& contacts);
    bool createFriendRequest(const std::string& senderUsername, const std::string& receiverUsername);
    bool loadPendingFriendRequests(const std::string& receiverUsername,
                                   std::vector<FriendRequest>& requests);
    bool respondToFriendRequest(const std::string& receiverUsername,
                                const std::string& senderUsername, bool accepted);
    bool updatePassword(const std::string& username, const std::string& newPasswordHash);
    bool saveMessage(ChatMessage& message);
    bool loadConversation(const std::string& username, const std::string& peer,
                          std::vector<ChatMessage>& messages);
    bool deleteConversation(const std::string& username, const std::string& peer);
    bool deleteAllMessages(const std::string& username);
    void close();

private:
    sqlite3* db;
};
