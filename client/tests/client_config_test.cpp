#include "ClientConfig.h"
#include "CallWidget.h"
#include "MainWindow.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QtGlobal>

#include <cstdlib>
#include <iostream>
#include <type_traits>

namespace {

class EnvironmentGuard {
public:
    explicit EnvironmentGuard(const QByteArray& name)
        : name_(name), existed_(qEnvironmentVariableIsSet(name.constData())), value_(qgetenv(name.constData())) {}

    ~EnvironmentGuard() {
        if (existed_)
            qputenv(name_.constData(), value_);
        else
            qunsetenv(name_.constData());
    }

private:
    QByteArray name_;
    bool existed_;
    QByteArray value_;
};

struct EnvironmentSnapshot {
    QByteArray name;
    bool existed;
    QByteArray value;
};

const char* const kEnvironmentNames[] = {
    "XIAOFU_SERVER_HOST", "XIAOFU_SERVER_PORT", "XIAOFU_STUN_URL",
    "XIAOFU_TURN_URL", "XIAOFU_TURN_USERNAME", "XIAOFU_TURN_CREDENTIAL",
    "XIAOFU_ICE_POLICY", "XIAOFU_ASR_URL", "XIAOFU_DISABLE_GPU",
    "XIAOFU_DISABLE_VIDEO_DECODE"
};

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

QString writeIni(QTemporaryDir& dir, const QByteArray& contents) {
    const QString path = dir.filePath(QStringLiteral("config.ini"));
    QFile file(path);
    check(file.open(QIODevice::WriteOnly | QIODevice::Text), "open temporary config.ini");
    check(file.write(contents) == contents.size(), "write temporary config.ini");
    return path;
}

QList<EnvironmentSnapshot> captureEnvironment() {
    QList<EnvironmentSnapshot> snapshots;
    for (const char* name : kEnvironmentNames) {
        snapshots.append({QByteArray(name), qEnvironmentVariableIsSet(name), qgetenv(name)});
    }
    return snapshots;
}

void clearConfigEnvironment() {
    for (const char* name : kEnvironmentNames)
        qunsetenv(name);
}

void restoreEnvironment(const QList<EnvironmentSnapshot>& snapshots) {
    for (const EnvironmentSnapshot& snapshot : snapshots) {
        if (snapshot.existed)
            qputenv(snapshot.name.constData(), snapshot.value);
        else
            qunsetenv(snapshot.name.constData());
    }
}

void checkEnvironmentEquals(const QList<EnvironmentSnapshot>& snapshots) {
    for (const EnvironmentSnapshot& snapshot : snapshots) {
        check(qEnvironmentVariableIsSet(snapshot.name.constData()) == snapshot.existed,
              "environment variable existence restored");
        if (snapshot.existed)
            check(qgetenv(snapshot.name.constData()) == snapshot.value,
                  "environment variable value restored");
    }
}

void testRuntimeConfigInitialization() {
    QTemporaryDir dir;
    const QString examplePath = dir.filePath(QStringLiteral("config.ini.example"));
    const QString runtimePath = dir.filePath(QStringLiteral("config.ini"));
    QFile example(examplePath);
    check(example.open(QIODevice::WriteOnly | QIODevice::Text), "open config example");
    check(example.write("[network]\nserver_port=9100\n") > 0, "write config example");
    example.close();

    check(ClientConfig::ensureRuntimeConfig(runtimePath, examplePath),
          "initialize missing runtime config");
    QFile runtime(runtimePath);
    check(runtime.open(QIODevice::ReadOnly | QIODevice::Text), "open initialized runtime config");
    check(runtime.readAll().contains("server_port=9100"), "runtime config copied from example");
    runtime.close();

    check(runtime.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text),
          "open existing runtime config");
    check(runtime.write("real-secret-config") > 0, "write existing runtime config");
    runtime.close();
    check(ClientConfig::ensureRuntimeConfig(runtimePath, examplePath),
          "existing runtime config accepted");
    check(runtime.open(QIODevice::ReadOnly | QIODevice::Text), "reopen existing runtime config");
    check(runtime.readAll() == QByteArray("real-secret-config"),
          "existing runtime config is never overwritten");
}

void testReadOnlyRuntimeFallsBackToExample() {
    QTemporaryDir dir;
    const QString examplePath = dir.filePath(QStringLiteral("config.ini.example"));
    const QString runtimePath = dir.filePath(QStringLiteral("missing-parent/config.ini"));
    QFile example(examplePath);
    check(example.open(QIODevice::WriteOnly | QIODevice::Text), "open fallback example");
    check(example.write("[subtitle]\nasr_url=wss://asr.example.test/socket\n") > 0,
          "write fallback example");
    example.close();

    check(!ClientConfig::ensureRuntimeConfig(runtimePath, examplePath),
          "copy into missing directory fails stably");
    const ClientConfig config(runtimePath, examplePath);
    check(config.sourcePath() == examplePath, "example becomes effective source");
    check(config.asrUrl() == QStringLiteral("wss://asr.example.test/socket"),
          "example values remain effective after copy failure");
    check(!config.initializationWarning().isEmpty(), "copy failure exposes deferred warning");
}

void testMalformedUrlsAreRejected() {
    QTemporaryDir dir;
    const QString path = writeIni(dir,
        "[webrtc]\nstun_url=stun:bad host:3478\nturn_url=turn:host\"evil:3478\n"
        "[subtitle]\nasr_url=ws:garbage\n");
    const ClientConfig config(path);
    check(config.stunUrl().isEmpty(), "STUN with invalid authority is rejected");
    check(config.turnUrl().isEmpty(), "TURN containing quote is rejected");
    check(config.asrUrl().isEmpty(), "opaque ws URL without host is rejected");

    EnvironmentGuard asrEnvironment("XIAOFU_ASR_URL");
    qputenv("XIAOFU_ASR_URL", "wss://asr.example.test/\"injection");
    const ClientConfig quoteConfig(path);
    check(quoteConfig.asrUrl().isEmpty(), "WebSocket URL containing quote is rejected");
}

void testSingleSnapshotInterfacesCompile() {
    static_assert(std::is_constructible<MainWindow, const ClientConfig&, QWidget*>::value,
                  "MainWindow must receive the startup ClientConfig snapshot");
    using Setter = void (CallWidget::*)(const ClientConfig&);
    static_assert(std::is_same<decltype(&CallWidget::setClientConfig), Setter>::value,
                  "CallWidget exposes ClientConfig snapshot setter");
}

void testIniValues() {
    QTemporaryDir dir;
    check(dir.isValid(), "create temporary directory");
    const QString path = writeIni(dir,
        "[network]\nserver_host=192.0.2.10\nserver_port=9100\n"
        "[webrtc]\nstun_url=stuns:192.0.2.20:3478\nturn_url=turns:192.0.2.21:5349\n"
        "turn_username=alice\nturn_credential=secret\nice_policy=relay\n"
        "[subtitle]\nasr_url=wss://asr.example.test/socket\n"
        "[performance]\ndisable_gpu=true\ndisable_video_decode=1\n");

    const ClientConfig config(path);
    check(config.serverHost() == QStringLiteral("192.0.2.10"), "INI server host");
    check(config.serverPort() == 9100, "INI server port");
    check(config.stunUrl() == QStringLiteral("stuns:192.0.2.20:3478"), "INI STUN URL");
    check(config.turnUrl() == QStringLiteral("turns:192.0.2.21:5349"), "INI TURN URL");
    check(config.turnUsername() == QStringLiteral("alice"), "INI TURN username");
    check(config.turnCredential() == QStringLiteral("secret"), "INI TURN credential");
    check(config.icePolicy() == QStringLiteral("relay"), "INI ICE policy");
    check(config.asrUrl() == QStringLiteral("wss://asr.example.test/socket"), "INI ASR URL");
    check(config.disableGpu(), "INI disable GPU");
    check(config.disableVideoDecode(), "INI disable video decode");
}

void testInvalidValuesFallBack() {
    QTemporaryDir dir;
    const QString path = writeIni(dir,
        "[network]\nserver_port=70000\n"
        "[webrtc]\nstun_url=https://bad.example\nturn_url=stun:wrong.example\nice_policy=invalid\n"
        "[subtitle]\nasr_url=http://bad.example\n"
        "[performance]\ndisable_gpu=maybe\ndisable_video_decode=2\n");

    const ClientConfig config(path);
    check(config.serverHost() == QStringLiteral("8.137.152.134"), "default server host");
    check(config.serverPort() == 9000, "invalid port falls back");
    check(config.stunUrl().isEmpty(), "invalid STUN is skipped");
    check(config.turnUrl().isEmpty(), "invalid TURN is skipped");
    check(config.icePolicy() == QStringLiteral("all"), "invalid ICE policy falls back");
    check(config.asrUrl().isEmpty(), "invalid ASR is skipped");
    check(!config.disableGpu(), "invalid GPU boolean falls back");
    check(!config.disableVideoDecode(), "invalid video decode boolean falls back");
}

void testEnvironmentOverridesIni() {
    QTemporaryDir dir;
    const QString path = writeIni(dir,
        "[network]\nserver_host=ini.example\nserver_port=9100\n"
        "[webrtc]\nstun_url=stun:ini.example:3478\nturn_url=turn:ini.example:3478\n"
        "turn_username=ini-user\nturn_credential=ini-secret\nice_policy=all\n"
        "[subtitle]\nasr_url=ws://ini.example:10095\n"
        "[performance]\ndisable_gpu=false\ndisable_video_decode=false\n");

    EnvironmentGuard host("XIAOFU_SERVER_HOST");
    EnvironmentGuard port("XIAOFU_SERVER_PORT");
    EnvironmentGuard stun("XIAOFU_STUN_URL");
    EnvironmentGuard turn("XIAOFU_TURN_URL");
    EnvironmentGuard user("XIAOFU_TURN_USERNAME");
    EnvironmentGuard credential("XIAOFU_TURN_CREDENTIAL");
    EnvironmentGuard policy("XIAOFU_ICE_POLICY");
    EnvironmentGuard asr("XIAOFU_ASR_URL");
    EnvironmentGuard gpu("XIAOFU_DISABLE_GPU");
    EnvironmentGuard decode("XIAOFU_DISABLE_VIDEO_DECODE");

    qputenv("XIAOFU_SERVER_HOST", "env.example");
    qputenv("XIAOFU_SERVER_PORT", "9200");
    qputenv("XIAOFU_STUN_URL", "stun:env.example:3478");
    qputenv("XIAOFU_TURN_URL", "turn:env.example:3478");
    qputenv("XIAOFU_TURN_USERNAME", "env-user");
    qputenv("XIAOFU_TURN_CREDENTIAL", "env-secret");
    qputenv("XIAOFU_ICE_POLICY", "relay");
    qputenv("XIAOFU_ASR_URL", "ws://env.example:10095");
    qputenv("XIAOFU_DISABLE_GPU", "yes");
    qputenv("XIAOFU_DISABLE_VIDEO_DECODE", "true");

    const ClientConfig config(path);
    check(config.serverHost() == QStringLiteral("env.example"), "environment server host");
    check(config.serverPort() == 9200, "environment server port");
    check(config.stunUrl() == QStringLiteral("stun:env.example:3478"), "environment STUN");
    check(config.turnUrl() == QStringLiteral("turn:env.example:3478"), "environment TURN");
    check(config.turnUsername() == QStringLiteral("env-user"), "environment TURN username");
    check(config.turnCredential() == QStringLiteral("env-secret"), "environment TURN credential");
    check(config.icePolicy() == QStringLiteral("relay"), "environment ICE policy");
    check(config.asrUrl() == QStringLiteral("ws://env.example:10095"), "environment ASR");
    check(config.disableGpu(), "environment GPU boolean");
    check(config.disableVideoDecode(), "environment decode boolean");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QList<EnvironmentSnapshot> originalEnvironment = captureEnvironment();
    clearConfigEnvironment();
    testRuntimeConfigInitialization();
    testReadOnlyRuntimeFallsBackToExample();
    testMalformedUrlsAreRejected();
    testSingleSnapshotInterfacesCompile();
    testIniValues();
    testInvalidValuesFallBack();
    testEnvironmentOverridesIni();
    restoreEnvironment(originalEnvironment);
    checkEnvironmentEquals(originalEnvironment);
    std::cout << "client_config_test: PASS\n";
    return 0;
}
