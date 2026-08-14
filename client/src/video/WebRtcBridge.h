#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class QWebEnginePage;

class WebRtcBridge : public QObject {
    Q_OBJECT
public:
    explicit WebRtcBridge(QObject* parent = nullptr);

    void setPage(QWebEnginePage* page);
    void startPreview();
    void startOutgoingCall();
    void acceptIncomingCall();
    void setCameraEnabled(bool enabled);
    void stopCall();
    void applyRemoteSignal(const QVariantMap& signal);
    // 配置 WebRTC ICE 服务器（STUN/TURN），必须在创建 RTCPeerConnection 之前注入。
    void setIceServers(const QVariantList& servers);
    // 页面加载完成后把 C++ 侧暂存的 ICE 配置推送给 JS。
    void applyIceServers();
    // 配置 ICE 传输策略：'relay' 强制所有媒体走 TURN 中继（诊断直连路径问题），'all'/空 使用标准 ICE（P2P/STUN 优先，TURN 兜底）。
    void setIcePolicy(const QString& policy);
    // 页面加载完成后把 C++ 侧暂存的 ICE 策略推送给 JS。
    void applyIcePolicy();
    // 实时字幕 FunASR WebSocket 服务地址，页面加载完成后注入 JS。
    void setSubtitleUrl(const QString& url);
    // 页面加载完成后把 C++ 侧暂存的字幕服务地址推送给 JS。
    void applySubtitleUrl();
    // R10：录屏 UI foundation 开关，仅驱动 JS 能力探测与基础状态，不真正采集/保存。
    void toggleRecording();
    // 页面内轻量提示（toast），用于录屏保存位置等非阻塞通知。
    void showToast(const QString& message);

public slots:
    void requestHangup();
    void reportPreviewReady(const QVariantMap& settings);
    void reportCallError(const QString& message);
    void reportOutgoingSignal(const QVariantMap& signal);
    void reportCallState(const QString& state);

signals:
    void previewReady(const QVariantMap& settings);
    void callError(const QString& message);
    void outgoingSignal(const QVariantMap& signal);
    void callStateChanged(const QString& state);
    void hangupRequested();

private:
    void runPageScript(const QString& script);

    QPointer<QWebEnginePage> page;
    QVariantList pendingIceServers;
    QString pendingIcePolicy;
    QString pendingSubtitleUrl;
};

