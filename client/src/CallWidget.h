#pragma once
#include <QWidget>

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
    void startOutgoingCall(const QString& peerName);

signals:
    void backToMainWidget();
    void incomingCallRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void on_btnCancelCall_clicked();
    void on_btnHangup_clicked();
    void on_btnMic_clicked();
    void on_btnCam_clicked();
    void on_btnPip_clicked();
    void on_btnMore_clicked();
    void on_btnShare_clicked();
    void on_btnRecord_clicked();
    void on_btnSpeaker_clicked();
    void on_btnFullscreen_clicked();
    void on_btnMin_clicked();
    void on_btnMax_clicked();
    void on_btnClose_clicked();

private:
    void refreshButtonStyle(QWidget* widget);
    void clampPipPosition();
    void resetPipPosition();
    void setupWebRtcView();
    void updateWebRtcGeometry();
    void showIncomingCallDialog(const QString& peerName);
    void updateCallPageForState();

    Ui::CallWidget* ui;
    NetworkManager* networkManager = nullptr;
    QWebEngineView* webRtcView = nullptr;
    QWebChannel* webChannel = nullptr;
    WebRtcBridge* webRtcBridge = nullptr;
    VideoCallController* callController = nullptr;
    bool webRtcPageReady = false;
    bool microphoneEnabled = true;
    bool cameraEnabled = true;
    bool pipExpanded = false;
    bool sharingEnabled = false;
    bool recordingEnabled = false;
    bool speakerEnabled = true;
    bool fullScreenEnabled = false;
    QPoint pipPressGlobalPosition;
    QPoint pipStartPosition;
    bool pipDragging = false;
    bool ignoreNextPipClick = false;
};
