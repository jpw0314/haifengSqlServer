#pragma once
#include <string>
#include <sql.h>
#include <sqlext.h>

class DbClient {
public:
    static DbClient& instance();
    DbClient(const DbClient&) = delete;
    DbClient& operator=(const DbClient&) = delete;
    DbClient(DbClient&&) = delete;
    DbClient& operator=(DbClient&&) = delete;

    std::string server;
    std::string uid;
    std::string pwd;
    std::string db;
    std::string usedDriver;
    SQLHENV env = SQL_NULL_HENV;
    SQLHDBC dbc = SQL_NULL_HDBC;

    bool init(const std::string& path);
    bool connect();
    std::string execScalar(const char* sql);
    SQLHDBC openTemp();
    void closeTemp(SQLHDBC h);
    void close();

private:
    DbClient();
};
