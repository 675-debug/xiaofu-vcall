#pragma once
#include <QJsonArray>
#include <QHash>
#include <QSet>
#include <QWidget>

class NetworkManager;
class QListWidget;
class QListWidgetItem;

namespace Ui {
class MainWidget;
}

class MainWidget : public QWidget {
    Q_OBJECT
public:
    explicit MainWidget(QWidget* parent = nullptr);
    ~MainWidget();

    void setNetworkManager(NetworkManager* manager);
    void setCurrentUser(const QString& username);
    void resetSession();

signals:
    void switchToChat();
    void switchToCall(const QString& peerName);
    void logoutRequested();

private slots:
    void on_btnMin_clicked();
    void on_btnMax_clicked();
    void on_btnClose_clicked();
    void on_btnAvatar_clicked();
    void on_btnSettings_clicked();
    void on_btnAddContact_clicked();
    void on_btnLogout_clicked();
    void on_btnCloseModal_clicked();
    void on_btnClearChatRecords_clicked();
    void on_btnSend_clicked();
    void on_btnCall_clicked();
    void on_editMessage_returnPressed();
    void on_listContacts_itemClicked(QListWidgetItem* item);
    void on_listOffline_itemClicked(QListWidgetItem* item);
    void on_editSearch_textChanged(const QString& text);
    void onChatReceived(const QString& sender, const QString& content, const QString& sentAt);
    void onHistoryReceived(const QString& peer, const QJsonArray& messages);
    void onConversationDeleted(const QString& peer, int code, const QString& message);
    void onAllChatsCleared(int code, const QString& message);
    void onContactsReceived(const QJsonArray& contacts);
    void onPresenceChanged(const QString& username, bool online);
    void onChatSendResult(const QString& peer, bool online, int code, const QString& message);
    void onFriendRequestReceived(const QString& sender, const QString& nickname, int avatarSeed);

private:
    void addContactItem(QListWidget* list, const QString& name,
                        const QString& status, const QString& color);
    void addMessageItem(const QString& avatarText, const QString& color,
                        const QString& text, const QString& time, bool mine);
    void appendLocalMessage();
    void selectContact(QListWidgetItem* item, bool online);
    void filterContactList(QListWidget* list, const QString& keyword);
    void showContactMenu(QListWidget* list, const QPoint& position, bool online);
    void clearMessageList();
    void appendHistoryMessage(const QString& sender, const QString& content, const QString& sentAt);
    void renderContacts();
    void addStatusMessage(const QString& text, bool online);
    void setContactOnlineState(const QString& username, bool online, bool addNotice);
    QString avatarColor(int avatarSeed) const;
    void updateChatAvatar(const QString& nickname, int avatarSeed, bool online);
    void showFriendRequestDialog(const QString& sender, const QString& nickname, int avatarSeed);

    Ui::MainWidget* ui;
    NetworkManager* networkManager = nullptr;
    QString currentContact;
    bool currentContactOnline = false;
    struct ContactInfo { QString nickname; int avatarSeed = 0; bool online = false; };
    QHash<QString, ContactInfo> contactsByUsername;
    QSet<QString> ignoredHistoryPeers;
};
