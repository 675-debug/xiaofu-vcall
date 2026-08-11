#include "../src/db/DbManager.h"
#include "../src/handler/ChatHandler.h"
#include "../src/handler/JoinHandler.h"
#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace {
int failures = 0;

void check(bool condition, const char* name) {
    if (condition) {
        std::printf("PASS: %s\n", name);
    } else {
        std::printf("FAIL: %s\n", name);
        ++failures;
    }
}
}

int main() {
    DbManager db;
    check(db.open("mysql-test"), "open mysql test database");
    check(db.createTables(), "create message table");

    // 联系人资料必须是单向关系：Alice 添加 Bob，不会自动反向添加。
    check(db.insertUser("alice", "hash", "alice@example.com"), "create alice account");
    check(db.insertUser("bob", "hash", "bob@example.com"), "create bob account");
    check(db.saveProfile("alice", "小爱", 3), "save profile");
    check(db.saveProfile("bob", "小波", 5), "save peer profile");
    check(db.addContact("alice", "bob"), "add contact");
    std::vector<ContactProfile> contacts;
    check(db.loadContacts("alice", contacts) && contacts.size() == 1,
          "load one-way contact");
    check(contacts.size() == 1 && contacts[0].username == "bob"
          && contacts[0].nickname == "小波" && contacts[0].avatarSeed == 5,
          "contact profile is returned");

    // 好友申请在接受前不能直接创建联系人；接受后才写入双方联系人列表。
    check(db.createFriendRequest("bob", "alice"), "create friend request");
    std::vector<FriendRequest> pendingRequests;
    check(db.loadPendingFriendRequests("alice", pendingRequests) && pendingRequests.size() == 1,
          "receiver loads one pending friend request");
    check(pendingRequests.size() == 1 && pendingRequests[0].senderUsername == "bob"
          && pendingRequests[0].nickname == "小波" && pendingRequests[0].avatarSeed == 5,
          "friend request includes sender profile");
    std::vector<ContactProfile> bobContacts;
    check(db.loadContacts("bob", bobContacts) && bobContacts.empty(),
          "pending friend request does not create contact");
    check(db.respondToFriendRequest("alice", "bob", true), "accept friend request");
    check(db.loadContacts("bob", bobContacts) && bobContacts.size() == 1
          && bobContacts[0].username == "alice", "accepted request creates reverse contact");

    JoinHandler joinHandler(nullptr);
    check(joinHandler.handleJoin(1001, "alice"), "alice joins");
    check(joinHandler.handleJoin(1002, "bob"), "bob joins");
    // 同一 TCP 连接在未退出旧账号前，不能再加入第二个账号。
    // 否则旧会话会残留，后续心跳超时可能误踢新账号的连接。
    check(!joinHandler.handleJoin(1001, "charlie"),
          "same connection rejects switching accounts before leave");
    check(joinHandler.usernameOf(1001) == "alice" && joinHandler.fdOf("charlie") == -1,
          "rejected account switch keeps original session intact");

    ChatHandler chatHandler(&db, &joinHandler);
    ChatMessage firstMessage;
    check(chatHandler.createMessage("alice", "bob", "你好，Bob！", firstMessage),
          "save Chinese message");
    check(firstMessage.sender == "alice" && firstMessage.receiver == "bob"
          && firstMessage.content == "你好，Bob！", "message fields preserved");
    check(chatHandler.receiverFd("bob") == 1002, "resolve online receiver fd");

    ChatMessage replyMessage;
    check(chatHandler.createMessage("bob", "alice", "你好，Alice！", replyMessage),
          "save reverse-direction message");
    std::vector<ChatMessage> history;
    check(chatHandler.loadHistory("alice", "bob", history), "load conversation history");
    check(history.size() == 2 && history[0].content == "你好，Bob！"
          && history[1].content == "你好，Alice！", "history keeps both directions and order");

    check(chatHandler.deleteConversation("alice", "bob"), "delete current conversation");
    history.clear();
    check(chatHandler.loadHistory("alice", "bob", history) && history.empty(),
          "conversation history is empty after delete");

    ChatMessage otherMessage;
    check(chatHandler.createMessage("alice", "charlie", "离线消息", otherMessage),
          "save offline message");
    check(chatHandler.receiverFd("charlie") == -1, "offline receiver has no fd");
    check(chatHandler.clearAllMessages("alice"), "clear all user messages");
    history.clear();
    check(chatHandler.loadHistory("alice", "charlie", history) && history.empty(),
          "all user messages are removed");

    // 两个线程各用一个 MySQL 连接，模拟线程池 worker 并发保存消息。
    {
        DbManager setupDatabase;
        check(setupDatabase.open("mysql-test"), "open concurrent test database");
        check(setupDatabase.createTables(), "create concurrent test tables");
        check(setupDatabase.insertUser("workerA", "hash", "a@example.com"),
              "create worker A");
        check(setupDatabase.insertUser("workerB", "hash", "b@example.com"),
              "create worker B");
    }

    std::atomic<int> savedMessages{0};
    auto saveMessages = [&](const std::string& sender, const std::string& receiver) {
        DbManager workerDatabase;
        if (!workerDatabase.open("mysql-test"))
            return;
        for (int index = 0; index < 20; ++index) {
            ChatMessage message;
            message.sender = sender;
            message.receiver = receiver;
            message.content = "concurrent-" + std::to_string(index);
            if (workerDatabase.saveMessage(message))
                ++savedMessages;
        }
    };
    std::thread firstWorker(saveMessages, "workerA", "workerB");
    std::thread secondWorker(saveMessages, "workerB", "workerA");
    firstWorker.join();
    secondWorker.join();

    {
        DbManager verifyDatabase;
        std::vector<ChatMessage> concurrentHistory;
        check(verifyDatabase.open("mysql-test"), "reopen concurrent test database");
        check(verifyDatabase.loadConversation("workerA", "workerB", concurrentHistory),
              "load concurrent history");
        check(savedMessages.load() == 40 && concurrentHistory.size() == 40,
              "two database workers save all messages");
    }

    std::printf(failures == 0 ? "ALL PASS\n" : "FAILURES: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
