#pragma once
#include "ClientConfig.h"
#include <QWidget>
#include <optional>

class NetworkManager;
class QEvent;
class QResizeEvent;
class QWebChannel;
class QWebEngineView;
class WebRtcBridge;
class VideoCallController;

namespace Ui {
class CallWidget;
}

class CallWidget : public QWidget {
    Q_OBJECT
public:
    explicit CallWidget(QWidget* parent = nullptr);
    ~CallWidget();

    void setNetworkManager(NetworkManager* manager);
    void setClientConfig(const ClientConfig& config);
    void startOutgoingCall(const QString& peerName);
    // 本地预览自测：不发起通话，直接进入通话页并打开摄像头（XIAOFU_PREVIEW_TEST=1）。
    void showLocalPreviewTest();

signals:
    void backToMainWidget();
    void incomingCallRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void on_btnCancelCall_clicked();
    void on_btnPip_clicked();
    void on_btnMin_clicked();
    void on_btnMax_clicked();
    void on_btnClose_clicked();

private:
    void clampPipPosition();
    void resetPipPosition();
    void setupWebRtcView();
    void scanCameraDevices();
    void updateWebRtcGeometry();
    void showIncomingCallDialog(const QString& peerName);
    void updateCallPageForState();
    void showCallErrorAndBack(const QString& message);
    void applyClientConfig();

    Ui::CallWidget* ui;
    NetworkManager* networkManager = nullptr;
    std::optional<ClientConfig> clientConfig;
    QWebEngineView* webRtcView = nullptr;
    QWebChannel* webChannel = nullptr;
    WebRtcBridge* webRtcBridge = nullptr;
    VideoCallController* callController = nullptr;
    bool webRtcPageReady = false;
    bool previewTestActive = false;
    bool pipExpanded = false;
    QPoint pipPressGlobalPosition;
    QPoint pipStartPosition;
    bool pipDragging = false;
    bool ignoreNextPipClick = false;
};
