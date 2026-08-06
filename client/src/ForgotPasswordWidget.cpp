#include "ForgotPasswordWidget.h"
#include "ui_ForgotPasswordWidget.h"
#include "network/NetworkManager.h"
#include <QMessageBox>
#include <QPainter>
#include <QLinearGradient>
#include <QLineEdit>
#include <QRegularExpression>
#include <QStyle>

ForgotPasswordWidget::ForgotPasswordWidget(QWidget* parent)
    : QWidget(parent), ui(new Ui::ForgotPasswordWidget) {
    ui->setupUi(this);

    // Enter 键：账号→新密码，最后回车提交重置
    connect(ui->editUser, &QLineEdit::returnPressed, this, [this]() { ui->editNewPass->setFocus(); });
    connect(ui->editNewPass, &QLineEdit::returnPressed, ui->btnSend, &QPushButton::click);

    // 实时校验：新密码不满足规则时红色高亮
    connect(ui->editUser, &QLineEdit::textChanged, this, [this](const QString&) {
        setFieldError(ui->editUser, false);
    });
    connect(ui->editNewPass, &QLineEdit::textChanged, this, [this](const QString& text) {
        setFieldError(ui->editNewPass, !text.isEmpty() && !isValidPassword(text));
    });
}

ForgotPasswordWidget::~ForgotPasswordWidget() {
    delete ui;
}

bool ForgotPasswordWidget::isValidPassword(const QString& password) const {
    static const QRegularExpression re(R"(^(?=.*[a-z])(?=.*[A-Z]).{6,}$)");
    return re.match(password).hasMatch();
}

void ForgotPasswordWidget::setFieldError(QLineEdit* edit, bool hasError) {
    if (edit->property("error").toBool() == hasError) return;
    edit->setProperty("error", hasError);
    edit->style()->unpolish(edit);
    edit->style()->polish(edit);
}

void ForgotPasswordWidget::setNetworkManager(NetworkManager* manager) {
    networkManager = manager;
    if (networkManager) {
        connect(networkManager, &NetworkManager::forgotResult, this, &ForgotPasswordWidget::onForgotResult);
    }
}

void ForgotPasswordWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    QLinearGradient g(0, 0, 0, height());
    g.setColorAt(0, QColor("#FAFAFA"));
    g.setColorAt(1, QColor("#F0F0F3"));
    painter.fillRect(rect(), g);
}

void ForgotPasswordWidget::on_btnSend_clicked() {
    if (!networkManager) {
        QMessageBox::warning(this, "提示", "未连接到服务器");
        return;
    }
    const QString username = ui->editUser->text().trimmed();
    const QString newPassword = ui->editNewPass->text();
    if (username.isEmpty()) {
        setFieldError(ui->editUser, true);
        QMessageBox::warning(this, "提示", "请输入账号");
        return;
    }
    if (!isValidPassword(newPassword)) {
        setFieldError(ui->editNewPass, true);
        QMessageBox::warning(this, "提示", "新密码需包含大小写字母且至少 6 位");
        return;
    }
    networkManager->sendForgot(username, newPassword);
}

void ForgotPasswordWidget::onForgotResult(int code, const QString& message) {
    if (code == 0) {
        QMessageBox::information(this, "重置成功", "密码已重置，请返回登录");
        emit backToLoginWidget();
    } else {
        QMessageBox::warning(this, "重置失败", QString("[%1] %2").arg(code).arg(message));
    }
}

void ForgotPasswordWidget::on_btnBackLogin_clicked() {
    emit backToLoginWidget();
}

void ForgotPasswordWidget::on_btnMin_clicked() {
    window()->showMinimized();
}

void ForgotPasswordWidget::on_btnMax_clicked() {
    if (window()->isMaximized())
        window()->showNormal();
    else
        window()->showMaximized();
}

void ForgotPasswordWidget::on_btnClose_clicked() {
    window()->close();
}
