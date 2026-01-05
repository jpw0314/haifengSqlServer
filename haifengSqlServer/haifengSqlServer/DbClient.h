#pragma once
#include <string>
#include <sql.h>
#include <sqlext.h>

// 数据库客户端类，负责管理 ODBC 连接和执行 SQL 查询
// 采用单例模式
class DbClient {
public:
    // 获取单例实例
    static DbClient& instance();
    DbClient(const DbClient&) = delete;
    DbClient& operator=(const DbClient&) = delete;
    DbClient(DbClient&&) = delete;
    DbClient& operator=(DbClient&&) = delete;

    // 连接配置信息
    std::string server;
    std::string uid;
    std::string pwd;
    std::string db;
    std::string usedDriver; // 实际使用的 ODBC 驱动名称

    // ODBC 环境句柄和连接句柄
    SQLHENV env = SQL_NULL_HENV;
    SQLHDBC dbc = SQL_NULL_HDBC;

    // 初始化配置，从指定路径的 ini 文件读取
    bool init(const std::string& path);
    // 建立数据库连接
    bool connect();
    // 执行返回单个值的 SQL 查询
    std::string execScalar(const char* sql);
    // 打开一个新的临时连接（用于多线程环境）
    SQLHDBC openTemp();
    // 关闭临时连接
    void closeTemp(SQLHDBC h);
    // 关闭主连接并清理资源
    void close();

private:
    DbClient();
};
