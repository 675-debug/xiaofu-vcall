#include "WebRtcBridge.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QDebug>
#include <QRegularExpression>
#include <QStringList>
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

void WebRtcBridge::requestHangup() {
    emit hangupRequested();
}

void WebRtcBridge::setCameraEnabled(bool enabled) {
    runPageScript(QStringLiteral("window.xiaofuWebRtc && window.xiaofuWebRtc.setCameraEnabled(%1);")
                      .arg(enabled ? QStringLiteral("true") : QStringLiteral("false")));
}

void WebRtcBridge::stopCall() {
    runPageScript(QStringLiteral("window.xiaofuWebRtc && window.xiaofuWebRtc.stopCall();"));
}

void WebRtcBridge::applyRemoteSignal(const QVariantMap& signal) {
    const QString type = signal.value(QStringLiteral("type")).toString();
    const QString sdp = signal.value(QStringLiteral("sdp")).toString();
    const QString candidate = signal.value(QStringLiteral("candidate")).toString();
    const QString sessionId = signal.value(QStringLiteral("sessionId")).toString();
    const QString sessionTag = sessionId.isEmpty()
        ? QStringLiteral("session=none")
        : QStringLiteral("session=") + sessionId;
    if (!sdp.isEmpty())
        qDebug().noquote() << "[Call] applyRemoteSignal:" << type << "sdp=" << sdp.size() << "chars" << sessionTag;
    else if (!candidate.isEmpty())
        qDebug().noquote() << "[Call] applyRemoteSignal:" << type << "candidate=" << candidate.left(120) << sessionTag;
    else
        qDebug().noquote() << "[Call] applyRemoteSignal:" << type << sessionTag;
    const QJsonDocument document(QJsonObject::fromVariantMap(signal));
    const QString signalJson = QString::fromUtf8(document.toJson(QJsonDocument::Compact));
    runPageScript(QStringLiteral("window.xiaofuWebRtc && window.xiaofuWebRtc.applyRemoteSignal(%1);")
                      .arg(signalJson));
}

void WebRtcBridge::setIceServers(const QVariantList& servers) {
    pendingIceServers = servers;
    QStringList iceUrls;
    for (const QVariant& serverVariant : servers) {
        const QString url = serverVariant.toMap().value(QStringLiteral("urls")).toString();
        if (!url.isEmpty())
            iceUrls << url;
    }
    qDebug().noquote() << "[Call] ice servers configured:" << iceUrls.join(QStringLiteral(" "));
    if (!page)
        return;
    const QJsonArray serverArray = QJsonArray::fromVariantList(servers);
    const QString serversJson = QString::fromUtf8(
        QJsonDocument(serverArray).toJson(QJsonDocument::Compact));
    runPageScript(QStringLiteral("window.xiaofuWebRtc && window.xiaofuWebRtc.setIceServers(%1);")
                      .arg(serversJson));
}

void WebRtcBridge::applyIceServers() {
    if (pendingIceServers.isEmpty())
        return;
    setIceServers(pendingIceServers);
}

void WebRtcBridge::setIcePolicy(const QString& policy) {
    pendingIcePolicy = policy;
    qDebug().noquote() << "[Call] ice policy:" << (policy.isEmpty() ? QStringLiteral("all (default)") : policy);
    if (!page)
        return;
    const QString policyJson = QStringLiteral("\"%1\"").arg(policy);
    runPageScript(QStringLiteral("window.xiaofuWebRtc && window.xiaofuWebRtc.setIcePolicy(%1);")
                      .arg(policyJson));
}

void WebRtcBridge::applyIcePolicy() {
    if (pendingIcePolicy.isEmpty())
        return;
    setIcePolicy(pendingIcePolicy);
}

void WebRtcBridge::setSubtitleUrl(const QString& url) {
    pendingSubtitleUrl = url;
    qDebug().noquote() << "[Call] subtitle ASR configured=" << !url.isEmpty();
    if (!page)
        return;
    const QByteArray arrayJson = QJsonDocument(QJsonArray{url}).toJson(QJsonDocument::Compact);
    const QString urlJson = QString::fromUtf8(arrayJson.mid(1, arrayJson.size() - 2));
    runPageScript(QStringLiteral("window.xiaofuWebRtc && window.xiaofuWebRtc.setSubtitleUrl(%1);")
                      .arg(urlJson));
}

void WebRtcBridge::applySubtitleUrl() {
    if (pendingSubtitleUrl.isEmpty())
        return;
    setSubtitleUrl(pendingSubtitleUrl);
}
void WebRtcBridge::toggleRecording() {
    runPageScript(QStringLiteral("window.xiaofuWebRtc && window.xiaofuWebRtc.recorderToggle();"));
}
void WebRtcBridge::showToast(const QString& message) {
    const QByteArray escaped = QJsonDocument::fromVariant(message).toJson(QJsonDocument::Compact);
    runPageScript(QStringLiteral("window.xiaofuWebRtc && window.xiaofuWebRtc.showToast(%1);")
                      .arg(QString::fromUtf8(escaped)));
}

void WebRtcBridge::reportPreviewReady(const QVariantMap& settings) {
    emit previewReady(settings);
}

void WebRtcBridge::reportCallError(const QString& message) {
    qWarning().noquote() << "[Call] reportCallError:" << message;
    emit callError(message);
}

void WebRtcBridge::reportOutgoingSignal(const QVariantMap& signal) {
    emit outgoingSignal(signal);
}

void WebRtcBridge::reportCallState(const QString& state) {
    qDebug().noquote() << "[Call] reportCallState:" << state;
    emit callStateChanged(state);
}

void WebRtcBridge::runPageScript(const QString& script) {
    if (!page)
        return;
    const QString lower = script.toLower();
    if (lower.contains(QStringLiteral("setsubtitleurl"))
        || lower.contains(QStringLiteral("credential"))
        || lower.contains(QStringLiteral("password"))
        || lower.contains(QStringLiteral("token"))
        || lower.contains(QStringLiteral("secret"))) {
        QString name = QStringLiteral("script");
        const QRegularExpression functionCall(QStringLiteral("window\\.xiaofuWebRtc\\.([A-Za-z0-9_]+)\\s*\\("));
        const QRegularExpressionMatch match = functionCall.match(script);
        if (match.hasMatch())
            name = match.captured(1);
        qDebug().noquote() << QStringLiteral("[Call] runScript: ") + name + QStringLiteral("(<redacted>)");
    } else {
        const QString compact = script.left(120).replace(QLatin1Char('\n'), QLatin1Char(' '));
        qDebug().noquote() << "[Call] runScript:" << compact << (script.size() > 120 ? "..." : "");
    }
    page->runJavaScript(script);
}

