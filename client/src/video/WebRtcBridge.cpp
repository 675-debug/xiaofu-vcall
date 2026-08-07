#include "WebRtcBridge.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QWebEnginePage>

WebRtcBridge::WebRtcBridge(QObject* parent)
    : QObject(parent) {
}

void WebRtcBridge::setPage(QWebEnginePage* webPage) {
    page = webPage;
}

void WebRtcBridge::startPreview() {
    runPageScript(QStringLiteral("window.xiaofuWebRtc && window.xiaofuWebRtc.startPreview();"));
}

void WebRtcBridge::startOutgoingCall() {
    runPageScript(QStringLiteral("window.xiaofuWebRtc && window.xiaofuWebRtc.startOutgoingCall();"));
}

void WebRtcBridge::acceptIncomingCall() {
    runPageScript(QStringLiteral("window.xiaofuWebRtc && window.xiaofuWebRtc.acceptIncomingCall();"));
}

void WebRtcBridge::setCameraEnabled(bool enabled) {
    runPageScript(QStringLiteral("window.xiaofuWebRtc && window.xiaofuWebRtc.setCameraEnabled(%1);")
                      .arg(enabled ? QStringLiteral("true") : QStringLiteral("false")));
}

void WebRtcBridge::stopCall() {
    runPageScript(QStringLiteral("window.xiaofuWebRtc && window.xiaofuWebRtc.stopCall();"));
}

void WebRtcBridge::applyRemoteSignal(const QVariantMap& signal) {
    const QJsonDocument document(QJsonObject::fromVariantMap(signal));
    const QString signalJson = QString::fromUtf8(document.toJson(QJsonDocument::Compact));
    runPageScript(QStringLiteral("window.xiaofuWebRtc && window.xiaofuWebRtc.applyRemoteSignal(%1);")
                      .arg(signalJson));
}

void WebRtcBridge::reportPreviewReady(const QVariantMap& settings) {
    emit previewReady(settings);
}

void WebRtcBridge::reportCallError(const QString& message) {
    emit callError(message);
}

void WebRtcBridge::reportOutgoingSignal(const QVariantMap& signal) {
    emit outgoingSignal(signal);
}

void WebRtcBridge::reportCallState(const QString& state) {
    emit callStateChanged(state);
}

void WebRtcBridge::runPageScript(const QString& script) {
    if (page)
        page->runJavaScript(script);
}
