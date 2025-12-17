#include <windows.h>
#include <fstream>
#include <string>
#include "DbClient.h"

static void printDiag(SQLSMALLINT ht, SQLHANDLE h) {
    SQLCHAR state[6];
    SQLINTEGER native;
    SQLCHAR msg[512];
    SQLSMALLINT len;
    SQLSMALLINT i = 1;
    while (SQLGetDiagRecA(ht, h, i, state, &native, msg, (SQLSMALLINT)(sizeof(msg)), &len) == SQL_SUCCESS) {
        i++;
    }
}

static bool tryConnectWith(SQLHDBC dbc, const char* driver, const std::string& server, const std::string& uid, const std::string& pwd, const std::string& db) {
    SQLCHAR out[512];
    SQLSMALLINT outLen = 0;
    std::string serverPart = server;
    std::string drv(driver);
    if (drv == "ODBC Driver 18 for SQL Server" || drv == "ODBC Driver 17 for SQL Server") {
        if (serverPart.rfind("tcp:", 0) != 0) serverPart = "tcp:" + serverPart;
    }
    std::string cs = std::string("DRIVER={") + driver + "};SERVER=" + serverPart;
    if (!db.empty()) cs += ";Database=" + db;
    if (std::string(driver) == "SQL Server") {
        if (!uid.empty() && !pwd.empty()) cs += ";UID=" + uid + ";PWD=" + pwd;
        else cs += ";Trusted_Connection=yes";
        cs += ";Network=dbmssocn";
    } else {
        if (!uid.empty() && !pwd.empty()) cs += ";UID=" + uid + ";PWD=" + pwd;
        else cs += ";Trusted_Connection=yes";
        cs += ";Encrypt=yes;TrustServerCertificate=yes";
    }
    SQLRETURN r = SQLDriverConnectA(dbc, NULL, (SQLCHAR*)cs.c_str(), SQL_NTS, out, (SQLSMALLINT)(sizeof(out)), &outLen, SQL_DRIVER_NOPROMPT);
    return SQL_SUCCEEDED(r);
}

static std::string execScalarString(SQLHDBC dbc, const char* sql) {
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt) != SQL_SUCCESS) return std::string();
    std::string res;
    if (SQLExecDirectA(stmt, (SQLCHAR*)sql, SQL_NTS) == SQL_SUCCESS) {
        if (SQLFetch(stmt) == SQL_SUCCESS) {
            char buf[256] = {0};
            SQLLEN ind = 0;
            if (SQLGetData(stmt, 1, SQL_C_CHAR, buf, sizeof(buf), &ind) == SQL_SUCCESS) {
                res = std::string(buf);
            }
        }
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return res;
}

DbClient& DbClient::instance() { static DbClient inst; return inst; }
DbClient::DbClient() = default;

bool DbClient::init(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        if (line[0] == '#' || line[0] == ';') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        std::string v = line.substr(eq + 1);
        auto trim = [](std::string s){
            size_t a = 0; while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) a++;
            size_t b = s.size(); while (b > a && (s[b-1] == ' ' || s[b-1] == '\t' || s[b-1] == '\r')) b--;
            return s.substr(a, b - a);
        };
        k = trim(k); v = trim(v);
        if (k == "server") server = (v);
        else if (k == "user" || k == "uid") uid = (v);
        else if (k == "password" || k == "pwd") pwd = (v);
        else if (k == "database" || k == "db") db = (v);
    }
    return !server.empty();
}

bool DbClient::connect() {
    if (SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env) != SQL_SUCCESS) return false;
    SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0);
    if (SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc) != SQL_SUCCESS) { SQLFreeHandle(SQL_HANDLE_ENV, env); return false; }
    SQLSetConnectAttr(dbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);
    SQLSetConnectAttr(dbc, SQL_ATTR_CONNECTION_TIMEOUT, (SQLPOINTER)10, 0);
    bool ok = false;
    if (tryConnectWith(dbc, "ODBC Driver 18 for SQL Server", server, uid, pwd, db)) { ok = true; usedDriver = "ODBC Driver 18 for SQL Server"; }
    else if (tryConnectWith(dbc, "ODBC Driver 17 for SQL Server", server, uid, pwd, db)) { ok = true; usedDriver = "ODBC Driver 17 for SQL Server"; }
    else if (tryConnectWith(dbc, "SQL Server", server, uid, pwd, db)) { ok = true; usedDriver = "SQL Server"; }
    if (!ok) {
        printDiag(SQL_HANDLE_DBC, dbc);
        SQLFreeHandle(SQL_HANDLE_DBC, dbc);
        SQLFreeHandle(SQL_HANDLE_ENV, env);
        dbc = SQL_NULL_HDBC; env = SQL_NULL_HENV;
        return false;
    }
    return true;
}

std::string DbClient::execScalar(const char* sql) { return execScalarString(dbc, sql); }

SQLHDBC DbClient::openTemp() {
    if (env == SQL_NULL_HENV) {
        if (SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env) != SQL_SUCCESS) return SQL_NULL_HDBC;
        SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0);
    }
    SQLHDBC h = SQL_NULL_HDBC;
    if (SQLAllocHandle(SQL_HANDLE_DBC, env, &h) != SQL_SUCCESS) return SQL_NULL_HDBC;
    SQLSetConnectAttr(h, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);
    SQLSetConnectAttr(h, SQL_ATTR_CONNECTION_TIMEOUT, (SQLPOINTER)10, 0);
    bool ok = false;
    if (!usedDriver.empty()) {
        ok = tryConnectWith(h, usedDriver.c_str(), server, uid, pwd, db);
    } else {
        if (tryConnectWith(h, "ODBC Driver 18 for SQL Server", server, uid, pwd, db)) ok = true;
        else if (tryConnectWith(h, "ODBC Driver 17 for SQL Server", server, uid, pwd, db)) ok = true;
        else if (tryConnectWith(h, "SQL Server", server, uid, pwd, db)) ok = true;
    }
    if (!ok) { SQLFreeHandle(SQL_HANDLE_DBC, h); return SQL_NULL_HDBC; }
    return h;
}

void DbClient::closeTemp(SQLHDBC h) {
    if (h) { SQLDisconnect(h); SQLFreeHandle(SQL_HANDLE_DBC, h); }
}

void DbClient::close() {
    if (dbc) { SQLDisconnect(dbc); SQLFreeHandle(SQL_HANDLE_DBC, dbc); dbc = SQL_NULL_HDBC; }
    if (env) { SQLFreeHandle(SQL_HANDLE_ENV, env); env = SQL_NULL_HENV; }
}
