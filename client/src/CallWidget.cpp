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

#include "RecordingPaths.h"

#include <QCoreApplication>

#include <QDateTime>

#include <QFile>

#include <QFileInfo>

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



// 构建 WebRTC ICE 服务器列表：默认 STUN+TURN 同时注入（标准 ICE：P2P/STUN 优先，TURN 兜底），

// 环境变量可覆盖。真实 TURN 凭据只允许通过环境变量注入，禁止把历史默认凭据提交到代码仓库。

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

        const QString username = qEnvironmentVariable("XIAOFU_TURN_USERNAME").trimmed();

        const QString credential = qEnvironmentVariable("XIAOFU_TURN_CREDENTIAL").trimmed();

        // turn:/turns: 协议必须同时提供 username/credential，否则 TURN allocation 会失败。

        // 没有有效凭据时跳过 TURN，STUN/P2P 仍可尝试；不静默使用历史凭据。

        const bool turnNeedsAuth = turnUrl.startsWith(QStringLiteral("turn:"))

                                   || turnUrl.startsWith(QStringLiteral("turns:"));

        if (turnNeedsAuth && (username.isEmpty() || credential.isEmpty())) {

            qWarning().noquote() << "[Call] TURN fallback unavailable reason=credential missing, STUN/P2P only";

            return servers;

        }

        QVariantMap turn;

        turn.insert(QStringLiteral("urls"), turnUrl);

        if (!username.isEmpty())

            turn.insert(QStringLiteral("username"), username);

        if (!credential.isEmpty())

            turn.insert(QStringLiteral("credential"), credential);

        servers.append(turn);

    }

    return servers;

}



// 读取 ICE 传输策略环境变量：XIAOFU_ICE_POLICY=relay 时强制所有媒体走 TURN 中继，

// 用于隔离“直连路径(NAT/内网)坏、TURN 中继好”的场景；其他值返回 "all"(标准 ICE：P2P/STUN 优先，TURN 兜底)。

QString iceTransportPolicyFromEnv() {

    const QString policy = qEnvironmentVariable("XIAOFU_ICE_POLICY").trimmed().toLower();

    if (policy == QLatin1String("relay"))

        return QStringLiteral("relay");

    return QStringLiteral("all");

}



// 实时字幕 FunASR WebSocket 地址（统一运行配置，部署地址只在这里维护一份）：

//   1) 环境变量 XIAOFU_ASR_URL（开发/诊断 override，例如 ws://127.0.0.1:10095）

//   2) exe 同目录 asr.url 配置文件（内容为完整 ws:// 地址，正常打包分发时写入）

//   3) 两者都未提供时返回空地址：不连接 WebSocket，UI 提示“字幕服务未配置”。

// 地址由 C++ 注入 subtitle.js，subtitle.js 不再维护任何兜底地址。

struct AsrUrlConfig {

    QString url;

    QString source;  // env / config / none

};



AsrUrlConfig resolveAsrUrl() {

    // 1) 环境变量优先（开发/诊断 override）

    const QByteArray envUrl = qgetenv("XIAOFU_ASR_URL");

    if (!envUrl.isEmpty()) {

        const QString url = QString::fromUtf8(envUrl).trimmed();

        if (!url.isEmpty())

            return {url, QStringLiteral("env")};

    }

    // 2) exe 同目录 asr.url 配置文件（正常打包分发）

    QFile configFile(QCoreApplication::applicationDirPath() + QStringLiteral("/asr.url"));

    if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {

        const QString url = QString::fromUtf8(configFile.readAll()).trimmed();

        if (!url.isEmpty())

            return {url, QStringLiteral("config")};

    }

    // 3) 未配置：返回空地址，不连接

    return {QString(), QStringLiteral("none")};

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



// 解析录屏最终保存目录：优先用户配置（QSettings recording/saveDirectory），

// 目录不存在自动创建；创建失败/不可写时回退默认目录并记录原因，避免客户端崩溃或录屏丢失。

QString resolveRecordingDirectory() {

    QString dir = RecordingPaths::configuredOrDefault();

    const QFileInfo info(dir);

    if (info.exists()) {

        if (info.isWritable())

            return dir;

        qWarning().noquote() << "[Recorder] SAVE_FALLBACK reason=not_writable dir=" << dir;

    } else {

        if (QDir().mkpath(dir))

            return dir;

        qWarning().noquote() << "[Recorder] SAVE_FALLBACK reason=mkpath_failed dir=" << dir;

    }

    dir = RecordingPaths::defaultDirectory();

    QDir().mkpath(dir);

    return dir;

}

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

        const QString callId = signal.value(QStringLiteral("callId")).toString();

        qDebug().noquote() << "[Call] signal received:" << type << "from=" << peerName;

        if (type == QStringLiteral("call_request")) {

            callController->receiveIncomingCall(peerName, callId);

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

    qDebug().noquote() << "[Call] XIAOFU_ICE_POLICY =" << icePolicy;

    webRtcBridge->setIcePolicy(icePolicy);

    const AsrUrlConfig asrUrl = resolveAsrUrl();

    qDebug().noquote() << "[Call] ASR_URL_SOURCE =" << asrUrl.source;

    qDebug().noquote() << "[Call] ASR_URL =" << asrUrl.url;

    if (asrUrl.source == QStringLiteral("env"))

        qDebug().noquote() << "[Call] ASR_ENV_OVERRIDE";

    else if (asrUrl.source == QStringLiteral("config"))

        qDebug().noquote() << "[Call] ASR_CONFIG_FALLBACK";

    else

        qDebug().noquote() << "[Call] ASR_NO_CONFIG";

    webRtcBridge->setSubtitleUrl(asrUrl.url);

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

        QString fileName = download->url().fileName();

        const bool isRecording = fileName.endsWith(QStringLiteral(".webm"), Qt::CaseInsensitive);

        QString dir;

        if (isRecording) {

            dir = resolveRecordingDirectory();

            const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));

            QString unique = QStringLiteral("xiaofu-vcall-%1.webm").arg(ts);

            int seq = 2;

            while (QFileInfo::exists(dir + QDir::separator() + unique)) {

                unique = QStringLiteral("xiaofu-vcall-%1-%2.webm").arg(ts).arg(seq);

                ++seq;

            }

            fileName = unique;

        } else {

            if (fileName.isEmpty())

                fileName = QStringLiteral("download");

            dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);

            if (dir.isEmpty())

                dir = QDir::homePath();

        }

        qDebug().noquote() << "[Recorder] SAVE_DIRECTORY" << dir;

        qDebug().noquote() << "[Recorder] SAVE_FILE" << fileName;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)

        download->setDownloadDirectory(dir);

        download->setDownloadFileName(fileName);

#else

        download->setPath(dir + QDir::separator() + fileName);

#endif

        download->accept();

        if (isRecording && webRtcBridge) {

            webRtcBridge->showToast(QStringLiteral("录屏已保存至：") + dir);

        }

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
        if (callController && callController->state() == VideoCallController::IncomingRinging) {
            callController->rejectIncomingCall();
            // 本地立即结束响铃状态并返回主页面，不等待对端响应；
            // rejectIncomingCall 已同步将状态置为 Idle，dialog 关闭触发的二次
            // rejected 会被上面的状态守卫拦截，不会重复发送 reject/hangup。
            emit backToMainWidget();
        }
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
