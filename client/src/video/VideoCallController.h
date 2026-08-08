#pragma once

#include <QObject>
#include <QVariantMap>

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

    void startOutgoingCall(const QString& peer);
    void receiveIncomingCall(const QString& peer);
    void acceptIncomingCall();
    void rejectIncomingCall();
    void handleRemoteSignal(const QVariantMap& signal);
    void endCall();

signals:
    void stateChanged(VideoCallController::CallState state);
    void peerChanged(const QString& peer);
    void signalReadyToSend(const QVariantMap& signal);
    // 对端拒绝或挂断导致通话结束，UI 需要据此返回主页面。
    void callEndedByPeer();

private:
    void setState(CallState nextState);
    void finishCall();

    WebRtcBridge* bridge = nullptr;
    CallState currentState = Idle;
    QString currentPeer;
};
