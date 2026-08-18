QT += core gui widgets
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = client_config_test
INCLUDEPATH += ../src
SOURCES += client_config_test.cpp \
           ../src/ClientConfig.cpp
HEADERS += ../src/ClientConfig.h
