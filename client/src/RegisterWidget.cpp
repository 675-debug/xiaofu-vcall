#include "RegisterWidget.h"
#include "ui_RegisterWidget.h"
#include "network/NetworkManager.h"
#include <QMessageBox>
#include <QPainter>
#include <QLinearGradient>
#include <QLineEdit>
#include <QRegularExpression>
#include <QStyle>
#include <QRandomGenerator>

RegisterWidget::RegisterWidget(QWidget* parent)
    : QWidget(parent), ui(new Ui::RegisterWidget) {
    ui->setupUi(this);
    avatarSeed = QRandomGenerator::global()->bounded(6);
    static const QStringList colors = {"#10B981", "#F97316", "#8B5CF6", "#007AFF", "#EC4899", "#14B8A6"};
    ui->avatarPreview->setStyleSheet(QString("background:%1;color:#FFFFFF;border-radius:18px;font-weight:700;").arg(colors.at(avatarSeed)));
    connect(ui->editNickname, &QLineEdit::textChanged, this, [this](const QString& text) {
        ui->avatarPreview->setText(text.trimmed().isEmpty() ? QStringLiteral("匿") : text.left(1).toUpper());
    });

    // Enter 键：账号→邮箱→密码→确认密码，最后回车提交注册
    connect(ui->editUser, &QLineEdit::returnPressed, this, [this]() { ui->editMail->setFocus(); });
    connect(ui->editMail, &QLineEdit::returnPressed, this, [this]() { ui->editPass->setFocus(); });
    connect(ui->editPass, &QLineEdit::returnPressed, this, [this]() { ui->editPass2->setFocus(); });
    connect(ui->editPass2, &QLineEdit::returnPressed, ui->btnRegister, &QPushButton::click);

    // 实时校验：不满足条件时当前输入框红色高亮
    connect(ui->editUser, &QLineEdit::textChanged, this, [this](const QString&) {
        setFieldError(ui->editUser, false);
    });
    connect(ui->editMail, &QLineEdit::textChanged, this, [this](const QString& text) {
        setFieldError(ui->editMail, !text.trimmed().isEmpty() && !isValidEmail(text.trimmed()));
    });
    connect(ui->editPass, &QLineEdit::textChanged, this, [this](const QString& text) {
        setFieldError(ui->editPass, !text.isEmpty() && !isValidPassword(text));
        setFieldError(ui->editPass2, !ui->editPass2->text().isEmpty() && ui->editPass2->text() != text);
    });
    connect(ui->editPass2, &QLineEdit::textChanged, this, [this](const QString& text) {
        setFieldError(ui->editPass2, !text.isEmpty() && text != ui->editPass->text());
    });
}

RegisterWidget::~RegisterWidget() {
    delete ui;
}

bool RegisterWidget::isValidEmail(const QString& email) const {
    static const QRegularExpression re(R"(^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}$)");
    return re.match(email).hasMatch();
}

bool RegisterWidget::isValidPassword(const QString& password) const {
    static const QRegularExpression re(R"(^(?=.*[a-z])(?=.*[A-Z]).{6,}$)");
    return re.match(password).hasMatch();
}

void RegisterWidget::setFieldError(QLineEdit* edit, bool hasError) {
    if (edit->property("error").toBool() == hasError) return;
    edit->setProperty("error", hasError);
    edit->style()->unpolish(edit);
    edit->style()->polish(edit);
}

void RegisterWidget::setNetworkManager(NetworkManager* manager) {
    networkManager = manager;
    if (networkManager) {
        connect(networkManager, &NetworkManager::registerResult, this, &RegisterWidget::onRegisterResult);
    }
}

void RegisterWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    QLinearGradient g(0, 0, 0, height());
    g.setColorAt(0, QColor("#FAFAFA"));
    g.setColorAt(1, QColor("#F0F0F3"));
    painter.fillRect(rect(), g);
}

void RegisterWidget::on_btnRegister_clicked() {
    if (!networkManager) {
        QMessageBox::warning(this, "提示", "未连接到服务器");
        return;
    }
    const QString username = ui->editUser->text().trimmed();
    const QString nickname = ui->editNickname->text().trimmed();
    const QString email = ui->editMail->text().trimmed();
    const QString password = ui->editPass->text();
    const QString repeatedPassword = ui->editPass2->text();
    if (username.isEmpty()) {
        setFieldError(ui->editUser, true);
        QMessageBox::warning(this, "提示", "请输入账号");
        return;
    }
    if (nickname.isEmpty()) {
        setFieldError(ui->editNickname, true);
        QMessageBox::warning(this, "提示", "请输入昵称");
        return;
    }
    if (!isValidEmail(email)) {
        setFieldError(ui->editMail, true);
        QMessageBox::warning(this, "提示", "邮箱格式不正确");
        return;
    }
    if (!isValidPassword(password)) {
        setFieldError(ui->editPass, true);
        QMessageBox::warning(this, "提示", "密码需包含大小写字母且至少 6 位");
        return;
    }
    if (password != repeatedPassword) {
        setFieldError(ui->editPass2, true);
        QMessageBox::warning(this, "提示", "两次密码不一致");
        return;
    }
    networkManager->sendRegister(username, email, password, nickname, avatarSeed);
}

void RegisterWidget::onRegisterResult(int code, const QString& message) {
    if (code == 0) {
        QMessageBox::information(this, "注册成功", "账号注册成功，请返回登录");
        emit backToLoginWidget();
    } else {
        QMessageBox::warning(this, "注册失败", QString("[%1] %2").arg(code).arg(message));
    }
}

void RegisterWidget::on_btnBackLogin_clicked() {
    emit backToLoginWidget();
}

void RegisterWidget::on_btnMin_clicked() {
    window()->showMinimized();
}

void RegisterWidget::on_btnMax_clicked() {
    if (window()->isMaximized())
        window()->showNormal();
    else
        window()->showMaximized();
}

void RegisterWidget::on_btnClose_clicked() {
    window()->close();
}
