#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "LoginWidget.h"
#include "RegisterWidget.h"
#include "ForgotPasswordWidget.h"
#include "MainWidget.h"
#include "ChatWidget.h"
#include "CallWidget.h"
#include "network/NetworkManager.h"
#include <QPainter>
#include <QMouseEvent>
#include <QDebug>

const QSize MainWindow::kAuthWindowSize = QSize(900, 930);
const QSize MainWindow::kWorkspaceWindowSize = QSize(1650, 1000);

MainWindow::MainWindow(QWidget* parent)
    : QWidget(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);

    networkManager = new NetworkManager(this);
    ui->mainPage->setNetworkManager(networkManager);
    ui->loginPage->setNetworkManager(networkManager);
    ui->registerPage->setNetworkManager(networkManager);
    ui->forgotPage->setNetworkManager(networkManager);
    ui->chatPage->setNetworkManager(networkManager);
    ui->callPage->setNetworkManager(networkManager);

    setupConnections();
    initNetwork();

    // 去掉系统标题栏/边框，保留页面内白色 titleBar（缩小/放大/关闭由页面按钮处理）
    setWindowFlags(Qt::FramelessWindowHint);
    installTitleBarDragging();

    // 启动默认显示登录页，窗口保持登录界面尺寸（登录成功后切主界面并 resize，见 TODO）
    showAuthPage(1);
}

MainWindow::~MainWindow() {
    delete ui;
}

void MainWindow::initNetwork() {
    qDebug() << "[MainWindow] connect server:" << "127.0.0.1" << 9000;
    networkManager->connectToServer("127.0.0.1", 9000);
}

void MainWindow::showAuthPage(int index) {
    ui->stackMain->setCurrentIndex(index);
    resize(kAuthWindowSize);
}

void MainWindow::showWorkspacePage(int index) {
    ui->stackMain->setCurrentIndex(index);
    resize(kWorkspaceWindowSize);
}

void MainWindow::setupConnections() {
    connect(ui->loginPage, &LoginWidget::switchToRegister, this, [this]() {
        showAuthPage(2);
    });
    connect(ui->loginPage, &LoginWidget::switchToForgotPassword, this, [this]() {
        showAuthPage(3);
    });
    connect(ui->registerPage, &RegisterWidget::backToLoginWidget, this, [this]() {
        showAuthPage(1);
    });
    connect(ui->forgotPage, &ForgotPasswordWidget::backToLoginWidget, this, [this]() {
        showAuthPage(1);
    });

    // 主界面 → 发消息 / 视频通话 / 退出登录
    connect(ui->mainPage, &MainWidget::switchToChat, this, [this]() {
        showWorkspacePage(4);
    });
    connect(ui->mainPage, &MainWidget::switchToCall, this, [this]() {
        ui->callPage->startDemoCall();
        showWorkspacePage(5);
    });
    connect(ui->mainPage, &MainWidget::logoutRequested, this, [this]() {
        networkManager->logout();
        ui->mainPage->resetSession();
        showAuthPage(1);
    });

    // 发消息界面 → 返回主界面 / 发起通话
    connect(ui->chatPage, &ChatWidget::backToMain, this, [this]() {
        showWorkspacePage(0);
    });
    connect(ui->chatPage, &ChatWidget::startCall, this, [this]() {
        ui->callPage->startDemoCall();
        showWorkspacePage(5);
    });

    // 通话界面 → 返回主界面
    connect(ui->callPage, &CallWidget::backToMainWidget, this, [this]() {
        showWorkspacePage(0);
    });

    // 登录成功 → 主界面
    connect(ui->loginPage, &LoginWidget::loginSucceeded, this, [this](const QString& username) {
        qDebug() << "[MainWindow] login succeeded, switch workspace:" << username;
        ui->mainPage->setCurrentUser(username);
        showWorkspacePage(0);
    });
}

void MainWindow::installTitleBarDragging() {
    const QList<QWidget*> titleBars = findChildren<QWidget*>(QStringLiteral("titleBar"));
    for (QWidget* titleBar : titleBars)
        titleBar->installEventFilter(this);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            dragOffset = mouseEvent->globalPos() - window()->frameGeometry().topLeft();
            dragging = true;
        }
    } else if (event->type() == QEvent::MouseMove) {
        if (dragging) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->buttons() & Qt::LeftButton)
                window()->move(mouseEvent->globalPos() - dragOffset);
        }
    } else if (event->type() == QEvent::MouseButtonRelease) {
        dragging = false;
    }
    return QWidget::eventFilter(watched, event);
}

void MainWindow::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor("#C7C7CC"));
}
