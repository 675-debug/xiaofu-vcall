#pragma once

#include <QObject>
#include <QPointer>
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

public slots:
    void reportPreviewReady(const QVariantMap& settings);
    void reportCallError(const QString& message);
    void reportOutgoingSignal(const QVariantMap& signal);
    void reportCallState(const QString& state);

signals:
    void previewReady(const QVariantMap& settings);
    void callError(const QString& message);
    void outgoingSignal(const QVariantMap& signal);
    void callStateChanged(const QString& state);

private:
    void runPageScript(const QString& script);

    QPointer<QWebEnginePage> page;
};
