cmake_minimum_required(VERSION 3.16)
project(xiaofu-vcall-client CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt ? moc / uic / rcc ????
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/ui)

# MSVC ??? UTF-8 ???????/????????
if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Gui Widgets Network WebEngineWidgets WebChannel)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Network WebEngineWidgets WebChannel)
if(QT_VERSION_MAJOR LESS 6)
    # Qt5 ?? multimedia?Qt6 ????
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Multimedia)
endif()

add_executable(xiaofu-vcall-client WIN32
    src/main.cpp
    src/ClientConfig.cpp
    src/MainWindow.cpp
    src/MainWidget.cpp
    src/LoginWidget.cpp
    src/RegisterWidget.cpp
    src/ForgotPasswordWidget.cpp
    src/ChatWidget.cpp
    src/CallWidget.cpp
    src/video/WebRtcBridge.cpp
    src/video/VideoCallController.cpp
    src/network/NetworkManager.cpp
    src/MainWindow.h
    src/ClientConfig.h
    src/MainWidget.h
    src/LoginWidget.h
    src/RegisterWidget.h
    src/ForgotPasswordWidget.h
    src/ChatWidget.h
    src/CallWidget.h
    src/RecordingPaths.h
    src/video/WebRtcBridge.h
    src/video/VideoCallController.h
    src/network/NetworkManager.h
    src/network/ProtocolCodes.h
    ui/MainWindow.ui
    ui/MainWidget.ui
    ui/LoginWidget.ui
    ui/RegisterWidget.ui
    ui/ForgotPasswordWidget.ui
    ui/ChatWidget.ui
    ui/CallWidget.ui
    resources.qrc
)

target_include_directories(xiaofu-vcall-client PRIVATE src)

add_custom_command(TARGET xiaofu-vcall-client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/config.ini.example
            $<TARGET_FILE_DIR:xiaofu-vcall-client>/config.ini.example
    COMMENT "Copying runtime configuration example"
)

target_link_libraries(xiaofu-vcall-client PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::WebEngineWidgets
    Qt${QT_VERSION_MAJOR}::WebChannel
)
if(QT_VERSION_MAJOR LESS 6)
    target_link_libraries(xiaofu-vcall-client PRIVATE Qt${QT_VERSION_MAJOR}::Multimedia)
endif()

# Windows exe icon resource
if(WIN32)
    target_sources(xiaofu-vcall-client PRIVATE app.rc)
endif()
