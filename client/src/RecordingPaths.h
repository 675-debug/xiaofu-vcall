#pragma once
// 录屏保存目录的共享逻辑（客户端本地设置，不涉及服务器/MySQL）。
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QString>

namespace RecordingPaths {

// 默认录屏目录：MoviesLocation/xiaofu-vcall -> DownloadLocation/xiaofu-vcall -> homePath()/xiaofu-vcall
inline QString defaultDirectory() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (!dir.isEmpty())
        return dir + QStringLiteral("/xiaofu-vcall");
    dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (!dir.isEmpty())
        return dir + QStringLiteral("/xiaofu-vcall");
    return QDir::homePath() + QStringLiteral("/xiaofu-vcall");
}

// 用户配置的目录（QSettings recording/saveDirectory），未配置时返回默认目录。
inline QString configuredOrDefault() {
    const QString saved = QSettings().value(QStringLiteral("recording/saveDirectory")).toString().trimmed();
    if (!saved.isEmpty())
        return saved;
    return defaultDirectory();
}

// 确保目录存在；不存在则自动创建。创建失败返回 false。
inline bool ensureDirectory(const QString& dir) {
    if (dir.isEmpty())
        return false;
    if (QDir(dir).exists())
        return true;
    return QDir().mkpath(dir);
}

} // namespace RecordingPaths
