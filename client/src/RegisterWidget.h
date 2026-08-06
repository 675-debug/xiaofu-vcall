#pragma once
#include <QWidget>

class NetworkManager;
class QLineEdit;

namespace Ui {
class RegisterWidget;
}

class RegisterWidget : public QWidget {
    Q_OBJECT
public:
    explicit RegisterWidget(QWidget* parent = nullptr);
    ~RegisterWidget();

    void setNetworkManager(NetworkManager* manager);

signals:
    void backToLoginWidget();

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void on_btnRegister_clicked();
    void on_btnBackLogin_clicked();
    void on_btnMin_clicked();
    void on_btnMax_clicked();
    void on_btnClose_clicked();
    void onRegisterResult(int code, const QString& message);

private:
    bool isValidEmail(const QString& email) const;
    bool isValidPassword(const QString& password) const;
    void setFieldError(QLineEdit* edit, bool hasError);

    Ui::RegisterWidget* ui;
    NetworkManager*     networkManager = nullptr;
    int avatarSeed = 0;
};
