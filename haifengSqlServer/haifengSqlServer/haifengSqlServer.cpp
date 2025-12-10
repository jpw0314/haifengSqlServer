#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <string>
#include <fstream>
#include <sql.h>
#include <sqlext.h>
#include "DbClient.h"
#include "WebSocketServer.h"
#include "QueryRouter.h"

static std::string w2u8(const std::wstring& ws) {
    if (ws.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string out;
    out.resize(n);
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, &out[0], n, nullptr, nullptr);
    if (len > 0) out.resize(len - 1); else out.clear();
    return out;
}

static void runConnectivityTest(DbClient& cli) {
    std::string line = std::string("Connected: Server=") + w2u8(cli.server);
    if (!cli.db.empty()) line += std::string(" Database=") + w2u8(cli.db);
    line += std::string(" Driver=") + w2u8(cli.usedDriver);
    if (!cli.uid.empty()) line += std::string(" User=") + w2u8(cli.uid); else line += " User=Windows Authentication";
    std::cout << line << "\n";
    SQLWCHAR name[256];
    SQLWCHAR ver[256];
    SQLGetInfo(cli.dbc, SQL_DBMS_NAME, name, sizeof(name) / sizeof(SQLWCHAR), NULL);
    SQLGetInfo(cli.dbc, SQL_DBMS_VER, ver, sizeof(ver) / sizeof(SQLWCHAR), NULL);
    std::cout << "DBMS: " << w2u8(std::wstring(name)) << " " << w2u8(std::wstring(ver)) << "\n";
}


int main(int argc, char** argv) {
    //SetConsoleOutputCP(CP_UTF8);
    system("chcp 936");
    DbClient& cli = DbClient::instance();
    if (!cli.init(L"dbconfig.ini")) { std::cout << "Read config failed\n"; return 1; }
    if (!cli.connect()) { std::cout << "Connection failed\n"; return 1; }
    std::cout << "连接数据库成功"<<std::endl;
    {
        if (!QueryRouter::instance().loadDir(L"queries")) {
            QueryRouter::instance().load(L"queries.ini");
        }
        std::cout << "加载查询配置: " << QueryRouter::instance().count() << " 条" << "\n";
        bool selftest = false; std::string specified;
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--selftest") selftest = true;
            else if (a.rfind("--query=", 0) == 0) specified = a.substr(8);
        }
        if (selftest) {
            auto run = [](const std::string& id){
                std::string msg = std::string("{") + "\"query\":\"" + id + "\"}";
                std::string resp = QueryRouter::instance().handle(msg);
                std::cout << "测试查询: " << id << "\n" << resp << "\n";
            };
            if (!specified.empty()) {
                run(specified);
            } else {
                run("production.month_ok_rate");
                run("production.stats_by_day");
                run("overview.order_status");
                run("quality.summary_today");
            }
            return 0;
        }
        uint16_t port = 8686; size_t workers = 10;
        std::cout << "开启监听: 端口=" << port << ", 线程=" << workers << "\n";
        WebSocketServer::instance().acceptLoop(port, workers, [](SOCKET client, const std::string& msg){
            std::cout << "接受的数据内容" << msg << "\n";
            std::string resp = QueryRouter::instance().handle(msg);
            std::cout << "发送响应: socket=" << (uintptr_t)client << ", 长度=" << resp.size() << "\n";
            WebSocketServer::sendTextTo(client, resp);
        });
    }
    return 0;
}
