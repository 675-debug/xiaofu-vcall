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
#include <QMessageBox>
#include <QtGlobal>
#include <QCursor>
#include <QWindow>

const QSize MainWindow::kAuthWindowSizePhysical = QSize(800, 880);
const QSize MainWindow::kWorkspaceWindowSizePhysical = QSize(1861, 1065);
const QSize MainWindow::kMinWorkspaceWindowSizePhysical = QSize(186, 106);

QSize MainWindow::logicalSizeFor(const QSize& physical) const {
    const qreal dpr = devicePixelRatioF();
    return QSize(qMax(1, qRound(physical.width() / dpr)),
                 qMax(1, qRound(physical.height() / dpr)));
}

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
    // 无边框窗口需要持续收到鼠标移动事件以识别边缘缩放热区。
    setMouseTracking(true);

    // 启动默认显示登录页，窗口保持登录界面尺寸（登录成功后切主界面并 resize）
    showAuthPage(1);
}

MainWindow::~MainWindow() {
    delete ui;
}

void MainWindow::initNetwork() {
    // 正式客户端默认连接云服务器；开发测试可通过环境变量覆盖，无需修改源码。
    const QString serverHost = qEnvironmentVariable("XIAOFU_SERVER_HOST", "8.137.152.134");
    bool portValid = false;
    const int configuredPort = qEnvironmentVariableIntValue("XIAOFU_SERVER_PORT", &portValid);
    const quint16 serverPort = portValid && configuredPort > 0 && configuredPort <= 65535
        ? static_cast<quint16>(configuredPort) : static_cast<quint16>(9000);
    qDebug() << "[MainWindow] connect server:" << serverHost << serverPort;
    networkManager->connectToServer(serverHost, serverPort);
}

void MainWindow::showAuthPage(int index) {
    ui->stackMain->setCurrentIndex(index);
    resize(logicalSizeFor(kAuthWindowSizePhysical));
    applyPageMinimumSize();
}

void MainWindow::showWorkspacePage(int index) {
    ui->stackMain->setCurrentIndex(index);
    resize(logicalSizeFor(kWorkspaceWindowSizePhysical));
    applyPageMinimumSize();
    // 打印逻辑尺寸与 DPR，避免高分屏缩放导致窗口尺寸误判。
    qDebug().noquote() << "[MainWindow] WORKSPACE_SIZE targetPhysical=" << kWorkspaceWindowSizePhysical
                       << "logical=" << size() << "dpr=" << devicePixelRatio();
}

void MainWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    // 无边框窗口的边缘/四角缩放依赖 Qt6 QWindow::startSystemResize，
    // windowHandle 在窗口显示后才存在，因此在这里安装事件过滤器。
    installResizeFilters();
    // 首次显示时屏幕/DPR 才最终确定，按当前页重新应用目标物理尺寸，
    // 避免窗口在 show 之前按错误 DPR resize 导致显示偏大。
    const int idx = ui->stackMain->currentIndex();
    if (idx == 1 || idx == 2 || idx == 3)
        showAuthPage(idx);
    else
        showWorkspacePage(idx);
}

void MainWindow::resetToLoginPage() {
    networkManager->logout();
    ui->mainPage->resetSession();
    ui->loginPage->resetLoginForm();
    showAuthPage(1);
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
    connect(ui->mainPage, &MainWidget::switchToCall, this, [this](const QString& peerName) {
        ui->callPage->startOutgoingCall(peerName);
        showWorkspacePage(5);
    });
    connect(ui->mainPage, &MainWidget::logoutRequested, this, [this]() {
        resetToLoginPage();
    });

    // 发消息界面 → 返回主界面 / 发起通话
    connect(ui->chatPage, &ChatWidget::backToMain, this, [this]() {
        showWorkspacePage(0);
    });
    connect(ui->chatPage, &ChatWidget::startCall, this, [this](const QString& peerName) {
        ui->callPage->startOutgoingCall(peerName);
        showWorkspacePage(5);
    });

    // 通话界面 → 返回主界面
    connect(ui->callPage, &CallWidget::backToMainWidget, this, [this]() {
        showWorkspacePage(0);
    });
    connect(ui->callPage, &CallWidget::incomingCallRequested, this, [this]() {
        showWorkspacePage(5);
    });

    // 重复登录/被踢下线：join 被拒时回到登录页并提示，不能静默覆盖当前会话。
    connect(networkManager, &NetworkManager::joinResult, this, [this](int code, const QString& message) {
        if (code == 0)
            return;
        qDebug().noquote() << "[MainWindow] join rejected:" << message;
        resetToLoginPage();
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("账号已在其他设备登录或已被踢下线，请重新登录"));
    });

    // 登录成功 → 主界面
    connect(ui->loginPage, &LoginWidget::loginSucceeded, this, [this](const QString& username) {
        qDebug() << "[MainWindow] login succeeded, switch workspace:" << username;
        ui->mainPage->setCurrentUser(username);
        showWorkspacePage(0);
        // 本地预览自测：XIAOFU_PREVIEW_TEST=1 时不打电话，登录后直接进通话页打开摄像头。
        if (qEnvironmentVariableIntValue("XIAOFU_PREVIEW_TEST") == 1) {
            qDebug().noquote() << "[MainWindow] local preview test mode enabled";
            ui->callPage->showLocalPreviewTest();
            showWorkspacePage(5);
        }
    });
}

void MainWindow::installResizeFilters() {
    if (resizeFiltersInstalled)
        return;
    resizeFiltersInstalled = true;
    // 无边框窗口自身 + 所有子控件统一安装事件过滤器：
    // 1) 边缘/四角识别并调用系统级缩放（Qt6 QWindow::startSystemResize）
    // 2) 页面内 titleBar 按住左键拖动窗口
    installEventFilter(this);
    const QList<QWidget*> widgets = findChildren<QWidget*>();
    for (QWidget* w : widgets) {
        w->installEventFilter(this);
        w->setMouseTracking(true);
    }
}

Qt::Edges MainWindow::resizeEdgesAt(const QPoint& globalPos) const {
    const QPoint local = mapFromGlobal(globalPos);
    Qt::Edges edges;
    if (local.x() <= kResizeGrip)
        edges |= Qt::LeftEdge;
    if (local.x() >= width() - kResizeGrip)
        edges |= Qt::RightEdge;
    if (local.y() <= kResizeGrip)
        edges |= Qt::TopEdge;
    if (local.y() >= height() - kResizeGrip)
        edges |= Qt::BottomEdge;
    return edges;
}

void MainWindow::updateResizeCursor(const QPoint& globalPos) {
    const Qt::Edges edges = resizeEdgesAt(globalPos);
    Qt::CursorShape shape = Qt::ArrowCursor;
    if (edges == (Qt::LeftEdge | Qt::TopEdge) || edges == (Qt::RightEdge | Qt::BottomEdge))
        shape = Qt::SizeFDiagCursor;
    else if (edges == (Qt::RightEdge | Qt::TopEdge) || edges == (Qt::LeftEdge | Qt::BottomEdge))
        shape = Qt::SizeBDiagCursor;
    else if (edges == Qt::LeftEdge || edges == Qt::RightEdge)
        shape = Qt::SizeHorCursor;
    else if (edges == Qt::TopEdge || edges == Qt::BottomEdge)
        shape = Qt::SizeVerCursor;
    setCursor(shape);
}

void MainWindow::startWindowResize(const QPoint& globalPos) {
    const Qt::Edges edges = resizeEdgesAt(globalPos);
    if (edges == Qt::Edges())
        return;
    QWindow* windowHandle = window()->windowHandle();
    if (!windowHandle)
        return;
    // Qt6 原生支持无边框窗口的系统级缩放，能正确跟随 DPI 缩放与贴边吸附。
    windowHandle->startSystemResize(edges);
}

void MainWindow::applyPageMinimumSize() {
    const int idx = ui->stackMain->currentIndex();
    if (idx == 1 || idx == 2 || idx == 3) {
        // 认证页：保持登录/注册/忘记密码页面原有最小尺寸，本轮不为 resize 大改。
        const QSize authMin = logicalSizeFor(QSize(400, 510));
        setMinimumSize(authMin);
        if (QWidget* page = ui->stackMain->currentWidget())
            page->setMinimumSize(authMin);
        // 工作区页面最小尺寸解除，避免 QStackedLayout 取各页 max 把窗口最小尺寸顶上去。
        ui->mainPage->setMinimumSize(0, 0);
        ui->chatPage->setMinimumSize(0, 0);
        ui->callPage->setMinimumSize(0, 0);
    } else {
        // 主面板/通话页：允许缩到默认尺寸的约 10%（186x106 物理像素）。
        setMinimumSize(logicalSizeFor(kMinWorkspaceWindowSizePhysical));
        // QStackedWidget 的最小尺寸是所有子页面的最大值，只有把全部页面
        // 的最小尺寸约束解除，顶层窗口才能真正缩到 186x106。
        ui->mainPage->setMinimumSize(0, 0);
        ui->chatPage->setMinimumSize(0, 0);
        ui->callPage->setMinimumSize(0, 0);
        ui->loginPage->setMinimumSize(0, 0);
        ui->registerPage->setMinimumSize(0, 0);
        ui->forgotPage->setMinimumSize(0, 0);
        // 子页面内部控件仍带 .ui 硬编码的 minimumSize（侧栏、按钮等），
        // 会通过布局把窗口最小尺寸顶回去；递归解除，使窗口可自由缩小。
        clearMinSizeRecursive(ui->mainPage);
        clearMinSizeRecursive(ui->chatPage);
        clearMinSizeRecursive(ui->callPage);
    }
}

void MainWindow::clearMinSizeRecursive(QWidget* w) {
    if (!w)
        return;
    w->setMinimumSize(0, 0);
    const QList<QWidget*> children = w->findChildren<QWidget*>();
    for (QWidget* c : children)
        c->setMinimumSize(0, 0);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            const QPoint globalPos = me->globalPos();
            // 边缘/四角：交给系统级缩放，子控件不再处理该按下事件。
            if (resizeEdgesAt(globalPos) != Qt::Edges()) {
                startWindowResize(globalPos);
                return true;
            }
            // 页面内 titleBar：记录拖动起点。
            auto* w = qobject_cast<QWidget*>(watched);
            if (w && (w->objectName() == QStringLiteral("titleBar") || w == this)) {
                dragOffset = globalPos - window()->frameGeometry().topLeft();
                dragging = true;
            }
        }
    } else if (event->type() == QEvent::MouseMove) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (dragging && (me->buttons() & Qt::LeftButton)) {
            window()->move(me->globalPos() - dragOffset);
            return true;
        }
        // 没有按键按下时更新边缘缩放光标。
        if (!(me->buttons() & Qt::LeftButton))
            updateResizeCursor(me->globalPos());
    } else if (event->type() == QEvent::MouseButtonRelease) {
        dragging = false;
    }
    return QWidget::eventFilter(watched, event);
}

void MainWindow::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor("#C7C7CC"));
}
