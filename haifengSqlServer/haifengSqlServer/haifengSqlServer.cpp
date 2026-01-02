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
#include "WeatherService.h"
#include "QueryRouter.h"


static void runConnectivityTest(DbClient& cli) {
    std::string line = std::string("Connected: Server=") + cli.server;
    if (!cli.db.empty()) line += std::string(" Database=") + cli.db;
    line += std::string(" Driver=") + cli.usedDriver;
    if (!cli.uid.empty()) line += std::string(" User=") + cli.uid; else line += " User=Windows Authentication";
    std::cout << line << "\n";
    SQLCHAR name[256];
    SQLCHAR ver[256];
    SQLSMALLINT n1 = 0, n2 = 0;
    SQLGetInfo(cli.dbc, SQL_DBMS_NAME, name, sizeof(name), &n1);
    SQLGetInfo(cli.dbc, SQL_DBMS_VER, ver, sizeof(ver), &n2);
    std::cout << "DBMS: " << std::string(reinterpret_cast<const char*>(name), n1) << " " << std::string(reinterpret_cast<const char*>(ver), n2) << "\n";
}


int main(int argc, char** argv) {
    //SetConsoleOutputCP(CP_UTF8);
    //system("chcp 936");
    DbClient& cli = DbClient::instance();
    if (!cli.init("dbconfig.ini")) { std::cout << "Read config failed\n"; return 1; }
    if (!cli.connect()) { std::cout << "Connection failed\n"; return 1; }
    std::cout << "连接数据库成功"<<std::endl;

    // 测试天气服务
    std::cout << "正在获取上海的天气信息...\n";
    std::string weatherInfo = WeatherService::instance().getWeather("guangzhou");
    std::cout << weatherInfo << "\n";

    {
        if (!QueryRouter::instance().loadDir("queries")) 
        {
            QueryRouter::instance().load("overview.ini");
            QueryRouter::instance().load("production.ini");
            QueryRouter::instance().load("quality.ini");
            QueryRouter::instance().load("warehouse.ini");
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
            std::cout << "发送的数据内容" << resp << std::endl;
            WebSocketServer::sendTextTo(client, resp);
        });
    }
    return 0;
}
