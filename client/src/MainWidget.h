#pragma once
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

signals:
    void switchToChat();
    void switchToCall();
    void logoutRequested();

private slots:
    void on_btnMin_clicked();
    void on_btnMax_clicked();
    void on_btnClose_clicked();
    void on_btnAvatar_clicked();
    void on_btnSettings_clicked();
    void on_btnLogout_clicked();
    void on_btnCloseModal_clicked();
    void on_btnSend_clicked();
    void on_btnCall_clicked();
    void on_editMessage_returnPressed();
    void on_listContacts_itemClicked(QListWidgetItem* item);
    void on_listOffline_itemClicked(QListWidgetItem* item);
    void on_editSearch_textChanged(const QString& text);

private:
    void addContactItem(QListWidget* list, const QString& name,
                        const QString& status, const QString& color);
    void addMessageItem(const QString& avatarText, const QString& color,
                        const QString& text, const QString& time, bool mine);
    void appendLocalMessage();
    void selectContact(QListWidgetItem* item, bool online);
    void filterContactList(QListWidget* list, const QString& keyword);

    Ui::MainWidget* ui;
    NetworkManager* networkManager = nullptr;
};
