#include "CallWidget.h"
#include "ui_CallWidget.h"
#include "network/NetworkManager.h"
#include <QEvent>
#include <QMouseEvent>
#include <QPoint>
#include <QResizeEvent>
#include <QStyle>
#include <QTimer>
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
}

CallWidget::~CallWidget() {
    delete ui;
}

void CallWidget::setNetworkManager(NetworkManager* manager) {
    networkManager = manager;
}

void CallWidget::startDemoCall() {
    ui->callStack->setCurrentIndex(0);
    ui->moreMenu->setVisible(false);
    pipExpanded = false;
    pipDragging = false;
    ignoreNextPipClick = false;
    ui->btnPip->setFixedSize(220, 300);
    const int currentSerial = ++demoCallSerial;

    // 静态演示：每次进入页面都先展示呼叫状态，再自动进入通话画面。
    // TODO: 后续收到对端 accept 信令后再切换到通话中页面并启动通话计时。
    QTimer::singleShot(1400, this, [this, currentSerial]() {
        if (currentSerial == demoCallSerial && ui->callStack->currentIndex() == 0) {
            ui->callStack->setCurrentIndex(1);
            resetPipPosition();
        }
    });
}

void CallWidget::on_btnCancelCall_clicked() {
    // TODO: 后续通过信令服务器通知对端取消呼叫并释放待建立的 RTP 会话。
    emit backToMainWidget();
}

void CallWidget::on_btnHangup_clicked() {
    // TODO: 后续通过信令服务器通知对端结束通话并释放 RTP 会话。
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
    // TODO: 后续调用 FFmpeg 音频采集模块启停麦克风轨道。
}

void CallWidget::on_btnCam_clicked() {
    cameraEnabled = !cameraEnabled;
    ui->btnCam->setProperty("active", cameraEnabled);
    ui->btnCam->setToolTip(cameraEnabled ? QStringLiteral("关闭摄像头")
                                          : QStringLiteral("打开摄像头"));
    ui->btnPip->setText(cameraEnabled ? QStringLiteral("我的视频")
                                      : QStringLiteral("摄像头已关闭"));
    refreshButtonStyle(ui->btnCam);
    // TODO: 后续调用 FFmpeg 摄像头采集模块启停视频轨道。
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
