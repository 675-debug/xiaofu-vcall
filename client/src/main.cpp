#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QFont>
#include <QStringList>
#include <QSysInfo>
#include <QtGlobal>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>
#include <cstdio>
#include "ClientConfig.h"
#include "MainWindow.h"

// ============================================================
// 文件日志：qDebug/qInfo/qWarning/qCritical 同时写入日志文件。
// 目录优先级：XIAOFU_LOG_DIR 环境变量 > exe 同目录 logs/ > %APPDATA%/xiaofu-vcall-client/logs/
// 文件名按天轮转：xiaofu-vcall-client-YYYY-MM-DD.log
// 同时保留默认输出（Qt Creator / 控制台仍可见）。
// ============================================================
namespace {

QMutex g_logMutex;
QString g_logDir;
QString g_logDay;
QFile* g_logFile = nullptr;
QtMessageHandler g_prevHandler = nullptr;

const char* logLevelName(QtMsgType type) {
    switch (type) {
    case QtDebugMsg:    return "Debug";
    case QtInfoMsg:     return "Info";
    case QtWarningMsg:  return "Warning";
    case QtCriticalMsg: return "Critical";
    case QtFatalMsg:    return "Fatal";
    }
    return "Debug";
}

void fileLogMessageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    QMutexLocker lock(&g_logMutex);

    const QString line = QStringLiteral("%1 [%2] %3")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
                                  QLatin1String(logLevelName(type)),
                                  msg);

    // 跨天自动切换到新文件
    const QString day = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    if (g_logFile && day != g_logDay) {
        g_logFile->close();
        delete g_logFile;
        g_logFile = nullptr;
    }
    if (!g_logFile && !g_logDir.isEmpty()) {
        g_logDay = day;
        const QString path = g_logDir + QStringLiteral("/xiaofu-vcall-client-") + day + QStringLiteral(".log");
        g_logFile = new QFile(path);
        if (!g_logFile->open(QIODevice::Append | QIODevice::Text)) {
            delete g_logFile;
            g_logFile = nullptr;
        }
    }
    if (g_logFile && g_logFile->isOpen()) {
        QTextStream out(g_logFile);
        out << line << QLatin1Char('\n');
        out.flush();
    }

    // 保留默认输出，Qt Creator / 控制台仍然可见
    if (g_prevHandler) {
        g_prevHandler(type, ctx, msg);
    } else {
        std::fprintf(stderr, "%s\n", line.toUtf8().constData());
        std::fflush(stderr);
    }
}

// 尝试创建 dir 并验证可写；成功返回 true
bool ensureWritableDir(const QString& dir) {
    QDir d(dir);
    if (!d.mkpath(QStringLiteral("."))) {
        return false;
    }
    QFile probe(dir + QStringLiteral("/.writetest"));
    if (!probe.open(QIODevice::WriteOnly)) {
        return false;
    }
    probe.remove();
    return true;
}

void installFileLogger() {
    QString logDir;

    const QByteArray envDir = qgetenv("XIAOFU_LOG_DIR");
    if (!envDir.isEmpty()) {
        logDir = QString::fromLocal8Bit(envDir);
        if (!ensureWritableDir(logDir)) {
            logDir.clear();
        }
    }

    if (logDir.isEmpty()) {
        const QString exeLogDir =
            QCoreApplication::applicationDirPath() + QStringLiteral("/logs");
        if (ensureWritableDir(exeLogDir)) {
            logDir = exeLogDir;
        }
    }

    if (logDir.isEmpty()) {
        const QString appDataLogDir =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/logs");
        if (ensureWritableDir(appDataLogDir)) {
            logDir = appDataLogDir;
        }
    }

    if (logDir.isEmpty()) {
        qWarning().noquote() << "[Log] file logging disabled: no writable log directory";
        return;
    }

    {
        QMutexLocker lock(&g_logMutex);
        g_logDir = logDir;
        g_prevHandler = qInstallMessageHandler(fileLogMessageHandler);
    }
    qInfo().noquote() << "[Log] LOG_DIR=" << logDir;
}
} // namespace

int main(int argc, char* argv[]) {
    // 花屏排查开关（必须在 QApplication 构造前设置，Chromium 启动时读取）：
    //   XIAOFU_DISABLE_VIDEO_DECODE=1 -> --disable-accelerated-video-decode
    //   XIAOFU_DISABLE_GPU=1          -> --disable-gpu
    // 两个开关互斥使用：先试 VIDEO_DECODE，无效再试 GPU。
    // QApplication 尚未构造；Windows 使用 GetModuleFileNameW 严格定位 exe。
    const QString configPath = ClientConfig::executableConfigPath(QString::fromLocal8Bit(argv[0]));
    const QString configExamplePath = QFileInfo(configPath).absoluteDir()
                                          .filePath(QStringLiteral("config.ini.example"));
    ClientConfig::ensureRuntimeConfig(configPath, configExamplePath);
    const ClientConfig config(configPath);
    QStringList chromiumFlags;
    if (config.disableVideoDecode()) {
        chromiumFlags << QStringLiteral("--disable-accelerated-video-decode");
    }
    if (config.disableGpu()) {
        chromiumFlags << QStringLiteral("--disable-gpu");
    }
    if (!chromiumFlags.isEmpty()) {
        QString existing = QString::fromUtf8(qgetenv("QTWEBENGINE_CHROMIUM_FLAGS"));
        if (!existing.isEmpty()) {
            existing += QLatin1Char(' ');
        }
        existing += chromiumFlags.join(QLatin1Char(' '));
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", existing.toUtf8());
        qDebug().noquote() << "[Main] QTWEBENGINE_CHROMIUM_FLAGS =" << existing;
    }

    QApplication app(argc, argv);
    // ????????????? QSettings ?????????/??????????
    QCoreApplication::setOrganizationName(QStringLiteral("xiaofu-vcall"));
    QCoreApplication::setApplicationName(QStringLiteral("xiaofu-vcall-client"));
    // 文件日志：XIAOFU_LOG_DIR 环境变量 > exe 同目录 logs/ > %APPDATA%/xiaofu-vcall-client/logs/
    installFileLogger();
    if (!config.initializationWarning().isEmpty())
        qWarning().noquote() << "[Config]" << config.initializationWarning();
    qInfo().noquote() << "[Config] source=" << QFileInfo(config.sourcePath()).fileName();
    qInfo().noquote() << "[WebEnv] Qt version=" << qVersion();
    qInfo().noquote() << "[WebEnv] OS=" << QSysInfo::prettyProductName()
                      << "kernel=" << QSysInfo::kernelType() << QSysInfo::kernelVersion();
    qInfo().noquote() << "[WebEnv] CPU arch=" << QSysInfo::currentCpuArchitecture();
    qInfo().noquote() << "[WebEnv] QTWEBENGINE_CHROMIUM_FLAGS="
                      << QString::fromUtf8(qgetenv("QTWEBENGINE_CHROMIUM_FLAGS"));
    QFont appFont(QStringLiteral("Microsoft YaHei UI"));
    appFont.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(appFont);

    // 弹窗统一白底样式，避免跟随系统深色主题出现黑色弹窗
    app.setStyleSheet(
        "QMessageBox { background-color: #FFFFFF; }"
        "QMessageBox QLabel { color: #1A1A2E; background: transparent; font-size: 13px; }"
        "QMessageBox QPushButton { background-color: #007AFF; color: #FFFFFF;"
        " border: none; border-radius: 6px; min-width: 72px; padding: 6px 14px; }"
        "QMessageBox QPushButton:hover { background-color: #0062CC; }");
    MainWindow w(config);
    w.show();
    return app.exec();
}
