#pragma once
#include <QWidget>

class NetworkManager;
class QListWidgetItem;

namespace Ui {
class ChatWidget;
}

class ChatWidget : public QWidget {
    Q_OBJECT
public:
    explicit ChatWidget(QWidget* parent = nullptr);
    ~ChatWidget();

    void setNetworkManager(NetworkManager* manager);

signals:
    void backToMain();
    void startCall();

private slots:
    void on_btnSend_clicked();
    void on_btnBack_clicked();
    void on_btnCall_clicked();
    void on_btnMin_clicked();
    void on_btnMax_clicked();
    void on_btnClose_clicked();
    void on_editMessage_returnPressed();
    void on_convList_itemClicked(QListWidgetItem* item);

private:
    void appendLocalMessage();
    void addMessageItem(const QString& avatarText, const QString& color,
                        const QString& text, const QString& time, bool mine);

    Ui::ChatWidget* ui;
    NetworkManager* networkManager = nullptr;
};
