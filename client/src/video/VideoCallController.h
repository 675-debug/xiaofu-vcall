#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

class QTimer;
class WebRtcBridge;

class VideoCallController : public QObject {
    Q_OBJECT
public:
    enum CallState {
        Idle,
        OutgoingRinging,
        IncomingRinging,
        Connecting,
        InCall,
        Ending
    };
    Q_ENUM(CallState)

    explicit VideoCallController(QObject* parent = nullptr);

    void setBridge(WebRtcBridge* bridge);
    CallState state() const;
    QString peer() const;
    QString callId() const;

    void startOutgoingCall(const QString& peer);
    void receiveIncomingCall(const QString& peer, const QString& callId);
    void acceptIncomingCall();
    void rejectIncomingCall();
    void handleRemoteSignal(const QVariantMap& signal);
    void endCall();
    // 异常断线等场景：直接清理本地通话状态，不再发送任何信令。
    void abortCall();

signals:
    void stateChanged(VideoCallController::CallState state);
    void peerChanged(const QString& peer);
    void signalReadyToSend(const QVariantMap& signal);
    // 对端拒绝或挂断导致通话结束，UI 需要据此返回主页面。
    void callEndedByPeer();
    // 呼叫超时（无人接听 / 来电无人处理），UI 需要提示并返回主页面。
    void callTimeout();

private slots:
    void onCallTimeout();

private:
    void setState(CallState nextState);
    void finishCall();
    bool belongsToCurrentCall(const QVariantMap& signal) const;
    QString generateCallId();
    void startCallTimeout();
    void stopCallTimeout();

    WebRtcBridge* bridge = nullptr;
    CallState currentState = Idle;
    QString currentPeer;
    QString currentCallId;
    QTimer* callTimeoutTimer = nullptr;
};
