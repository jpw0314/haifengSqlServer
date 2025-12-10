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

    std::wstring server;
    std::wstring uid;
    std::wstring pwd;
    std::wstring db;
    std::wstring usedDriver;
    SQLHENV env = SQL_NULL_HENV;
    SQLHDBC dbc = SQL_NULL_HDBC;

    bool init(const std::wstring& path);
    bool connect();
    std::wstring execScalar(const wchar_t* sql);
    SQLHDBC openTemp();
    void closeTemp(SQLHDBC h);
    void close();

private:
    DbClient();
};
