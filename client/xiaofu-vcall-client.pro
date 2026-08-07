QT += core gui widgets network webenginewidgets webchannel
TARGET = xiaofu-vcall-client
TEMPLATE = app
CONFIG += c++11
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
    src/video/WebRtcBridge.h \
    src/video/VideoCallController.h \
    src/network/NetworkManager.h

FORMS += \
    ui/MainWindow.ui \
    ui/MainWidget.ui \
    ui/LoginWidget.ui \
    ui/RegisterWidget.ui \
    ui/ForgotPasswordWidget.ui \
    ui/ChatWidget.ui \
    ui/CallWidget.ui

RESOURCES += resources.qrc
