#pragma once
#include <QWidget>

class NetworkManager;
class QLineEdit;

namespace Ui {
class ForgotPasswordWidget;
}

class ForgotPasswordWidget : public QWidget {
    Q_OBJECT
public:
    explicit ForgotPasswordWidget(QWidget* parent = nullptr);
    ~ForgotPasswordWidget();

    void setNetworkManager(NetworkManager* manager);

signals:
    void backToLoginWidget();

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void on_btnSend_clicked();
    void on_btnBackLogin_clicked();
    void on_btnMin_clicked();
    void on_btnMax_clicked();
    void on_btnClose_clicked();
    void onForgotResult(int code, const QString& message);

private:
    bool isValidPassword(const QString& password) const;
    void setFieldError(QLineEdit* edit, bool hasError);

    Ui::ForgotPasswordWidget* ui;
    NetworkManager*            networkManager = nullptr;
};
