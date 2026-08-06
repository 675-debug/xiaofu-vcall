#include <QApplication>
#include <QFont>
#include "MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
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
