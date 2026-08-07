#include "CallWidget.h"
#include "ui_CallWidget.h"
#include "network/NetworkManager.h"
#include "video/VideoCallController.h"
#include "video/WebRtcBridge.h"
#include <QEvent>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPoint>
#include <QResizeEvent>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <QtGlobal>

namespace {
const int kPipDragThreshold = 6;
const int kPipMargin = 12;
const int kPipDefaultRightMargin = 26;
const int kPipDefaultTopMargin = 16;
}

CallWidget::CallWidget(QWidget* parent)
    : QWidget(parent), ui(new Ui::CallWidget) {
    ui->setupUi(this);
    setAttribute(Qt::WA_StyledBackground, true);

    ui->moreMenu->setVisible(false);
    ui->callStack->setCurrentIndex(0);

    ui->btnMic->setProperty("active", microphoneEnabled);
    ui->btnCam->setProperty("active", cameraEnabled);
    ui->btnSpeaker->setProperty("active", speakerEnabled);

    // “我的视频”脱离布局后作为悬浮控件，位置由拖拽逻辑独立管理。
    ui->pipRow->removeWidget(ui->btnPip);
    ui->btnPip->setParent(ui->viewIncall);
    ui->btnPip->installEventFilter(this);
    ui->btnPip->show();
    ui->btnPip->raise();
    resetPipPosition();
    setupWebRtcView();
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
            callController->endCall();
            emit backToMainWidget();
        }
    });
}

void CallWidget::startOutgoingCall(const QString& peerName) {
    if (!callController || peerName.isEmpty())
        return;

    ui->avatarRinging->setText(peerName.left(1).toUpper());
    ui->nameRinging->setText(peerName);
    ui->statusPill->setText(QStringLiteral("正在呼叫…"));
    ui->hintRinging->setText(QStringLiteral("等待对方接听…"));
    ui->callStack->setCurrentIndex(0);
    callController->startOutgoingCall(peerName);
}

void CallWidget::on_btnCancelCall_clicked() {
    callController->endCall();
    emit backToMainWidget();
}

void CallWidget::on_btnHangup_clicked() {
    callController->endCall();
    if (window()->isFullScreen())
        window()->showNormal();
    emit backToMainWidget();
}

void CallWidget::on_btnMic_clicked() {
    microphoneEnabled = !microphoneEnabled;
    ui->btnMic->setProperty("active", microphoneEnabled);
    ui->btnMic->setToolTip(microphoneEnabled ? QStringLiteral("关闭麦克风")
                                              : QStringLiteral("打开麦克风"));
    refreshButtonStyle(ui->btnMic);
    // TODO: 后续接入 WebRTC 音频轨道后，在这里切换麦克风状态。
}

void CallWidget::on_btnCam_clicked() {
    cameraEnabled = !cameraEnabled;
    ui->btnCam->setProperty("active", cameraEnabled);
    ui->btnCam->setToolTip(cameraEnabled ? QStringLiteral("关闭摄像头")
                                          : QStringLiteral("打开摄像头"));
    refreshButtonStyle(ui->btnCam);
    webRtcBridge->setCameraEnabled(cameraEnabled);
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
    // TODO: 后续在本地视频控件中渲染 FFmpeg 解码后的摄像头画面。
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

void CallWidget::on_btnMore_clicked() {
    const bool visible = !ui->moreMenu->isVisible();
    if (visible) {
        const QPoint buttonTopLeft = ui->btnMore->mapTo(this, QPoint(0, 0));
        const int desiredX = buttonTopLeft.x() + ui->btnMore->width() / 2;
        const int desiredY = buttonTopLeft.y() - ui->moreMenu->height() - 26;
        const int boundedX = qBound(8, desiredX, width() - ui->moreMenu->width() - 8);
        const int boundedY = qBound(8, desiredY, height() - ui->moreMenu->height() - 8);
        ui->moreMenu->move(boundedX, boundedY);
    }
    ui->moreMenu->setVisible(visible);
    ui->btnMore->setProperty("active", visible);
    if (visible)
        ui->moreMenu->raise();
    refreshButtonStyle(ui->btnMore);
}

void CallWidget::on_btnShare_clicked() {
    sharingEnabled = !sharingEnabled;
    ui->btnShare->setProperty("active", sharingEnabled);
    ui->btnShare->setText(sharingEnabled ? QStringLiteral("停止共享屏幕")
                                          : QStringLiteral("共享屏幕"));
    refreshButtonStyle(ui->btnShare);
    // TODO: 后续接入桌面采集并将共享画面编码为独立视频轨道。
}

void CallWidget::on_btnRecord_clicked() {
    recordingEnabled = !recordingEnabled;
    ui->btnRecord->setProperty("active", recordingEnabled);
    ui->btnRecord->setText(recordingEnabled ? QStringLiteral("停止录制通话")
                                             : QStringLiteral("录制通话"));
    refreshButtonStyle(ui->btnRecord);
    // TODO: 后续向服务端发送录制开关并同步录像状态。
}

void CallWidget::on_btnSpeaker_clicked() {
    speakerEnabled = !speakerEnabled;
    ui->btnSpeaker->setProperty("active", speakerEnabled);
    ui->btnSpeaker->setText(speakerEnabled ? QStringLiteral("关闭扬声器")
                                            : QStringLiteral("打开扬声器"));
    refreshButtonStyle(ui->btnSpeaker);
    // TODO: 后续切换 FFmpeg 解码音频的输出设备或静音状态。
}

void CallWidget::on_btnFullscreen_clicked() {
    fullScreenEnabled = !fullScreenEnabled;
    ui->btnFullscreen->setText(fullScreenEnabled ? QStringLiteral("退出全屏")
                                                  : QStringLiteral("全屏"));
    ui->moreMenu->setVisible(false);
    ui->btnMore->setProperty("active", false);
    refreshButtonStyle(ui->btnMore);
    if (fullScreenEnabled)
        window()->showFullScreen();
    else
        window()->showNormal();
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

void CallWidget::refreshButtonStyle(QWidget* widget) {
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

void CallWidget::setupWebRtcView() {
    webRtcView = new QWebEngineView(ui->viewIncall);
    webRtcView->setAttribute(Qt::WA_StyledBackground, true);
    webRtcView->setStyleSheet(QStringLiteral("background:#1A1A2E; border:1px solid #6B6B6B;"));
    webRtcView->lower();
    ui->btnPip->hide();

    webChannel = new QWebChannel(webRtcView->page());
    webRtcBridge = new WebRtcBridge(this);
    webRtcBridge->setPage(webRtcView->page());
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
    connect(webRtcBridge, &WebRtcBridge::callError, this, [](const QString& message) {
        qDebug() << "[Call] WebRTC error:" << message;
    });

    connect(webRtcView, &QWebEngineView::loadFinished, this, [this](bool loaded) {
        webRtcPageReady = loaded;
        if (!loaded)
            return;
        if (ui->callStack->currentIndex() == 1)
            webRtcBridge->startPreview();
    });
    connect(webRtcView->page(), &QWebEnginePage::featurePermissionRequested,
            this, [this](const QUrl& origin, QWebEnginePage::Feature feature) {
        const auto permission = feature == QWebEnginePage::MediaVideoCapture
                                    ? QWebEnginePage::PermissionGrantedByUser
                                    : QWebEnginePage::PermissionDeniedByUser;
        // 只允许摄像头权限；音频功能留待后续独立实现。
        webRtcView->page()->setFeaturePermission(origin, feature, permission);
    });

    updateWebRtcGeometry();
    webRtcView->setUrl(QUrl(QStringLiteral("qrc:///video/video_call.html")));
}

void CallWidget::updateWebRtcGeometry() {
    if (!webRtcView || !ui || !ui->viewIncall)
        return;
    webRtcView->setGeometry(ui->viewIncall->rect());
    webRtcView->lower();
}

void CallWidget::showIncomingCallDialog(const QString& peerName) {
    QDialog dialog(this);
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setModal(true);
    dialog.setFixedSize(390, 232);
    dialog.setStyleSheet(
        "QDialog{background:#1A1A2E;border:1px solid #6B6B6B;border-radius:14px;}"
        "QLabel{color:#FFFFFF;background:transparent;}"
        "QPushButton{min-height:38px;border-radius:9px;padding:0 22px;font-size:13px;font-weight:600;}"
        "QPushButton#rejectButton{background:rgba(255,255,255,36);color:#FFFFFF;border:1px solid rgba(255,255,255,55);}"
        "QPushButton#acceptButton{background:#007AFF;color:#FFFFFF;border:none;}");

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(12);
    auto* title = new QLabel(QStringLiteral("视频通话邀请"), &dialog);
    title->setStyleSheet("font-size:17px;font-weight:700;");
    auto* description = new QLabel(QStringLiteral("%1 邀请你进行视频通话").arg(peerName), &dialog);
    description->setStyleSheet("font-size:13px;color:rgba(255,255,255,180);");
    layout->addWidget(title);
    layout->addWidget(description);
    layout->addStretch();
    auto* buttonRow = new QHBoxLayout;
    auto* rejectButton = new QPushButton(QStringLiteral("拒绝"), &dialog);
    rejectButton->setObjectName(QStringLiteral("rejectButton"));
    auto* acceptButton = new QPushButton(QStringLiteral("接听"), &dialog);
    acceptButton->setObjectName(QStringLiteral("acceptButton"));
    buttonRow->addWidget(rejectButton);
    buttonRow->addWidget(acceptButton);
    layout->addLayout(buttonRow);
    bool accepted = false;
    connect(rejectButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(acceptButton, &QPushButton::clicked, &dialog, [&dialog, &accepted]() {
        accepted = true;
        dialog.accept();
    });
    dialog.exec();
    if (accepted)
        callController->acceptIncomingCall();
    else
        callController->rejectIncomingCall();
}

void CallWidget::updateCallPageForState() {
    if (!callController)
        return;

    const VideoCallController::CallState state = callController->state();
    if (state == VideoCallController::Connecting || state == VideoCallController::InCall) {
        ui->callStack->setCurrentIndex(1);
        // QStackedWidget 切页后尺寸会在下一轮事件循环才完成，避免 WebEngine 保留初始 640×480 大小。
        QTimer::singleShot(0, this, [this]() {
            updateWebRtcGeometry();
        });
        ui->btnPip->hide();
        ui->avatarIncall->hide();
        ui->nameIncall->hide();
        ui->statusIncall->hide();
        if (webRtcPageReady)
            webRtcBridge->startPreview();
    }
}
