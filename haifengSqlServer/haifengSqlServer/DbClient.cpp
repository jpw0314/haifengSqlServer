#include <windows.h>
#include <fstream>
#include <string>
#include "DbClient.h"

static std::wstring u8tow(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring out;
    out.resize(n);
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], n);
    if (len > 0) out.resize(len - 1); else out.clear();
    return out;
}

static void printDiag(SQLSMALLINT ht, SQLHANDLE h) {
    SQLWCHAR state[6];
    SQLINTEGER native;
    SQLWCHAR msg[512];
    SQLSMALLINT len;
    SQLSMALLINT i = 1;
    while (SQLGetDiagRecW(ht, h, i, state, &native, msg, (SQLSMALLINT)(sizeof(msg) / sizeof(SQLWCHAR)), &len) == SQL_SUCCESS) {
        i++;
    }
}

static bool tryConnectWith(SQLHDBC dbc, const wchar_t* driver, const std::wstring& server, const std::wstring& uid, const std::wstring& pwd, const std::wstring& db) {
    SQLWCHAR out[512];
    SQLSMALLINT outLen = 0;
    std::wstring serverPart = server;
    std::wstring drv(driver);
    if (drv == L"ODBC Driver 18 for SQL Server" || drv == L"ODBC Driver 17 for SQL Server") {
        if (serverPart.rfind(L"tcp:", 0) != 0) serverPart = L"tcp:" + serverPart;
    }
    std::wstring cs = std::wstring(L"DRIVER={") + driver + L"};SERVER=" + serverPart;
    if (!db.empty()) cs += L";Database=" + db;
    if (std::wstring(driver) == L"SQL Server") {
        if (!uid.empty() && !pwd.empty()) cs += L";UID=" + uid + L";PWD=" + pwd;
        else cs += L";Trusted_Connection=yes";
        cs += L";Network=dbmssocn";
    } else {
        if (!uid.empty() && !pwd.empty()) cs += L";UID=" + uid + L";PWD=" + pwd;
        else cs += L";Trusted_Connection=yes";
        cs += L";Encrypt=yes;TrustServerCertificate=yes";
    }
    SQLRETURN r = SQLDriverConnectW(dbc, NULL, (SQLWCHAR*)cs.c_str(), SQL_NTS, out, (SQLSMALLINT)(sizeof(out) / sizeof(SQLWCHAR)), &outLen, SQL_DRIVER_NOPROMPT);
    return SQL_SUCCEEDED(r);
}

static std::wstring execScalarString(SQLHDBC dbc, const wchar_t* sql) {
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt) != SQL_SUCCESS) return std::wstring();
    std::wstring res;
    if (SQLExecDirectW(stmt, (SQLWCHAR*)sql, SQL_NTS) == SQL_SUCCESS) {
        if (SQLFetch(stmt) == SQL_SUCCESS) {
            wchar_t buf[256] = {0};
            SQLLEN ind = 0;
            if (SQLGetData(stmt, 1, SQL_C_WCHAR, buf, sizeof(buf), &ind) == SQL_SUCCESS) {
                res = std::wstring(buf);
            }
        }
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return res;
}

DbClient& DbClient::instance() { static DbClient inst; return inst; }
DbClient::DbClient() = default;

bool DbClient::init(const std::wstring& path) {
    std::ifstream f;
    int n = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n > 0) {
        std::string p; p.resize(n);
        WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, &p[0], n, nullptr, nullptr);
        if (p.size() && p.back() == '\0') p.pop_back();
        f.open(p);
    }
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
        if (k == "server") server = u8tow(v);
        else if (k == "user" || k == "uid") uid = u8tow(v);
        else if (k == "password" || k == "pwd") pwd = u8tow(v);
        else if (k == "database" || k == "db") db = u8tow(v);
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
    if (tryConnectWith(dbc, L"ODBC Driver 18 for SQL Server", server, uid, pwd, db)) { ok = true; usedDriver = L"ODBC Driver 18 for SQL Server"; }
    else if (tryConnectWith(dbc, L"ODBC Driver 17 for SQL Server", server, uid, pwd, db)) { ok = true; usedDriver = L"ODBC Driver 17 for SQL Server"; }
    else if (tryConnectWith(dbc, L"SQL Server", server, uid, pwd, db)) { ok = true; usedDriver = L"SQL Server"; }
    if (!ok) {
        printDiag(SQL_HANDLE_DBC, dbc);
        SQLFreeHandle(SQL_HANDLE_DBC, dbc);
        SQLFreeHandle(SQL_HANDLE_ENV, env);
        dbc = SQL_NULL_HDBC; env = SQL_NULL_HENV;
        return false;
    }
    return true;
}

std::wstring DbClient::execScalar(const wchar_t* sql) { return execScalarString(dbc, sql); }

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
        if (tryConnectWith(h, L"ODBC Driver 18 for SQL Server", server, uid, pwd, db)) ok = true;
        else if (tryConnectWith(h, L"ODBC Driver 17 for SQL Server", server, uid, pwd, db)) ok = true;
        else if (tryConnectWith(h, L"SQL Server", server, uid, pwd, db)) ok = true;
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
