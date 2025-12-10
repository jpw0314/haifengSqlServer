#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <windows.h>
#include <iostream>
#include <map>
#include <fstream>
#include <algorithm>
#include <mutex>
#include "QueryRouter.h"
#include "DbClient.h"

static std::wstring u8tow(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring out; out.resize(n);
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], n);
    if (len > 0) out.resize(len - 1); else out.clear();
    return out;
}

static std::string w2u8(const std::wstring& ws) {
    if (ws.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string out; out.resize(n);
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, &out[0], n, nullptr, nullptr);
    if (len > 0) out.resize(len - 1); else out.clear();
    return out;
}

static std::string trim(const std::string& s) {
    size_t a = 0; while (a < s.size() && (s[a]==' '||s[a]=='\t' || s[a]=='\r')) a++;
    size_t b = s.size(); while (b > a && (s[b-1]==' '||s[b-1]=='\t'||s[b-1]=='\r')) b--;
    return s.substr(a, b-a);
}

static bool isNumericType(const std::string& t) {
    std::string s = t;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s.rfind("int",0)==0 || s.rfind("decimal",0)==0 || s.rfind("float",0)==0 || s.rfind("double",0)==0;
}

QueryRouter& QueryRouter::instance() { static QueryRouter inst; return inst; }
QueryRouter::QueryRouter() = default;
size_t QueryRouter::count() const { return specs.size(); }

static bool parseIniInto(std::map<std::string, QuerySpec>& specs, const std::wstring& path) {
    std::ifstream f;
    int n = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n > 0) {
        std::string p; p.resize(n);
        WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, &p[0], n, nullptr, nullptr);
        if (p.size() && p.back()=='\0') p.pop_back();
        f.open(p);
    }
    if (!f.is_open()) return false;
    std::string line; std::string cur;
    QuerySpec q;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        if (line[0]=='#' || line[0]==';') continue;
        if (line.front()=='[' && line.back()==']') {
            if (!q.id.empty()) { specs[q.id] = q; q = QuerySpec(); }
            cur = line.substr(1, line.size()-2);
            q.id = cur;
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq));
        std::string v = trim(line.substr(eq+1));
        if (k == "returns") q.returns = v;
        else if (k == "sql") q.sql = v;
        else if (k == "param_mode") q.param_mode = v;
        else if (k == "wrap") q.wrap = v;
        else if (k == "columns") {
            q.columns.clear();
            size_t pos = 0;
            while (pos < v.size()) {
                size_t c = v.find(',', pos);
                std::string item = trim(v.substr(pos, c==std::string::npos ? std::string::npos : c-pos));
                size_t d = item.find(':');
                if (d!=std::string::npos) q.columns.emplace_back(trim(item.substr(0,d)), trim(item.substr(d+1)));
                pos = c==std::string::npos ? v.size() : c+1;
            }
        } else if (k == "params") {
            q.params.clear();
            if (!v.empty()) {
                size_t pos = 0;
                while (pos < v.size()) {
                    size_t c = v.find(',', pos);
                    std::string item = trim(v.substr(pos, c==std::string::npos ? std::string::npos : c-pos));
                    size_t d = item.find(':');
                    if (d!=std::string::npos) q.params.emplace_back(trim(item.substr(0,d)), trim(item.substr(d+1)));
                    pos = c==std::string::npos ? v.size() : c+1;
                }
            }
        }
    }
    if (!q.id.empty()) specs[q.id] = q;
    return true;
}

bool QueryRouter::loadDir(const std::wstring& dir) {
    specs.clear();
    std::wstring pattern = dir;
    if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/') pattern += L"\\";
    pattern += L"*.ini";
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring path = dir;
        if (!path.empty() && path.back() != L'\\' && path.back() != L'/') path += L"\\";
        path += fd.cFileName;
        parseIniInto(specs, path);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return !specs.empty();
}

bool QueryRouter::load(const std::wstring& path) {
    specs.clear();
    std::ifstream f;
    int n = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n > 0) {
        std::string p; p.resize(n);
        WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, &p[0], n, nullptr, nullptr);
        if (p.size() && p.back()=='\0') p.pop_back();
        f.open(p);
    }
    if (!f.is_open()) return false;
    std::string line; std::string cur;
    QuerySpec q;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        if (line[0]=='#' || line[0]==';') continue;
        if (line.front()=='[' && line.back()==']') {
            if (!q.id.empty()) { specs[q.id] = q; q = QuerySpec(); }
            cur = line.substr(1, line.size()-2);
            q.id = cur;
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq));
        std::string v = trim(line.substr(eq+1));
        if (k == "returns") q.returns = v;
        else if (k == "sql") q.sql = v;
        else if (k == "param_mode") q.param_mode = v;
        else if (k == "wrap") q.wrap = v;
        else if (k == "columns") {
            q.columns.clear();
            size_t pos = 0;
            while (pos < v.size()) {
                size_t c = v.find(',', pos);
                std::string item = trim(v.substr(pos, c==std::string::npos ? std::string::npos : c-pos));
                size_t d = item.find(':');
                if (d!=std::string::npos) q.columns.emplace_back(trim(item.substr(0,d)), trim(item.substr(d+1)));
                pos = c==std::string::npos ? v.size() : c+1;
            }
        } else if (k == "params") {
            q.params.clear();
            if (!v.empty()) {
                size_t pos = 0;
                while (pos < v.size()) {
                    size_t c = v.find(',', pos);
                    std::string item = trim(v.substr(pos, c==std::string::npos ? std::string::npos : c-pos));
                    size_t d = item.find(':');
                    if (d!=std::string::npos) q.params.emplace_back(trim(item.substr(0,d)), trim(item.substr(d+1)));
                    pos = c==std::string::npos ? v.size() : c+1;
                }
            }
        }
    }
    if (!q.id.empty()) specs[q.id] = q;
    return true;
}

static std::string getJsonString(const std::string& msg, const std::string& key) {
    std::string k = std::string("\"") + key + std::string("\"");
    size_t pos = msg.find(k);
    if (pos == std::string::npos) return std::string();
    size_t colon = msg.find(":", pos + k.size());
    if (colon == std::string::npos) return std::string();
    size_t v = colon + 1;
    while (v < msg.size() && (msg[v]==' ' || msg[v]=='\t' || msg[v]=='\r' || msg[v]=='\n')) v++;
    if (v >= msg.size() || msg[v] != '"') return std::string();
    v++;
    size_t e = msg.find("\"", v);
    if (e == std::string::npos) return std::string();
    return msg.substr(v, e - v);
}

static std::string odbcErrors(SQLSMALLINT type, SQLHANDLE handle) {
    std::string out;
    SQLSMALLINT i = 1;
    for (;;) {
        SQLWCHAR state[6] = {0};
        SQLINTEGER native = 0;
        SQLWCHAR text[512] = {0};
        SQLSMALLINT len = 0;
        SQLRETURN rc = SQLGetDiagRecW(type, handle, i, state, &native, text, (SQLSMALLINT)(sizeof(text)/sizeof(SQLWCHAR)), &len);
        if (rc != SQL_SUCCESS) break;
        std::string s = w2u8(std::wstring(state));
        std::string m = w2u8(std::wstring(text));
        if (!out.empty()) out += " | ";
        out += std::string("SQLSTATE=") + s + std::string(", native=") + std::to_string(native) + std::string(", message=") + m;
        i++;
    }
    return out;
}

std::string QueryRouter::handle(const std::string& msg) {
    static std::mutex sqlMutex;
    std::string id = getJsonString(msg, "query");
    if (id.empty()) {
        std::cout << "请求错误: 缺少 query 字段, 原始数据=" << msg << "\n";
        return std::string("{\"ok\":false,\"error\":\"missing query\"}");
    }
    auto it = specs.find(id);
    if (it==specs.end()) {
        std::cout << "请求错误: 未知查询ID: " << id << "\n";
        return std::string("{\"ok\":false,\"error\":\"unknown query\"}");
    }
    const QuerySpec& q = it->second;
    if (q.returns == "scalar") {
        std::lock_guard<std::mutex> lock(sqlMutex);
        SQLHDBC hdbc = DbClient::instance().openTemp();
        if (hdbc == SQL_NULL_HDBC) {
            std::cout << "查询执行错误: 连接失败, 查询ID=" << id << "\n";
            return std::string("{\"ok\":false,\"error\":\"connect failed\"}");
        }
        SQLHSTMT stmt = SQL_NULL_HSTMT;
        if (SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &stmt) != SQL_SUCCESS) {
            std::cout << "查询执行错误: 语句句柄分配失败, 查询ID=" << id << "\n";
            std::cout << odbcErrors(SQL_HANDLE_DBC, hdbc) << "\n";
            DbClient::instance().closeTemp(hdbc);
            return std::string("{\"ok\":false,\"error\":\"stmt alloc failed\"}");
        }
        std::wstring wsql = u8tow(q.sql);
        std::wstring v;
        std::string out;
        if (SQLExecDirectW(stmt, (SQLWCHAR*)wsql.c_str(), SQL_NTS) == SQL_SUCCESS) {
            if (SQLFetch(stmt) == SQL_SUCCESS) {
                wchar_t buf[256] = {0};
                SQLLEN ind = 0;
                if (SQLGetData(stmt, 1, SQL_C_WCHAR, buf, sizeof(buf), &ind) == SQL_SUCCESS) {
                    v = std::wstring(buf);
                }
            }
            std::string s = w2u8(v);
            bool num = (!q.columns.empty() && isNumericType(q.columns[0].second));
            if (q.wrap == "none") {
                out = std::string("{") + "\"value\":" + (num ? s : (std::string("\"")+s+"\"")) + "}";
            } else {
                out = std::string("{\"ok\":true,\"data\":{") + "\"value\":" + (num ? s : (std::string("\"")+s+"\"")) + "}}";
            }
        } else {
            std::cout << "查询执行错误: SQL 执行失败, 查询ID=" << id << "\n";
            std::cout << "失败SQL=" << w2u8(wsql) << "\n";
            std::cout << odbcErrors(SQL_HANDLE_STMT, stmt) << "\n";
            out = std::string("{\"ok\":false,\"error\":\"exec failed\"}");
        }
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        DbClient::instance().closeTemp(hdbc);
        return out;
    } else {
        std::lock_guard<std::mutex> lock(sqlMutex);
        SQLHDBC hdbc = DbClient::instance().openTemp();
        if (hdbc == SQL_NULL_HDBC) {
            std::cout << "查询执行错误: 连接失败, 查询ID=" << id << "\n";
            return std::string("{\"ok\":false,\"error\":\"connect failed\"}");
        }
        SQLHSTMT stmt = SQL_NULL_HSTMT;
        if (SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &stmt) != SQL_SUCCESS) {
            std::cout << "查询执行错误: 语句句柄分配失败, 查询ID=" << id << "\n";
            std::cout << odbcErrors(SQL_HANDLE_DBC, hdbc) << "\n";
            DbClient::instance().closeTemp(hdbc);
            return std::string("{\"ok\":false,\"error\":\"stmt alloc failed\"}");
        }
        std::wstring wsql = u8tow(q.sql);
        std::string out;
        if (SQLExecDirectW(stmt, (SQLWCHAR*)wsql.c_str(), SQL_NTS) == SQL_SUCCESS) {
            if (q.wrap == "none") out = "["; else out = std::string("{\"ok\":true,\"data\":[");
            bool firstRow = true;
            for (;;) {
                if (SQLFetch(stmt) != SQL_SUCCESS) break;
                if (!firstRow) out += ","; firstRow = false;
                out += "{";
                for (size_t i = 0; i < q.columns.size(); ++i) {
                    wchar_t buf[256]={0}; SQLLEN ind=0;
                    SQLGetData(stmt, (SQLUSMALLINT)(i+1), SQL_C_WCHAR, buf, sizeof(buf), &ind);
                    std::string val = w2u8(std::wstring(buf));
                    bool num = isNumericType(q.columns[i].second);
                    out += std::string("\"") + q.columns[i].first + std::string("\":") + (num ? (val.empty()?"0":val) : (std::string("\"")+val+"\""));
                    if (i+1<q.columns.size()) out += ",";
                }
                out += "}";
            }
            if (q.wrap == "none") out += "]"; else out += "]}";
        } else {
            std::cout << "查询执行错误: SQL 执行失败, 查询ID=" << id << "\n";
            std::cout << "失败SQL=" << w2u8(wsql) << "\n";
            std::cout << odbcErrors(SQL_HANDLE_STMT, stmt) << "\n";
            out = std::string("{\"ok\":false,\"error\":\"exec failed\"}");
        }
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        DbClient::instance().closeTemp(hdbc);
        return out;
    }
}
