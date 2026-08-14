greaterThan(QT_MAJOR_VERSION, 5) {
    QT += core gui widgets network webenginewidgets webchannel
} else {
    QT += core gui widgets network multimedia webenginewidgets webchannel
}
TARGET = xiaofu-vcall-client
TEMPLATE = app
greaterThan(QT_MAJOR_VERSION, 5) {
    CONFIG += c++17
} else {
    CONFIG += c++11
}
CONFIG -= debug_and_release
DESTDIR = $$PWD/debug
INCLUDEPATH += src

SOURCES += \
    src/main.cpp \
    src/MainWindow.cpp \
    src/MainWidget.cpp \
    src/LoginWidget.cpp \
    src/RegisterWidget.cpp \
    src/ForgotPasswordWidget.cpp \
    src/ChatWidget.cpp \
    src/CallWidget.cpp \
    src/video/WebRtcBridge.cpp \
    src/video/VideoCallController.cpp \
    src/network/NetworkManager.cpp

HEADERS += \
    src/MainWindow.h \
    src/MainWidget.h \
    src/LoginWidget.h \
    src/RegisterWidget.h \
    src/ForgotPasswordWidget.h \
    src/ChatWidget.h \
    src/CallWidget.h \
    src/RecordingPaths.h \
    src/video/WebRtcBridge.h \
    src/video/VideoCallController.h \
    src/network/NetworkManager.h \
    src/network/ProtocolCodes.h

FORMS += \
    ui/MainWindow.ui \
    ui/MainWidget.ui \
    ui/LoginWidget.ui \
    ui/RegisterWidget.ui \
    ui/ForgotPasswordWidget.ui \
    ui/ChatWidget.ui \
    ui/CallWidget.ui

RESOURCES += resources.qrc

# WebEngine 网页资源（resources/video/** 的 JS）由 Chromium/V8 运行时解析，不是 QML JavaScript。
# Qt 6.11 的 qtquickcompiler(qmlcachegen) 会把资源内所有 .js 当 QML JS 预编译，
# 无法解析 async/await 等现代浏览器语法（误报 Expected token ','），且本项目没有 QML 文件，
# 因此显式关闭该特性，让 HTML/CSS/JS 保持普通资源由 WebEngine 运行时加载。
CONFIG -= qtquickcompiler

# Windows exe 图标
RC_ICONS += app.ico

# 运行配置与 exe 同目录分发；环境变量 XIAOFU_ASR_URL 仍可优先覆盖。
asr_config.files = $$PWD/asr.url
asr_config.path = $$DESTDIR
COPIES += asr_config
