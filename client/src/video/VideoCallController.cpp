#include "VideoCallController.h"

#include "WebRtcBridge.h"

VideoCallController::VideoCallController(QObject* parent)
    : QObject(parent) {
}

void VideoCallController::setBridge(WebRtcBridge* webRtcBridge) {
    bridge = webRtcBridge;
    if (!bridge)
        return;

    connect(bridge, &WebRtcBridge::outgoingSignal, this, [this](QVariantMap signal) {
        // 浏览器只关心 WebRTC 内容；目标用户由 C++ 控制器统一补齐。
        if (currentPeer.isEmpty())
            return;
        signal.insert(QStringLiteral("to"), currentPeer);
        emit signalReadyToSend(signal);
    });
    connect(bridge, &WebRtcBridge::callStateChanged, this, [this](const QString& stateName) {
        if (stateName == QStringLiteral("connected"))
            setState(InCall);
        else if (stateName == QStringLiteral("closed") && currentState != Idle)
            finishCall();
    });
}

VideoCallController::CallState VideoCallController::state() const {
    return currentState;
}

QString VideoCallController::peer() const {
    return currentPeer;
}

void VideoCallController::startOutgoingCall(const QString& peerName) {
    if (peerName.isEmpty() || currentState != Idle)
        return;

    currentPeer = peerName;
    emit peerChanged(currentPeer);
    setState(OutgoingRinging);
    emit signalReadyToSend({{QStringLiteral("type"), QStringLiteral("call_request")},
                            {QStringLiteral("to"), currentPeer}});
}

void VideoCallController::receiveIncomingCall(const QString& peerName) {
    if (peerName.isEmpty() || currentState != Idle)
        return;

    currentPeer = peerName;
    emit peerChanged(currentPeer);
    setState(IncomingRinging);
}

void VideoCallController::acceptIncomingCall() {
    if (currentState != IncomingRinging)
        return;

    setState(Connecting);
    emit signalReadyToSend({{QStringLiteral("type"), QStringLiteral("call_accept")},
                            {QStringLiteral("to"), currentPeer}});
    if (bridge)
        bridge->acceptIncomingCall();
}

void VideoCallController::rejectIncomingCall() {
    if (currentState != IncomingRinging)
        return;

    emit signalReadyToSend({{QStringLiteral("type"), QStringLiteral("call_reject")},
                            {QStringLiteral("to"), currentPeer}});
    finishCall();
}

void VideoCallController::handleRemoteSignal(const QVariantMap& signal) {
    const QString type = signal.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("call_accept") && currentState == OutgoingRinging) {
        setState(Connecting);
        if (bridge)
            bridge->startOutgoingCall();
        return;
    }

    if (type == QStringLiteral("call_reject") || type == QStringLiteral("call_hangup")) {
        // 收到对端挂断必须关闭本地轨道，避免摄像头在回到聊天页后仍被占用。
        if (bridge)
            bridge->stopCall();
        finishCall();
        return;
    }

    if (bridge)
        bridge->applyRemoteSignal(signal);
}

void VideoCallController::endCall() {
    if (currentState == Idle)
        return;

    if (!currentPeer.isEmpty()) {
        emit signalReadyToSend({{QStringLiteral("type"), QStringLiteral("call_hangup")},
                                {QStringLiteral("to"), currentPeer}});
    }
    if (bridge)
        bridge->stopCall();
    finishCall();
}

void VideoCallController::setState(CallState nextState) {
    if (currentState == nextState)
        return;
    currentState = nextState;
    emit stateChanged(currentState);
}

void VideoCallController::finishCall() {
    if (currentState == Idle)
        return;

    setState(Ending);
    currentPeer.clear();
    emit peerChanged(currentPeer);
    setState(Idle);
}
