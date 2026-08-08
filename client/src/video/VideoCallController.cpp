#include "VideoCallController.h"

#include <QDebug>
#include "WebRtcBridge.h"

namespace {
const char* callStateName(VideoCallController::CallState state) {
    switch (state) {
        case VideoCallController::Idle: return "Idle";
        case VideoCallController::OutgoingRinging: return "OutgoingRinging";
        case VideoCallController::IncomingRinging: return "IncomingRinging";
        case VideoCallController::Connecting: return "Connecting";
        case VideoCallController::InCall: return "InCall";
        case VideoCallController::Ending: return "Ending";
    }
    return "?";
}
}

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
        const QString type = signal.value(QStringLiteral("type")).toString();
        const QString sdp = signal.value(QStringLiteral("sdp")).toString();
        const QString candidate = signal.value(QStringLiteral("candidate")).toString();
        if (type == QStringLiteral("webrtc_offer") || type == QStringLiteral("webrtc_answer"))
            qDebug().noquote() << "[Call] offer/answer ready to send:" << type << "sdp=" << sdp.size() << "chars";
        else if (type == QStringLiteral("ice_candidate"))
            qDebug().noquote() << "[Call] ice candidate ready to send:" << candidate.left(120);
        else
            qDebug().noquote() << "[Call] signal ready to send:" << type;
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
    qDebug() << "[Call] request sent to" << currentPeer;
    emit signalReadyToSend({{QStringLiteral("type"), QStringLiteral("call_request")},
                            {QStringLiteral("to"), currentPeer}});
}

void VideoCallController::receiveIncomingCall(const QString& peerName) {
    if (peerName.isEmpty() || currentState != Idle)
        return;

    currentPeer = peerName;
    emit peerChanged(currentPeer);
    setState(IncomingRinging);
    qDebug() << "[Call] incoming call from" << currentPeer;
}

void VideoCallController::acceptIncomingCall() {
    if (currentState != IncomingRinging)
        return;

    setState(Connecting);
    qDebug() << "[Call] accept sent to" << currentPeer;
    emit signalReadyToSend({{QStringLiteral("type"), QStringLiteral("call_accept")},
                            {QStringLiteral("to"), currentPeer}});
    if (bridge)
        bridge->acceptIncomingCall();
}

void VideoCallController::rejectIncomingCall() {
    if (currentState != IncomingRinging)
        return;

    qDebug() << "[Call] reject sent to" << currentPeer;
    emit signalReadyToSend({{QStringLiteral("type"), QStringLiteral("call_reject")},
                            {QStringLiteral("to"), currentPeer}});
    finishCall();
}

void VideoCallController::handleRemoteSignal(const QVariantMap& signal) {
    const QString type = signal.value(QStringLiteral("type")).toString();
    const QString sdp = signal.value(QStringLiteral("sdp")).toString();
    const QString candidate = signal.value(QStringLiteral("candidate")).toString();
    const QString sessionId = signal.value(QStringLiteral("sessionId")).toString();
    const QString sessionTag = sessionId.isEmpty()
        ? QStringLiteral("session=none")
        : QStringLiteral("session=") + sessionId;
    // 生命周期守卫：Idle 状态下丢弃所有 WebRTC 信令，
    // 防止 hangup 后仍在途的 offer/answer/candidate 触发新的 RTCPeerConnection。
    if (currentState == Idle &&
        (type == QStringLiteral("webrtc_offer") ||
         type == QStringLiteral("webrtc_answer") ||
         type == QStringLiteral("ice_candidate") ||
         type == QStringLiteral("webrtc_ice"))) {
        qWarning().noquote() << "[Call] drop stale WebRTC signal in Idle state:" << type
                             << sessionTag;
        return;
    }
    if (!sdp.isEmpty())
        qDebug().noquote() << "[Call] signal received:" << type << "sdp=" << sdp.size() << "chars" << sessionTag;
    else if (!candidate.isEmpty())
        qDebug().noquote() << "[Call] signal received:" << type << "candidate=" << candidate.left(120) << sessionTag;
    else
        qDebug().noquote() << "[Call] signal received:" << type << sessionTag;
    if (type == QStringLiteral("call_accept") && currentState == OutgoingRinging) {
        setState(Connecting);
        qDebug() << "[Call] peer accepted, starting WebRTC";
        if (bridge)
            bridge->startOutgoingCall();
        return;
    }

    if (type == QStringLiteral("call_reject") || type == QStringLiteral("call_hangup")) {
        qDebug() << "[Call] call ended by peer:" << type;
        // 收到对端挂断必须关闭本地轨道，避免摄像头在回到聊天页后仍被占用。
        if (bridge)
            bridge->stopCall();
        finishCall();
        emit callEndedByPeer();
        return;
    }

    if (bridge)
        bridge->applyRemoteSignal(signal);
}

void VideoCallController::endCall() {
    if (currentState == Idle)
        return;

    if (!currentPeer.isEmpty()) {
        qDebug() << "[Call] hangup sent to" << currentPeer;
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
    qDebug().noquote() << "[Call] state ->" << callStateName(nextState);
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
