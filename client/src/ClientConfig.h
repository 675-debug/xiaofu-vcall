#pragma once

#include <QString>
#include <QtGlobal>

class ClientConfig {
public:
    ClientConfig();
    explicit ClientConfig(const QString& filePath);
    ClientConfig(const QString& runtimePath, const QString& examplePath);

    static QString defaultFilePath();
    static QString executableConfigPath(const QString& argv0);
    static bool ensureRuntimeConfig(const QString& runtimePath, const QString& examplePath);

    QString serverHost() const;
    quint16 serverPort() const;
    QString stunUrl() const;
    QString turnUrl() const;
    QString turnUsername() const;
    QString turnCredential() const;
    QString icePolicy() const;
    QString asrUrl() const;
    bool disableGpu() const;
    bool disableVideoDecode() const;
    QString sourcePath() const;
    QString initializationWarning() const;

private:
    QString serverHost_;
    quint16 serverPort_ = 9000;
    QString stunUrl_;
    QString turnUrl_;
    QString turnUsername_;
    QString turnCredential_;
    QString icePolicy_;
    QString asrUrl_;
    bool disableGpu_ = false;
    bool disableVideoDecode_ = false;
    QString sourcePath_;
    QString initializationWarning_;
};
