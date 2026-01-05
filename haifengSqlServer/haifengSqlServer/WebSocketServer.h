#pragma once
#include <string>
#include <functional>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "ThreadPool.h"

// WebSocket 服务器类，负责处理 WebSocket 连接和消息通信
// 采用单例模式
class WebSocketServer {
public:
    // 获取单例实例
    static WebSocketServer& instance();
    WebSocketServer(const WebSocketServer&) = delete;
    WebSocketServer& operator=(const WebSocketServer&) = delete;
    WebSocketServer(WebSocketServer&&) = delete;
    WebSocketServer& operator=(WebSocketServer&&) = delete;

    // 启动服务器（阻塞模式，用于简单测试）
    bool start(uint16_t port);
    // 监听连接（已废弃或用于简单模式）
    void listen(std::function<void(const std::string&)> onText);
    // 发送文本消息给当前连接（简单模式）
    bool sendText(const std::string& text);
    
    // 启动接受连接的主循环，使用线程池处理并发连接
    // port: 监听端口
    // workers: 线程池工作线程数量
    // onText: 收到文本消息的回调函数
    bool acceptLoop(uint16_t port, size_t workers, std::function<void(SOCKET,const std::string&)> onText);
    
    // 静态辅助函数：向指定 socket 发送文本消息
    static bool sendTextTo(SOCKET client, const std::string& text);
    
    // 停止服务器并释放资源
    void stop();

private:
    WebSocketServer();
    SOCKET s = INVALID_SOCKET; // 监听 socket
    SOCKET c = INVALID_SOCKET; // 简单模式下的客户端 socket
    ThreadPool pool;           // 处理并发连接的线程池
};
