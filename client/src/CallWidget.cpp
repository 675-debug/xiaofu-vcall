#include "CallWidget.h"
#include "ui_CallWidget.h"
#include "network/NetworkManager.h"
#include "video/VideoCallController.h"
#include "video/WebRtcBridge.h"
#include <QDir>
#include <QStandardPaths>
#include <QDebug>
#include <QDialog>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPoint>
#include <QResizeEvent>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QWebChannel>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QWebEngineDownloadRequest>
#else
#include <QWebEngineDownloadItem>
#endif
#include <QWebEngineFullScreenRequest>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <QtGlobal>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QCameraInfo>
#endif

namespace {
const int kPipDragThreshold = 6;
const int kPipMargin = 12;
const int kPipDefaultRightMargin = 26;
const int kPipDefaultTopMargin = 16;

// 构建 WebRTC ICE 服务器列表：默认使用自建 STUN/TURN，环境变量可覆盖。
QVariantList buildIceServers() {
    QVariantList servers;
    const QString stunUrl = qEnvironmentVariable("XIAOFU_STUN_URL",
                                                 QStringLiteral("stun:8.137.152.134:3478"));
    if (!stunUrl.isEmpty()) {
        servers.append(QVariantMap{{QStringLiteral("urls"), stunUrl}});
    }
    const QString turnUrl = qEnvironmentVariable("XIAOFU_TURN_URL",
                                                 QStringLiteral("turn:8.137.152.134:3478"));
    if (!turnUrl.isEmpty()) {
        const QString username = qEnvironmentVariable("XIAOFU_TURN_USERNAME",
                                                      QStringLiteral("xiaofu"));
        const QString credential = qEnvironmentVariable("XIAOFU_TURN_CREDENTIAL",
                                                       QStringLiteral("D5SGHLEeuvbG2trYW2+ffTyh+mP80FrR"));
        // turn:/turns: 协议必须同时提供 username/credential，否则 RTCPeerConnection 构造失败。
        // 凭据不完整时跳过 TURN，避免整个通话因配置缺失直接中断。
        const bool turnNeedsAuth = turnUrl.startsWith(QStringLiteral("turn:"))
                                   || turnUrl.startsWith(QStringLiteral("turns:"));
        if (turnNeedsAuth && (username.isEmpty() || credential.isEmpty())) {
            qWarning() << "[Call] TURN credential missing, skip TURN server:" << turnUrl;
        } else {
            QVariantMap turn;
            turn.insert(QStringLiteral("urls"), turnUrl);
            if (!username.isEmpty())
                turn.insert(QStringLiteral("username"), username);
            if (!credential.isEmpty())
                turn.insert(QStringLiteral("credential"), credential);
            servers.append(turn);
        }
    }
    return servers;
}

// 读取 ICE 传输策略环境变量：XIAOFU_ICE_POLICY=relay 时强制所有媒体走 TURN 中继，
// 用于隔离“直连路径(NAT/内网)坏、TURN 中继好”的场景；其他值返回空(默认 all)。
QString iceTransportPolicyFromEnv() {
    const QString policy = qEnvironmentVariable("XIAOFU_ICE_POLICY").trimmed().toLower();
    if (policy == QLatin1String("relay"))
        return QStringLiteral("relay");
    return QString();
}

// 读取候选诊断过滤模式环境变量：XIAOFU_DIAG_CAND_FILTER=ipv4-udp-host-only|no-ipv6|no-tcp。
// 用于临时隔离 IPv6/TCP/srflx/relay 候选，定位“ICE connected 但媒体路径错误”的场景；其他值返回空(默认 none)。
QString diagCandidateFilterFromEnv() {
    const QString mode = qEnvironmentVariable("XIAOFU_DIAG_CAND_FILTER").trimmed().toLower();
    if (mode == QLatin1String("ipv4-udp-host-only") ||
        mode == QLatin1String("no-ipv6") ||
        mode == QLatin1String("no-tcp")) {
        return mode;
    }
    return QString();
}

// 实时字幕 FunASR WebSocket 地址：默认本机占位，部署时通过 XIAOFU_ASR_URL 覆盖。
QString asrUrlFromEnv() {
    return qEnvironmentVariable("XIAOFU_ASR_URL",
                                QStringLiteral("ws://127.0.0.1:10095"));
}

QString featureName(QWebEnginePage::Feature feature) {
    switch (feature) {
    case QWebEnginePage::Notifications:
        return QStringLiteral("Notifications");
    case QWebEnginePage::Geolocation:
        return QStringLiteral("Geolocation");
    case QWebEnginePage::MediaAudioCapture:
        return QStringLiteral("MediaAudioCapture");
    case QWebEnginePage::MediaVideoCapture:
        return QStringLiteral("MediaVideoCapture");
    case QWebEnginePage::MediaAudioVideoCapture:
        return QStringLiteral("MediaAudioVideoCapture");
    case QWebEnginePage::MouseLock:
        return QStringLiteral("MouseLock");
    case QWebEnginePage::DesktopVideoCapture:
        return QStringLiteral("DesktopVideoCapture");
    case QWebEnginePage::DesktopAudioVideoCapture:
        return QStringLiteral("DesktopAudioVideoCapture");
    default:
        return QStringLiteral("Feature") + QString::number(static_cast<int>(feature));
    }
}

class ConsoleLogWebPage : public QWebEnginePage {
public:
    explicit ConsoleLogWebPage(QObject* parent = nullptr)
        : QWebEnginePage(parent) {}

protected:
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level,
                                  const QString& message,
                                  int lineNumber,
                                  const QString& sourceID) override {
        const bool tagged = message.startsWith(QLatin1Char('['))
                            && message.indexOf(QLatin1Char(']')) > 1;
        if (tagged) {
            qDebug().noquote() << "[WebConsole]" << sourceID << ":" << lineNumber
                               << "-" << message;
        } else if (level == ErrorMessageLevel) {
            qWarning().noquote() << "[WebConsole][error]" << sourceID << ":" << lineNumber
                                 << "-" << message;
        } else if (level == WarningMessageLevel) {
            qWarning().noquote() << "[WebConsole][warn]" << message;
        }
        QWebEnginePage::javaScriptConsoleMessage(level, message, lineNumber, sourceID);
    }
};
}

CallWidget::CallWidget(QWidget* parent)
    : QWidget(parent), ui(new Ui::CallWidget) {
    ui->setupUi(this);
    setAttribute(Qt::WA_StyledBackground, true);

    ui->callStack->setCurrentIndex(0);


    // “我的视频”脱离布局后作为悬浮控件，位置由拖拽逻辑独立管理。
    ui->pipRow->removeWidget(ui->btnPip);
    ui->btnPip->setParent(ui->viewIncall);
    ui->btnPip->installEventFilter(this);
    ui->btnPip->show();
    ui->btnPip->raise();
    resetPipPosition();
    setupWebRtcView();
    scanCameraDevices();
}

CallWidget::~CallWidget() {
    delete ui;
}

void CallWidget::setNetworkManager(NetworkManager* manager) {
    networkManager = manager;
    if (!networkManager || !callController)
        return;

    connect(networkManager, &NetworkManager::callSignalReceived, this, [this](const QVariantMap& signal) {
        const QString type = signal.value(QStringLiteral("type")).toString();
        const QString peerName = signal.value(QStringLiteral("from")).toString();
        qDebug().noquote() << "[Call] signal received:" << type << "from=" << peerName;
        if (type == QStringLiteral("call_request")) {
            callController->receiveIncomingCall(peerName);
            if (callController->state() == VideoCallController::IncomingRinging) {
                emit incomingCallRequested();
                showIncomingCallDialog(peerName);
            }
            return;
        }
        callController->handleRemoteSignal(signal);
    });
    connect(networkManager, &NetworkManager::callSignalSendResult, this,
            [this](const QString& signalType, int code, const QString& message) {
        if (code != 0 && callController->state() != VideoCallController::Idle) {
            qDebug() << "[Call] signal send failed:" << signalType << message;
            QString hint;
            if (signalType == QStringLiteral("call_request"))
                hint = QStringLiteral("对方不在线或无法接通，请稍后再试");
            else if (signalType == QStringLiteral("call_accept"))
                hint = QStringLiteral("对方已挂断或通话已结束，请稍后再试");
            else
                hint = QStringLiteral("信令发送失败：%1").arg(message);
            showCallErrorAndBack(hint);
        }
    });
    // 对端拒绝/挂断时，本地应回到主页面并关闭来电弹窗。
    connect(callController, &VideoCallController::callEndedByPeer, this, [this]() {
        emit backToMainWidget();
    });
    // 呼叫超时：无人接听/来电无人处理，提示后恢复 idle 并返回主页面。
    connect(callController, &VideoCallController::callTimeout, this, [this]() {
        showCallErrorAndBack(QStringLiteral("无人接听/呼叫超时"));
    });
    // 异常断线：Server 连接断开时立即清理通话状态，避免永久 calling/busy。
    connect(networkManager, &NetworkManager::disconnected, this, [this]() {
        if (callController && callController->state() != VideoCallController::Idle) {
            qDebug().noquote() << "[Call] server disconnected during call, abort";
            callController->abortCall();
            emit backToMainWidget();
        }
    });
}

void CallWidget::startOutgoingCall(const QString& peerName) {
    if (!callController || peerName.isEmpty())
        return;
    callController->startOutgoingCall(peerName);
}

void CallWidget::showLocalPreviewTest() {
    if (!webRtcBridge)
        return;
    // 不发起通话，直接进入通话页并打开本地摄像头预览，用于区分“摄像头/渲染”与“WebRTC”问题。
    qDebug().noquote() << "[Call] local preview test mode (no call)";
    ui->callStack->setCurrentIndex(1);
    ui->avatarIncall->hide();
    ui->nameIncall->hide();
    ui->statusIncall->hide();
    ui->btnPip->hide();
    QTimer::singleShot(0, this, [this]() {

        updateWebRtcGeometry();
    });
    if (webRtcPageReady)
        webRtcBridge->startPreview();
}

void CallWidget::on_btnCancelCall_clicked() {
    callController->endCall();
    emit backToMainWidget();
}




void CallWidget::on_btnPip_clicked() {
    if (ignoreNextPipClick) {
        ignoreNextPipClick = false;
        return;
    }

    pipExpanded = !pipExpanded;
    ui->btnPip->setFixedSize(pipExpanded ? QSize(330, 450) : QSize(220, 300));
    ui->btnPip->setToolTip(pipExpanded ? QStringLiteral("还原我的视频")
                                        : QStringLiteral("放大我的视频"));
    clampPipPosition();
}

bool CallWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched != ui->btnPip)
        return QWidget::eventFilter(watched, event);

    if (event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            pipPressGlobalPosition = mouseEvent->globalPos();
            pipStartPosition = ui->btnPip->pos();
            pipDragging = false;
        }
    } else if (event->type() == QEvent::MouseMove) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->buttons() & Qt::LeftButton) {
            const QPoint movement = mouseEvent->globalPos() - pipPressGlobalPosition;
            if (!pipDragging && movement.manhattanLength() > kPipDragThreshold)
                pipDragging = true;
            if (pipDragging) {
                ui->btnPip->move(pipStartPosition + movement);
                clampPipPosition();
                return true;
            }
        }
    } else if (event->type() == QEvent::MouseButtonRelease) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton && pipDragging) {
            ignoreNextPipClick = true;
            pipDragging = false;
            // 清理未产生 clicked 的拖拽状态，避免吞掉下一次正常单击。
            QTimer::singleShot(0, this, [this]() {
                ignoreNextPipClick = false;
            });
        }
    }

    return QWidget::eventFilter(watched, event);
}

void CallWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    clampPipPosition();
    updateWebRtcGeometry();
}

void CallWidget::clampPipPosition() {
    if (!ui || !ui->viewIncall || !ui->btnPip)
        return;

    const int maximumX = qMax(kPipMargin,
                              ui->viewIncall->width() - ui->btnPip->width() - kPipMargin);
    const int maximumY = qMax(kPipMargin,
                              ui->viewIncall->height() - ui->btnPip->height() - kPipMargin);
    const int boundedX = qBound(kPipMargin, ui->btnPip->x(), maximumX);
    const int boundedY = qBound(kPipMargin, ui->btnPip->y(), maximumY);
    ui->btnPip->move(boundedX, boundedY);
}

void CallWidget::resetPipPosition() {
    const int defaultX = ui->viewIncall->width() - ui->btnPip->width()
                         - kPipDefaultRightMargin;
    ui->btnPip->move(defaultX, kPipDefaultTopMargin);
    clampPipPosition();
    ui->btnPip->raise();
}






void CallWidget::on_btnMin_clicked() {
    window()->showMinimized();
}

void CallWidget::on_btnMax_clicked() {
    if (window()->isMaximized())
        window()->showNormal();
    else
        window()->showMaximized();
}

void CallWidget::on_btnClose_clicked() {
    window()->close();
}


void CallWidget::setupWebRtcView() {
    webRtcView = new QWebEngineView(ui->viewIncall);
    // Qt 5.12 兼容：用子类页面把 JS console 日志转发到 Qt 输出（5.12 中不是信号）。
    webRtcView->setPage(new ConsoleLogWebPage(webRtcView));
    webRtcView->settings()->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    webRtcView->settings()->setAttribute(QWebEngineSettings::ScreenCaptureEnabled, true);
#endif
    webRtcView->setAttribute(Qt::WA_StyledBackground, true);
    webRtcView->setStyleSheet(QStringLiteral("background:#1A1A2E; border:1px solid #6B6B6B;"));
    webRtcView->lower();
    ui->btnPip->hide();

    webChannel = new QWebChannel(webRtcView->page());
    webRtcBridge = new WebRtcBridge(this);
    webRtcBridge->setPage(webRtcView->page());
    webRtcBridge->setIceServers(buildIceServers());
    const QString icePolicy = iceTransportPolicyFromEnv();
    qDebug().noquote() << "[Call] XIAOFU_ICE_POLICY ="
                       << (icePolicy.isEmpty() ? QStringLiteral("(default all)") : icePolicy);
    webRtcBridge->setIcePolicy(icePolicy);
    const QString diagFilter = diagCandidateFilterFromEnv();
    qDebug().noquote() << "[Call] XIAOFU_DIAG_CAND_FILTER ="
                       << (diagFilter.isEmpty() ? QStringLiteral("(none, default)") : diagFilter);
    webRtcBridge->setDiagFilter(diagFilter);
    const bool cameraProbe = qEnvironmentVariableIntValue("XIAOFU_CAMERA_PROBE") == 1;
    qDebug().noquote() << "[Call] XIAOFU_CAMERA_PROBE =" << (cameraProbe ? 1 : 0);
    webRtcBridge->setCameraProbeEnabled(cameraProbe);
    const QString asrUrl = asrUrlFromEnv();
    qDebug().noquote() << "[Call] XIAOFU_ASR_URL =" << asrUrl;
    webRtcBridge->setSubtitleUrl(asrUrl);
    webChannel->registerObject(QStringLiteral("webRtcBridge"), webRtcBridge);
    webRtcView->page()->setWebChannel(webChannel);

    callController = new VideoCallController(this);
    callController->setBridge(webRtcBridge);
    connect(callController, &VideoCallController::signalReadyToSend,
            this, [this](const QVariantMap& signal) {
        if (networkManager)
            networkManager->sendCallSignal(signal);
    });
    connect(callController, &VideoCallController::stateChanged,
            this, [this](VideoCallController::CallState) { updateCallPageForState(); });
    connect(webRtcBridge, &WebRtcBridge::callError, this, [this](const QString& message) {
        qDebug() << "[Call] WebRTC error:" << message;
        showCallErrorAndBack(message);
    });
    connect(webRtcBridge, &WebRtcBridge::hangupRequested, this, [this]() {
        if (callController)
            callController->endCall();
        if (window()->isFullScreen())
            window()->showNormal();
        emit backToMainWidget();
    });

    connect(webRtcView, &QWebEngineView::loadFinished, this, [this](bool loaded) {
        webRtcPageReady = loaded;
        qDebug().noquote() << "[WebPage] loadFinished=" << (loaded ? QStringLiteral("true") : QStringLiteral("false"))
                           << "url=" << webRtcView->url().toString();
        if (loaded) {
            qDebug().noquote() << "[WebPage] UserAgent="
                               << QWebEngineProfile::defaultProfile()->httpUserAgent();
            qDebug().noquote() << "[WebPage] QTWEBENGINE_CHROMIUM_FLAGS="
                               << QString::fromUtf8(qgetenv("QTWEBENGINE_CHROMIUM_FLAGS"));
        }
        if (!loaded)
            return;
        // 页面脚本就绪后再注入 ICE 配置，保证创建 RTCPeerConnection 前已生效。
        webRtcBridge->applyIceServers();
        webRtcBridge->applyIcePolicy();
        webRtcBridge->applyDiagFilter();
        webRtcBridge->applyCameraProbeEnabled();
        webRtcBridge->applySubtitleUrl();
        if (ui->callStack->currentIndex() == 1)
            webRtcBridge->startPreview();
    });
    connect(webRtcView->page(), &QWebEnginePage::featurePermissionRequested,
            this, [this](const QUrl& origin, QWebEnginePage::Feature feature) {
        const bool videoGranted = feature == QWebEnginePage::MediaVideoCapture
                                 || feature == QWebEnginePage::MediaAudioVideoCapture;
        const bool audioGranted = feature == QWebEnginePage::MediaAudioCapture
                                  || feature == QWebEnginePage::MediaAudioVideoCapture;
        const auto permission = (videoGranted || audioGranted)
                                    ? QWebEnginePage::PermissionGrantedByUser
                                    : QWebEnginePage::PermissionDeniedByUser;
        qDebug().noquote() << "[WebPermission] origin=" << origin.toString()
                           << "feature=" << featureName(feature)
                           << "featureValue=" << static_cast<int>(feature)
                           << "granted=" << (permission == QWebEnginePage::PermissionGrantedByUser);
        webRtcView->page()->setFeaturePermission(origin, feature, permission);
    });
    connect(webRtcView->page(), &QWebEnginePage::fullScreenRequested, this,
            [this](QWebEngineFullScreenRequest request) {
        qDebug().noquote() << "[WebPage] FULLSCREEN_REQUEST toggleOn=" << (request.toggleOn() ? QStringLiteral("true") : QStringLiteral("false"));
        request.accept();
        if (request.toggleOn()) {
            window()->showFullScreen();
        } else {
            window()->showNormal();
        }
        QTimer::singleShot(0, this, [this]() {
            updateWebRtcGeometry();
            qDebug().noquote() << "[WebPage] FULLSCREEN_RESIZE view="
                               << (webRtcView ? QStringLiteral("%1x%2").arg(webRtcView->width()).arg(webRtcView->height())
                                                : QStringLiteral("null"))
                               << "container="
                               << (ui && ui->viewIncall ? QStringLiteral("%1x%2").arg(ui->viewIncall->width()).arg(ui->viewIncall->height())
                                                        : QStringLiteral("null"));
        });
    });

    updateWebRtcGeometry();
    connect(QWebEngineProfile::defaultProfile(), &QWebEngineProfile::downloadRequested,
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            this, [this](QWebEngineDownloadRequest* download) {
#else
            this, [this](QWebEngineDownloadItem* download) {
#endif
        static int s_downloadSeq = 0;
        QString name = download->url().fileName();
        if (name.isEmpty()) {
            name = QStringLiteral("vcall-recording-%1.webm").arg(++s_downloadSeq);
        }
        qDebug().noquote() << "[WebPage] DOWNLOAD_REQUESTED file=" << name;
        QString path = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (path.isEmpty()) path = QDir::homePath();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        download->setDownloadDirectory(path);
        download->setDownloadFileName(name);
#else
        download->setPath(path + QDir::separator() + name);
#endif
        download->accept();
    });
    webRtcView->setUrl(QUrl(QStringLiteral("qrc:///video/video_call.html")));
}

void CallWidget::scanCameraDevices() {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    const QList<QCameraInfo> cameras = QCameraInfo::availableCameras();
    qDebug().noquote() << "[CameraDevice] COUNT count=" << cameras.size();
    int index = 0;
    for (const QCameraInfo& camera : cameras) {
        qDebug().noquote() << "[CameraDevice] DEVICE index=" << index
                           << "description=" << camera.description();
        ++index;
    }
#else
    qDebug().noquote() << "[CameraDevice] QT6_UNSUPPORTED";
#endif
}

void CallWidget::updateWebRtcGeometry() {
    if (!webRtcView || !ui || !ui->viewIncall)
        return;
    webRtcView->setGeometry(ui->viewIncall->rect());
    webRtcView->lower();
}

void CallWidget::showIncomingCallDialog(const QString& peerName) {
    auto* dialog = new QDialog(this);
    dialog->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog->setModal(false);
    dialog->setFixedSize(390, 232);
    dialog->setStyleSheet(
        "QDialog{background:#1A1A2E;border:1px solid #6B6B6B;border-radius:14px;}"
        "QLabel{color:#FFFFFF;background:transparent;}"
        "QPushButton{min-height:38px;border-radius:9px;padding:0 22px;font-size:13px;font-weight:600;}"
        "QPushButton#rejectButton{background:rgba(255,255,255,36);color:#FFFFFF;border:1px solid rgba(255,255,255,55);}"
        "QPushButton#acceptButton{background:#007AFF;color:#FFFFFF;border:none;}");

    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(12);
    auto* title = new QLabel(QStringLiteral("视频通话邀请"), dialog);
    title->setStyleSheet("font-size:17px;font-weight:700;");
    auto* description = new QLabel(QStringLiteral("%1 邀请你进行视频通话").arg(peerName), dialog);
    description->setStyleSheet("font-size:13px;color:rgba(255,255,255,180);");
    layout->addWidget(title);
    layout->addWidget(description);
    layout->addStretch();
    auto* buttonRow = new QHBoxLayout;
    auto* rejectButton = new QPushButton(QStringLiteral("拒绝"), dialog);
    rejectButton->setObjectName(QStringLiteral("rejectButton"));
    auto* acceptButton = new QPushButton(QStringLiteral("接听"), dialog);
    acceptButton->setObjectName(QStringLiteral("acceptButton"));
    buttonRow->addWidget(rejectButton);
    buttonRow->addWidget(acceptButton);
    layout->addLayout(buttonRow);

    connect(rejectButton, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(acceptButton, &QPushButton::clicked, dialog, &QDialog::accept);
    // 弹窗非阻塞，避免对方挂断后仍卡在模态框里；结果只在仍在响铃时生效。
    connect(dialog, &QDialog::rejected, this, [this]() {
        if (callController && callController->state() == VideoCallController::IncomingRinging)
            callController->rejectIncomingCall();
    });
    connect(dialog, &QDialog::accepted, this, [this]() {
        if (callController && callController->state() == VideoCallController::IncomingRinging)
            callController->acceptIncomingCall();
    });
    // 对方挂断/拒绝后自动关闭来电弹窗。
    connect(callController, &VideoCallController::stateChanged, dialog,
            [dialog](VideoCallController::CallState state) {
        if (state != VideoCallController::IncomingRinging)
            dialog->close();
    });
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}

void CallWidget::updateCallPageForState() {
    if (!callController)
        return;

    const VideoCallController::CallState state = callController->state();
    const QString peerName = callController->peer();
    const QString avatarText = peerName.left(1).toUpper();

    if (state == VideoCallController::OutgoingRinging) {
        ui->callStack->setCurrentIndex(0);
        ui->avatarRinging->setText(avatarText);
        ui->nameRinging->setText(peerName);
        ui->statusPill->setText(QStringLiteral("正在呼叫…"));
        ui->hintRinging->setText(QStringLiteral("等待对方接听…"));
        ui->btnCancelCall->show();
        return;
    }
    if (state == VideoCallController::IncomingRinging) {
        ui->callStack->setCurrentIndex(0);
        ui->avatarRinging->setText(avatarText);
        ui->nameRinging->setText(peerName);
        ui->statusPill->setText(QStringLiteral("视频通话邀请"));
        ui->hintRinging->setText(QStringLiteral("%1 邀请你进行视频通话").arg(peerName));
        ui->btnCancelCall->hide();
        return;
    }
    if (state == VideoCallController::Connecting || state == VideoCallController::InCall) {
        ui->callStack->setCurrentIndex(1);
        // QStackedWidget 切页后尺寸会在下一轮事件循环才完成，避免 WebEngine 保留初始 640×480 大小。
        QTimer::singleShot(0, this, [this]() {
            updateWebRtcGeometry();
        });
        ui->btnPip->hide();
        if (state == VideoCallController::Connecting) {
            // 连接阶段显示对方信息和状态，媒体画面就绪后隐藏。
            ui->avatarIncall->setText(avatarText);
            ui->nameIncall->setText(peerName);
            ui->statusIncall->setText(QStringLiteral("正在连接对方…"));
            ui->avatarIncall->show();
            ui->nameIncall->show();
            ui->statusIncall->show();
            ui->avatarIncall->raise();
            ui->nameIncall->raise();
            ui->statusIncall->raise();
        } else {
            ui->avatarIncall->hide();
            ui->nameIncall->hide();
            ui->statusIncall->hide();
        }
        if (webRtcPageReady)
            webRtcBridge->startPreview();
        return;
    }
    // Ending / Idle：页面返回由挂断按钮或 callEndedByPeer 负责。
}

void CallWidget::showCallErrorAndBack(const QString& message) {
    ui->callStack->setCurrentIndex(0);
    ui->statusPill->setText(QStringLiteral("通话失败"));
    ui->hintRinging->setText(message);
    ui->btnCancelCall->show();
    if (callController)
        callController->endCall();
    // 短暂展示错误原因后再回到主页面；期间发起新通话则不再自动返回。
    QTimer::singleShot(1800, this, [this]() {
        if (callController && callController->state() == VideoCallController::Idle)
            emit backToMainWidget();
    });
}


