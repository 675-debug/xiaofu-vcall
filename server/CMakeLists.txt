cmake_minimum_required(VERSION 3.16)
project(xiaofu-server CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 本机 mingw 自带的 sqlite3（Linux 版后续改成系统包）
set(SQLITE_INCLUDE_DIR "D:/app/Qt/Tools/mingw730_64/opt/include" CACHE PATH "sqlite3 include dir")
set(SQLITE_LIB "D:/app/Qt/Tools/mingw730_64/opt/lib/libsqlite3.a" CACHE FILEPATH "sqlite3 static lib")

set(SERVER_SOURCES
    src/main.cpp
    src/net/EventLoopWin.cpp
    src/net/TcpServer.cpp
    src/net/Connection.cpp
    src/protocol/JsonValue.cpp
    src/handler/JoinHandler.cpp
    src/handler/LoginHandler.cpp
    src/handler/RegisterHandler.cpp
    src/handler/ForgotHandler.cpp
    src/heartbeat/HeartbeatManager.cpp
    src/db/DbManager.cpp
    src/db/PasswordHasher.cpp
    src/util/Validator.cpp
    src/util/Log.cpp
)

add_executable(xiaofu-server ${SERVER_SOURCES})
target_include_directories(xiaofu-server PRIVATE src ${SQLITE_INCLUDE_DIR})
target_link_libraries(xiaofu-server PRIVATE ${SQLITE_LIB} ws2_32 advapi32)

add_executable(unit_test tools/unit_test.cpp src/protocol/JsonValue.cpp src/db/PasswordHasher.cpp)
target_include_directories(unit_test PRIVATE src)
target_link_libraries(unit_test PRIVATE advapi32)

add_executable(test_client tools/test_client.cpp)
target_link_libraries(test_client PRIVATE ws2_32)

# 独立工具：检查数据库 users 表内容
add_executable(db_check tools/db_check.cpp)
target_include_directories(db_check PRIVATE ${SQLITE_INCLUDE_DIR})
target_link_libraries(db_check PRIVATE ${SQLITE_LIB})
