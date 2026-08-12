#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <mysql/mysql.h>

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

struct CallRecord {
    std::string callId;
    std::string caller;
    std::string callee;
    std::string state;           // "ended"
    std::int64_t createdAt = 0;  // ms
    std::int64_t acceptedAt = 0;
    std::int64_t connectedAt = 0;
    std::int64_t endedAt = 0;
    std::int64_t duration = 0;   // ms
    std::string endReason;
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

    // Call history
    bool saveCallRecord(const CallRecord& record);
    bool loadCallRecords(const std::string& username, int limit,
                         std::vector<CallRecord>& records);

    // Loginlog — single-account single-login
    bool resetAllLoginStatus();
    bool tryLogin(const std::string& username);
    bool setOffline(const std::string& username);
    bool initLoginlogForUser(const std::string& username);

    // Check the MySQL connection and reconnect if it was lost.
    bool ping();

    void close();

private:
    MYSQL* mysql;
    std::string databasePath;
};
