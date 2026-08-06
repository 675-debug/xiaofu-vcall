#pragma once
#include <QWidget>

class NetworkManager;

namespace Ui {
class LoginWidget;
}

class LoginWidget : public QWidget {
    Q_OBJECT
public:
    explicit LoginWidget(QWidget* parent = nullptr);
    ~LoginWidget();

    void setNetworkManager(NetworkManager* manager);

signals:
    void switchToRegister();
    void switchToForgotPassword();
    void loginSucceeded(const QString& username);

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void on_btnLogin_clicked();
    void on_btnRegister_clicked();
    void on_btnForgot_clicked();
    void on_btnMin_clicked();
    void on_btnMax_clicked();
    void on_btnClose_clicked();
    void onLoginResult(int code, const QString& message, const QString& username);

private:
    static const int kMaxLoginAttempts = 6;
    int loginAttempts = 0;

    Ui::LoginWidget* ui;
    NetworkManager*  networkManager = nullptr;
};
