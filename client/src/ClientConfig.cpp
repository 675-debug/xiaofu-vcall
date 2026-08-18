#include "ClientConfig.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QUrl>

#ifdef Q_OS_WIN
extern "C" __declspec(dllimport) unsigned long __stdcall GetModuleFileNameW(
    void* module, wchar_t* fileName, unsigned long size);
#endif

namespace {

QString configuredValue(const QSettings& settings, const char* environmentName,
                        const QString& iniKey, const QString& defaultValue = QString()) {
    if (qEnvironmentVariableIsSet(environmentName))
        return QString::fromLocal8Bit(qgetenv(environmentName)).trimmed();
    return settings.value(iniKey, defaultValue).toString().trimmed();
}

bool containsUnsafeUrlCharacter(const QString& value) {
    for (const QChar character : value) {
        if (character.isSpace() || character.unicode() < 0x20
            || character == QLatin1Char('"') || character == QLatin1Char('\'')
            || character == QLatin1Char('\\'))
            return true;
    }
    return false;
}

bool isWebSocketUrl(const QString& value) {
    if (value.isEmpty() || containsUnsafeUrlCharacter(value))
        return false;
    const QUrl url(value, QUrl::StrictMode);
    const QString scheme = url.scheme().toLower();
    return url.isValid() && (scheme == QLatin1String("ws") || scheme == QLatin1String("wss"))
           && !url.host().isEmpty() && !url.authority().isEmpty();
}

bool isIceUrl(const QString& value, const QString& firstScheme, const QString& secondScheme) {
    if (value.isEmpty() || containsUnsafeUrlCharacter(value))
        return false;
    const int separator = value.indexOf(QLatin1Char(':'));
    if (separator <= 0)
        return false;
    const QString scheme = value.left(separator).toLower();
    if (scheme != firstScheme && scheme != secondScheme)
        return false;
    const QString remainder = value.mid(separator + 1);
    if (remainder.isEmpty() || remainder.contains(QLatin1Char('/')))
        return false;
    const QUrl authorityUrl(scheme + QStringLiteral("://") + remainder, QUrl::StrictMode);
    return authorityUrl.isValid() && !authorityUrl.host().isEmpty()
           && !authorityUrl.authority().isEmpty();
}

bool configuredBool(const QSettings& settings, const char* environmentName,
                    const QString& iniKey) {
    const QString value = configuredValue(settings, environmentName, iniKey).toLower();
    if (value == QLatin1String("1") || value == QLatin1String("true")
        || value == QLatin1String("yes") || value == QLatin1String("on"))
        return true;
    if (!value.isEmpty() && value != QLatin1String("0") && value != QLatin1String("false")
        && value != QLatin1String("no") && value != QLatin1String("off"))
        qWarning().noquote() << "[Config] invalid boolean for" << iniKey << ", using false";
    return false;
}

} // namespace

ClientConfig::ClientConfig()
    : ClientConfig(defaultFilePath()) {}

ClientConfig::ClientConfig(const QString& filePath)
    : ClientConfig(filePath, QFileInfo(filePath).absoluteDir()
                                 .filePath(QStringLiteral("config.ini.example"))) {}

ClientConfig::ClientConfig(const QString& runtimePath, const QString& examplePath) {
    if (QFileInfo::exists(runtimePath)) {
        sourcePath_ = runtimePath;
    } else if (QFileInfo::exists(examplePath)) {
        sourcePath_ = examplePath;
        initializationWarning_ = QStringLiteral(
            "config.ini could not be initialized; using read-only config.ini.example");
    } else {
        sourcePath_ = runtimePath;
        initializationWarning_ = QStringLiteral(
            "config.ini and config.ini.example are unavailable; using built-in defaults");
    }
    const QSettings settings(sourcePath_, QSettings::IniFormat);

    serverHost_ = configuredValue(settings, "XIAOFU_SERVER_HOST",
                                  QStringLiteral("network/server_host"),
                                  QStringLiteral("8.137.152.134"));
    if (serverHost_.isEmpty())
        serverHost_ = QStringLiteral("8.137.152.134");

    bool portOk = false;
    const int port = configuredValue(settings, "XIAOFU_SERVER_PORT",
                                     QStringLiteral("network/server_port"),
                                     QStringLiteral("9000")).toInt(&portOk);
    if (portOk && port >= 1 && port <= 65535)
        serverPort_ = static_cast<quint16>(port);
    else
        qWarning().noquote() << "[Config] invalid server port, using 9000";

    stunUrl_ = configuredValue(settings, "XIAOFU_STUN_URL",
                               QStringLiteral("webrtc/stun_url"),
                               QStringLiteral("stun:8.137.152.134:3478"));
    if (!stunUrl_.isEmpty() && !isIceUrl(stunUrl_, QStringLiteral("stun"), QStringLiteral("stuns"))) {
        qWarning().noquote() << "[Config] invalid STUN URL, skipping STUN";
        stunUrl_.clear();
    }

    turnUrl_ = configuredValue(settings, "XIAOFU_TURN_URL",
                               QStringLiteral("webrtc/turn_url"),
                               QStringLiteral("turn:8.137.152.134:3478"));
    if (!turnUrl_.isEmpty() && !isIceUrl(turnUrl_, QStringLiteral("turn"), QStringLiteral("turns"))) {
        qWarning().noquote() << "[Config] invalid TURN URL, skipping TURN";
        turnUrl_.clear();
    }
    turnUsername_ = configuredValue(settings, "XIAOFU_TURN_USERNAME",
                                    QStringLiteral("webrtc/turn_username"));
    turnCredential_ = configuredValue(settings, "XIAOFU_TURN_CREDENTIAL",
                                      QStringLiteral("webrtc/turn_credential"));

    icePolicy_ = configuredValue(settings, "XIAOFU_ICE_POLICY",
                                 QStringLiteral("webrtc/ice_policy"),
                                 QStringLiteral("all")).toLower();
    if (icePolicy_ != QLatin1String("all") && icePolicy_ != QLatin1String("relay")) {
        qWarning().noquote() << "[Config] invalid ICE policy, using all";
        icePolicy_ = QStringLiteral("all");
    }

    asrUrl_ = configuredValue(settings, "XIAOFU_ASR_URL", QStringLiteral("subtitle/asr_url"));
    if (!asrUrl_.isEmpty() && !isWebSocketUrl(asrUrl_)) {
        qWarning().noquote() << "[Config] invalid ASR URL, subtitles disabled";
        asrUrl_.clear();
    }

    disableGpu_ = configuredBool(settings, "XIAOFU_DISABLE_GPU",
                                 QStringLiteral("performance/disable_gpu"));
    disableVideoDecode_ = configuredBool(settings, "XIAOFU_DISABLE_VIDEO_DECODE",
                                         QStringLiteral("performance/disable_video_decode"));
}

QString ClientConfig::defaultFilePath() {
    QString applicationDir = QCoreApplication::applicationDirPath();
    if (applicationDir.isEmpty())
        applicationDir = QDir::currentPath();
    return QDir(applicationDir).filePath(QStringLiteral("config.ini"));
}

QString ClientConfig::executableConfigPath(const QString& argv0) {
#ifdef Q_OS_WIN
    wchar_t executablePath[32768];
    const unsigned long length = GetModuleFileNameW(nullptr, executablePath, 32768);
    if (length > 0 && length < 32768) {
        return QFileInfo(QString::fromWCharArray(executablePath, static_cast<int>(length)))
            .absoluteDir().filePath(QStringLiteral("config.ini"));
    }
#endif
    return QFileInfo(argv0).absoluteDir().filePath(QStringLiteral("config.ini"));
}

bool ClientConfig::ensureRuntimeConfig(const QString& runtimePath, const QString& examplePath) {
    if (QFileInfo::exists(runtimePath))
        return true;
    if (!QFileInfo::exists(examplePath))
        return false;
    if (QFile::copy(examplePath, runtimePath))
        return true;
    // 另一进程可能已在 exists 检查之后完成初始化。
    if (QFileInfo::exists(runtimePath))
        return true;
    return false;
}

QString ClientConfig::serverHost() const { return serverHost_; }
quint16 ClientConfig::serverPort() const { return serverPort_; }
QString ClientConfig::stunUrl() const { return stunUrl_; }
QString ClientConfig::turnUrl() const { return turnUrl_; }
QString ClientConfig::turnUsername() const { return turnUsername_; }
QString ClientConfig::turnCredential() const { return turnCredential_; }
QString ClientConfig::icePolicy() const { return icePolicy_; }
QString ClientConfig::asrUrl() const { return asrUrl_; }
bool ClientConfig::disableGpu() const { return disableGpu_; }
bool ClientConfig::disableVideoDecode() const { return disableVideoDecode_; }
QString ClientConfig::sourcePath() const { return sourcePath_; }
QString ClientConfig::initializationWarning() const { return initializationWarning_; }
