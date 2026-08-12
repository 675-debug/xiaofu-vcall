#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QFont>
#include <QStringList>
#include <QSysInfo>
#include <QtGlobal>
#include "MainWindow.h"

int main(int argc, char* argv[]) {
    // 花屏排查开关（必须在 QApplication 构造前设置，Chromium 启动时读取）：
    //   XIAOFU_DISABLE_VIDEO_DECODE=1 -> --disable-accelerated-video-decode
    //   XIAOFU_DISABLE_GPU=1          -> --disable-gpu
    // 两个开关互斥使用：先试 VIDEO_DECODE，无效再试 GPU。
    QStringList chromiumFlags;
    if (qEnvironmentVariableIntValue("XIAOFU_DISABLE_VIDEO_DECODE") == 1) {
        chromiumFlags << QStringLiteral("--disable-accelerated-video-decode");
    }
    if (qEnvironmentVariableIntValue("XIAOFU_DISABLE_GPU") == 1) {
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
    MainWindow w;
    w.show();
    return app.exec();
}
