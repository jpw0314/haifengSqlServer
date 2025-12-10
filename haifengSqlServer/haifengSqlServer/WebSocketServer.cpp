#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <bcrypt.h>
#include <wincrypt.h>
#include <string>
#include <vector>
#include "WebSocketServer.h"
#include "ThreadPool.h"

static std::string readUntil(SOCKET s, const std::string& delim) {
    std::string buf;
    char tmp[1024];
    for (;;) {
        int n = recv(s, tmp, sizeof(tmp), 0);
        if (n <= 0) break;
        buf.append(tmp, tmp + n);
        if (buf.find(delim) != std::string::npos) break;
        if (buf.size() > 1 << 20) break;
    }
    return buf;
}

static std::string getHeader(const std::string& req, const std::string& name) {
    std::string needle = name + ":";
    size_t p = req.find(needle);
    if (p == std::string::npos) return std::string();
    size_t b = p + needle.size();
    while (b < req.size() && (req[b] == ' ' || req[b] == '\t')) b++;
    size_t e = req.find("\r\n", b);
    if (e == std::string::npos) e = req.size();
    return req.substr(b, e - b);
}

static std::string sha1Base64(const std::string& data) {
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE h = NULL;
    DWORD hashLen = 0, cb = 0;
    std::vector<UCHAR> hash;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, NULL, 0) != 0) return std::string();
    if (BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (PUCHAR)&hashLen, sizeof(DWORD), &cb, 0) != 0) { BCryptCloseAlgorithmProvider(alg, 0); return std::string(); }
    if (BCryptCreateHash(alg, &h, NULL, 0, NULL, 0, 0) != 0) { BCryptCloseAlgorithmProvider(alg, 0); return std::string(); }
    if (BCryptHashData(h, (PUCHAR)data.data(), (ULONG)data.size(), 0) != 0) { BCryptDestroyHash(h); BCryptCloseAlgorithmProvider(alg, 0); return std::string(); }
    hash.resize(hashLen);
    if (BCryptFinishHash(h, hash.data(), hashLen, 0) != 0) { BCryptDestroyHash(h); BCryptCloseAlgorithmProvider(alg, 0); return std::string(); }
    BCryptDestroyHash(h);
    BCryptCloseAlgorithmProvider(alg, 0);
    DWORD outLen = 0;
    CryptBinaryToStringA(hash.data(), hashLen, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &outLen);
    std::string out; out.resize(outLen);
    if (!CryptBinaryToStringA(hash.data(), hashLen, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &out[0], &outLen)) return std::string();
    if (!out.empty() && (out.back() == '\0' || out.back() == '\n' || out.back() == '\r')) { while (!out.empty() && (out.back() == '\0' || out.back() == '\n' || out.back() == '\r')) out.pop_back(); }
    return out;
}

WebSocketServer& WebSocketServer::instance() { static WebSocketServer inst; return inst; }
WebSocketServer::WebSocketServer() = default;

bool WebSocketServer::start(uint16_t port) {
    stop();
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) return false;
    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { WSACleanup(); return false; }
    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
    sockaddr_in addr; memset(&addr, 0, sizeof(addr)); addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_ANY); addr.sin_port = htons(port);
    if (bind(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) { stop(); return false; }
    if (::listen(s, SOMAXCONN) == SOCKET_ERROR) { stop(); return false; }
    fd_set rf; FD_ZERO(&rf); FD_SET(s, &rf);
    timeval tv; tv.tv_sec = 10; tv.tv_usec = 0;
    if (select(0, &rf, NULL, NULL, &tv) <= 0) { stop(); return false; }
    c = accept(s, NULL, NULL);
    if (c == INVALID_SOCKET) { stop(); return false; }
    std::string req = readUntil(c, "\r\n\r\n");
    std::string key = getHeader(req, "Sec-WebSocket-Key");
    if (key.empty()) { stop(); return false; }
    std::string magic = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string accept = sha1Base64(magic);
    if (accept.empty()) { stop(); return false; }
    std::string resp = "HTTP/1.1 101 Switching Protocols\r\n";
    resp += "Upgrade: websocket\r\n";
    resp += "Connection: Upgrade\r\n";
    resp += "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
    int sn = send(c, resp.c_str(), (int)resp.size(), 0);
    if (sn <= 0) { stop(); return false; }
    return true;
}

void WebSocketServer::listen(std::function<void(const std::string&)> onText) {
    if (c == INVALID_SOCKET) return;
    for (;;) {
        unsigned char hdr[2];
        int n = recv(c, (char*)hdr, 2, 0);
        if (n <= 0) break;
        bool fin = (hdr[0] & 0x80) != 0;
        unsigned char opcode = hdr[0] & 0x0F;
        bool mask = (hdr[1] & 0x80) != 0;
        uint64_t len = hdr[1] & 0x7F;
        if (len == 126) {
            unsigned char ext[2];
            if (recv(c, (char*)ext, 2, 0) != 2) break;
            len = (ext[0] << 8) | ext[1];
        } else if (len == 127) {
            unsigned char ext[8];
            if (recv(c, (char*)ext, 8, 0) != 8) break;
            len = 0; for (int i = 0; i < 8; ++i) { len = (len << 8) | ext[i]; }
        }
        unsigned char maskingKey[4] = {0,0,0,0};
        if (mask) { if (recv(c, (char*)maskingKey, 4, 0) != 4) break; }
        std::vector<unsigned char> payload; payload.resize((size_t)len);
        size_t got = 0;
        while (got < payload.size()) {
            int r = recv(c, (char*)payload.data() + got, (int)(payload.size() - got), 0);
            if (r <= 0) break; got += r;
        }
        if (got != payload.size()) break;
        if (mask) { for (size_t i = 0; i < payload.size(); ++i) payload[i] ^= maskingKey[i % 4]; }
        if (opcode == 0x8) break;
        if (opcode == 0x1 && onText) onText(std::string((const char*)payload.data(), payload.size()));
    }
}

bool WebSocketServer::sendText(const std::string& text) {
    if (c == INVALID_SOCKET) return false;
    std::vector<unsigned char> frame;
    frame.push_back(0x81);
    size_t len = text.size();
    if (len <= 125) { frame.push_back((unsigned char)len); }
    else if (len <= 65535) { frame.push_back(126); frame.push_back((unsigned char)((len >> 8) & 0xFF)); frame.push_back((unsigned char)(len & 0xFF)); }
    else { frame.push_back(127); for (int i = 7; i >= 0; --i) frame.push_back((unsigned char)((((uint64_t)len) >> (i * 8)) & 0xFF)); }
    frame.insert(frame.end(), text.begin(), text.end());
    size_t sent = 0;
    while (sent < frame.size()) {
        int n = send(c, (const char*)frame.data() + sent, (int)(frame.size() - sent), 0);
        if (n <= 0) return false; sent += n;
    }
    return true;
}

bool WebSocketServer::sendTextTo(SOCKET client, const std::string& text) {
    if (client == INVALID_SOCKET) return false;
    std::vector<unsigned char> frame;
    frame.push_back(0x81);
    size_t len = text.size();
    if (len <= 125) { frame.push_back((unsigned char)len); }
    else if (len <= 65535) { frame.push_back(126); frame.push_back((unsigned char)((len >> 8) & 0xFF)); frame.push_back((unsigned char)(len & 0xFF)); }
    else { frame.push_back(127); for (int i = 7; i >= 0; --i) frame.push_back((unsigned char)((((uint64_t)len) >> (i * 8)) & 0xFF)); }
    frame.insert(frame.end(), text.begin(), text.end());
    size_t sent = 0;
    while (sent < frame.size()) {
        int n = send(client, (const char*)frame.data() + sent, (int)(frame.size() - sent), 0);
        if (n <= 0) return false; sent += n;
    }
    return true;
}

bool WebSocketServer::acceptLoop(uint16_t port, size_t workers, std::function<void(SOCKET,const std::string&)> onText) {
    stop();
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) return false;
    std::cout << "服务启动: WebSocket 初始化" << std::endl;
    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { WSACleanup(); return false; }
    int opt = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    sockaddr_in addr; memset(&addr, 0, sizeof(addr)); addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_ANY); addr.sin_port = htons(port);
    if (bind(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) { stop(); return false; }
    if (::listen(s, SOMAXCONN) == SOCKET_ERROR) { stop(); return false; }
    std::cout << "监听端口: " << port << ", 工作线程: " << workers << "\n";
    pool.start(workers);
    for (;;) {
        sockaddr_in raddr; int rlen = sizeof(raddr); memset(&raddr, 0, sizeof(raddr));
        SOCKET cli = accept(s, (sockaddr*)&raddr, &rlen);
        if (cli == INVALID_SOCKET) break;
        char ipbuf[64] = {0};
        inet_ntop(AF_INET, &raddr.sin_addr, ipbuf, sizeof(ipbuf));
        std::cout << "收到连接: socket=" << (uintptr_t)cli << ", 远端=" << ipbuf << ":" << ntohs(raddr.sin_port) << "\n";
        pool.submit([cli, onText]{
            std::string req = readUntil(cli, "\r\n\r\n");
            std::string key = getHeader(req, "Sec-WebSocket-Key");
            if (key.empty()) { closesocket(cli); return; }
            std::string magic = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
            std::string accept = sha1Base64(magic);
            if (accept.empty()) { closesocket(cli); return; }
            std::string resp = "HTTP/1.1 101 Switching Protocols\r\n";
            resp += "Upgrade: websocket\r\n";
            resp += "Connection: Upgrade\r\n";
            resp += "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
            int sn = send(cli, resp.c_str(), (int)resp.size(), 0);
            if (sn <= 0) { closesocket(cli); return; }
            std::cout << "握手成功: socket=" << (uintptr_t)cli << "\n";
            for (;;) {
                unsigned char hdr[2];
                int n = recv(cli, (char*)hdr, 2, 0);
                if (n <= 0) break;
                unsigned char opcode = hdr[0] & 0x0F;
                bool mask = (hdr[1] & 0x80) != 0;
                uint64_t len = hdr[1] & 0x7F;
                if (len == 126) {
                    unsigned char ext[2];
                    if (recv(cli, (char*)ext, 2, 0) != 2) break;
                    len = (ext[0] << 8) | ext[1];
                } else if (len == 127) {
                    unsigned char ext[8];
                    if (recv(cli, (char*)ext, 8, 0) != 8) break;
                    len = 0; for (int i = 0; i < 8; ++i) { len = (len << 8) | ext[i]; }
                }
                unsigned char maskingKey[4] = {0,0,0,0};
                if (mask) { if (recv(cli, (char*)maskingKey, 4, 0) != 4) break; }
                std::vector<unsigned char> payload; payload.resize((size_t)len);
                size_t got = 0;
                while (got < payload.size()) {
                    int r = recv(cli, (char*)payload.data() + got, (int)(payload.size() - got), 0);
                    if (r <= 0) break; got += r;
                }
                if (got != payload.size()) break;
                if (mask) { for (size_t i = 0; i < payload.size(); ++i) payload[i] ^= maskingKey[i % 4]; }
                if (opcode == 0x8) break;
                if (opcode == 0x1) {
                    std::cout << "收到文本: socket=" << (uintptr_t)cli << ", 长度=" << payload.size() << "\n";
                    if (onText) onText(cli, std::string((const char*)payload.data(), payload.size()));
                }
            }
            std::cout << "连接关闭: socket=" << (uintptr_t)cli << "\n";
            closesocket(cli);
        });
    }
    return true;
}

void WebSocketServer::stop() {
    if (c != INVALID_SOCKET) { closesocket(c); c = INVALID_SOCKET; }
    if (s != INVALID_SOCKET) { closesocket(s); s = INVALID_SOCKET; }
    WSACleanup();
}
