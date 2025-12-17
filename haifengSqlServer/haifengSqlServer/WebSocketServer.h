#pragma once
#include <string>
#include <functional>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "ThreadPool.h"

class WebSocketServer {
public:
    static WebSocketServer& instance();
    WebSocketServer(const WebSocketServer&) = delete;
    WebSocketServer& operator=(const WebSocketServer&) = delete;
    WebSocketServer(WebSocketServer&&) = delete;
    WebSocketServer& operator=(WebSocketServer&&) = delete;

    bool start(uint16_t port);
    void listen(std::function<void(const std::string&)> onText);
    bool sendText(const std::string& text);
    bool acceptLoop(uint16_t port, size_t workers, std::function<void(SOCKET,const std::string&)> onText);
    static bool sendTextTo(SOCKET client, const std::string& text);
    void stop();

private:
    WebSocketServer();
    SOCKET s = INVALID_SOCKET;
    SOCKET c = INVALID_SOCKET;
    ThreadPool pool;
};
