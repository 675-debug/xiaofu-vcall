#include "LoginWidget.h"
#include "ui_LoginWidget.h"
#include "network/NetworkManager.h"
#include <QDebug>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QLinearGradient>

LoginWidget::LoginWidget(QWidget* parent)
    : QWidget(parent), ui(new Ui::LoginWidget) {
    ui->setupUi(this);

    // Enter 键：账号框回车跳转密码框，密码框回车直接登录
    connect(ui->editUser, &QLineEdit::returnPressed, this, [this]() {
        ui->editPass->setFocus();
    });
    connect(ui->editPass, &QLineEdit::returnPressed, ui->btnLogin, &QPushButton::click);
}

LoginWidget::~LoginWidget() {
    delete ui;
}

void LoginWidget::setNetworkManager(NetworkManager* manager) {
    networkManager = manager;
    if (networkManager) {
        connect(networkManager, &NetworkManager::loginResult, this, &LoginWidget::onLoginResult);
    }
}

void LoginWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    QLinearGradient g(0, 0, 0, height());
    g.setColorAt(0, QColor("#FAFAFA"));
    g.setColorAt(1, QColor("#F0F0F3"));
    painter.fillRect(rect(), g);
}

void LoginWidget::on_btnLogin_clicked() {
    if (loginAttempts >= kMaxLoginAttempts) {
        qDebug() << "Login blocked: max attempts reached, please restart client";
        QMessageBox::warning(this, "提示", "登录尝试次数已达上限，请重启客户端后重试");
        return;
    }
    if (!networkManager) {
        QMessageBox::warning(this, "提示", "未连接到服务器");
        return;
    }
    const QString username = ui->editUser->text().trimmed();
    const QString password = ui->editPass->text();
    if (username.isEmpty() || password.isEmpty()) {
        QMessageBox::warning(this, "提示", "请输入账号和密码");
        return;
    }
    qDebug() << "Login attempt" << (loginAttempts + 1) << "of" << kMaxLoginAttempts << "for user" << username;
    alreadyLoggedInDialogShown = false;
    networkManager->sendLogin(username, password);
}

void LoginWidget::onLoginResult(int code, const QString& message, const QString& username) {
    Q_UNUSED(message);
    if (code == Protocol::Ok) {
        loginAttempts = 0;
        qDebug() << "Login success for user" << username;
        emit loginSucceeded(username);
    } else if (code == Protocol::AccountAlreadyLoggedIn) {
        if (alreadyLoggedInDialogShown)
            return;
        alreadyLoggedInDialogShown = true;
        qDebug() << "Login rejected: account already logged in elsewhere for user" << username;
        ui->btnLogin->setEnabled(true);
        // 模态提示框只提供"确定"按钮；用户点击确定后才清空表单并回到账号框。
        // 保持登录页，不进入主界面，不创建会话，不自动重试，不退出程序。
        QMessageBox::information(this, "提示", "该账户已在别处登录。");
        resetLoginForm();
    } else {
        loginAttempts++;
        int remaining = kMaxLoginAttempts - loginAttempts;
        qDebug() << "Login failed for user" << username
                 << "attempts used:" << loginAttempts
                 << "remaining:" << remaining;
        // 清空账号和密码，光标回到账号框，防止他人直接看到/继续尝试
        ui->editUser->clear();
        ui->editPass->clear();
        ui->editUser->setFocus();
        // 统一提示，不区分账户不存在/密码错误，防止用户名枚举爆破
        if (remaining <= 0) {
            ui->btnLogin->setEnabled(false);
            qDebug() << "Login disabled: max attempts reached";
            QMessageBox::warning(this, "登录失败", "账户或密码错误，登录尝试次数已达上限，请重启客户端后重试");
        } else {
            QMessageBox::warning(this, "登录失败", QString("账户或密码错误，当前还有 %1 次机会").arg(remaining));
        }
    }
}

void LoginWidget::resetLoginForm() {
    ui->btnLogin->setEnabled(true);
    ui->editUser->clear();
    ui->editPass->clear();
    ui->editUser->setFocus();
}

void LoginWidget::on_btnRegister_clicked() {
    emit switchToRegister();
}

void LoginWidget::on_btnForgot_clicked() {
    emit switchToForgotPassword();
}

void LoginWidget::on_btnMin_clicked() {
    window()->showMinimized();
}

void LoginWidget::on_btnMax_clicked() {
    if (window()->isMaximized())
        window()->showNormal();
    else
        window()->showMaximized();
}

void LoginWidget::on_btnClose_clicked() {
    window()->close();
}
