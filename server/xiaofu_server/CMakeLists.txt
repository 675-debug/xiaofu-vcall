cmake_minimum_required(VERSION 3.16)
project(xiaofu-server CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

find_path(MYSQLCLIENT_INCLUDE_DIR mysql/mysql.h REQUIRED)
find_library(MYSQLCLIENT_LIBRARY mysqlclient REQUIRED)
set(MYSQL_LIBRARY_TARGET ${MYSQLCLIENT_LIBRARY})
set(MYSQL_INCLUDE_DIR ${MYSQLCLIENT_INCLUDE_DIR})
set(PASSWORD_HASH_LIBRARY OpenSSL::Crypto)

set(BUSINESS_SOURCES
    src/protocol/JsonValue.cpp
    src/handler/JoinHandler.cpp
    src/handler/CallSession.cpp
    src/handler/LoginHandler.cpp
    src/handler/RegisterHandler.cpp
    src/handler/ForgotHandler.cpp
    src/handler/ChatHandler.cpp
    src/heartbeat/HeartbeatManager.cpp
    src/db/DbManager.cpp
    src/db/PasswordHasher.cpp
    src/util/Validator.cpp
    src/util/Log.cpp
)

if(UNIX AND NOT APPLE)
    set(SERVER_SOURCES
        src/main.cpp
        src/ServerApp.cpp
        src/net/SocketPlatform.cpp
        src/net/EpollLoop.cpp
        src/net/TcpServer.cpp
        src/net/Connection.cpp
        src/concurrency/ThreadPool.cpp
        src/concurrency/CompletionDispatcher.cpp
        src/process/Daemon.cpp
        ${BUSINESS_SOURCES}
    )

    add_executable(xiaofu-server ${SERVER_SOURCES})
    target_include_directories(xiaofu-server PRIVATE src)
    target_link_libraries(xiaofu-server PRIVATE
        ${MYSQL_LIBRARY_TARGET}
        ${PASSWORD_HASH_LIBRARY}
        Threads::Threads)
endif()

add_executable(unit_test
    tools/unit_test.cpp
    src/protocol/JsonValue.cpp
    src/db/PasswordHasher.cpp)
target_include_directories(unit_test PRIVATE src)
target_link_libraries(unit_test PRIVATE ${PASSWORD_HASH_LIBRARY})

add_executable(chat_handler_test
    tools/chat_handler_test.cpp
    src/db/DbManager.cpp
    src/handler/ChatHandler.cpp
    src/handler/JoinHandler.cpp
    src/util/Log.cpp)
target_include_directories(chat_handler_test PRIVATE src ${MYSQL_INCLUDE_DIR})
target_link_libraries(chat_handler_test PRIVATE
    ${MYSQL_LIBRARY_TARGET}
    Threads::Threads)

add_executable(thread_pool_test
    tools/thread_pool_test.cpp
    src/concurrency/ThreadPool.cpp
    src/db/DbManager.cpp
    src/util/Log.cpp)
target_include_directories(thread_pool_test PRIVATE src ${MYSQL_INCLUDE_DIR})
target_link_libraries(thread_pool_test PRIVATE
    ${MYSQL_LIBRARY_TARGET}
    Threads::Threads)

if(WIN32)
    add_executable(test_client tools/test_client.cpp)
    target_link_libraries(test_client PRIVATE ws2_32)
endif()

add_executable(call_session_test
    tools/call_session_test.cpp
    src/handler/CallSession.cpp
    src/util/Log.cpp)
target_include_directories(call_session_test PRIVATE src)

add_executable(login_concurrency_test
    tools/login_concurrency_test.cpp
    src/db/DbManager.cpp
    src/util/Log.cpp)
target_include_directories(login_concurrency_test PRIVATE src ${MYSQL_INCLUDE_DIR})
target_link_libraries(login_concurrency_test PRIVATE
    ${MYSQL_LIBRARY_TARGET}
    Threads::Threads)
