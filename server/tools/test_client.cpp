#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

static void sendJson(SOCKET s, const std::string& json) {
    uint32_t len = (uint32_t)json.size();
    char head[4] = { (char)(len >> 24), (char)(len >> 16), (char)(len >> 8), (char)len };
    send(s, head, 4, 0);
    send(s, json.data(), (int)len, 0);
}

static std::string recvJson(SOCKET s) {
    char head[4];
    int got = 0;
    while (got < 4) {
        int n = recv(s, head + got, 4 - got, 0);
        if (n <= 0) return "";
        got += n;
    }
    uint32_t len = ((uint32_t)(uint8_t)head[0] << 24) | ((uint32_t)(uint8_t)head[1] << 16)
                 | ((uint32_t)(uint8_t)head[2] << 8) | (uint32_t)(uint8_t)head[3];
    std::vector<char> buf(len);
    got = 0;
    while (got < (int)len) {
        int n = recv(s, buf.data() + got, (int)len - got, 0);
        if (n <= 0) return "";
        got += n;
    }
    return std::string(buf.data(), len);
}

int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9000);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::printf("connect failed: %d\n", WSAGetLastError());
        return 1;
    }
    std::printf("connected\n");

    sendJson(s, "{\"type\":\"register\",\"username\":\"alice\",\"password\":\"123456\"}");
    std::printf("register: %s\n", recvJson(s).c_str());

    sendJson(s, "{\"type\":\"login\",\"username\":\"alice\",\"password\":\"123456\"}");
    std::printf("login: %s\n", recvJson(s).c_str());

    sendJson(s, "{\"type\":\"join\",\"username\":\"alice\"}");
    std::printf("join: %s\n", recvJson(s).c_str());

    sendJson(s, "{\"type\":\"heartbeat\",\"username\":\"alice\"}");
    std::printf("heartbeat: %s\n", recvJson(s).c_str());

    sendJson(s, "{\"type\":\"forgot\",\"username\":\"alice\",\"newPassword\":\"654321\"}");
    std::printf("forgot: %s\n", recvJson(s).c_str());

    closesocket(s);
    WSACleanup();
    return 0;
}