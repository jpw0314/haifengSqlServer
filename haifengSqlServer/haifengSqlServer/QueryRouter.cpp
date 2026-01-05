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

// 辅助函数：去除字符串两端的空白字符
static std::string trim(const std::string& s) {
    size_t a = 0; while (a < s.size() && (s[a]==' '||s[a]=='\t' || s[a]=='\r')) a++;
    size_t b = s.size(); while (b > a && (s[b-1]==' '||s[b-1]=='\t'||s[b-1]=='\r')) b--;
    return s.substr(a, b-a);
}

// 辅助函数：判断类型字符串是否表示数值类型
static bool isNumericType(const std::string& t) {
    std::string s = t;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s.rfind("int",0)==0 || s.rfind("decimal",0)==0 || s.rfind("float",0)==0 || s.rfind("double",0)==0;
}

QueryRouter& QueryRouter::instance() { static QueryRouter inst; return inst; }
QueryRouter::QueryRouter() = default;
size_t QueryRouter::count() const { return specs.size(); }

// 辅助函数：解析 INI 文件并将查询规格存入 specs 映射表
static bool parseIniInto(std::map<std::string, QuerySpec>& specs, const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::string line; std::string cur;
    QuerySpec q;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        if (line[0]=='#' || line[0]==';') continue; // 跳过注释
        // 解析 Section，例如 [query_id]
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
        
        // 解析各个字段
        if (k == "returns") q.returns = v;
        else if (k == "return_format") q.returnFormat = v;
        else if (k == "sql") q.sql = v;
        else if (k == "param_mode") q.param_mode = v;
        else if (k == "wrap") q.wrap = v;
        else if (k == "columns") {
            // 解析列定义: col1:type1,col2:type2
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
            // 解析参数定义
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

// 加载指定目录下的所有 .ini 文件
bool QueryRouter::loadDir(const std::string& dir) {
    specs.clear();
    std::string pattern = dir;
    if (!pattern.empty() && pattern.back() != '\\' && pattern.back() != '/') pattern += "\\";
    pattern += "*.ini";
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string path = dir;
        if (!path.empty() && path.back() != '\\' && path.back() != '/') path += "\\";
        path += fd.cFileName;
        parseIniInto(specs, path);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return !specs.empty();
}

// 加载单个配置文件
bool QueryRouter::load(const std::string& path) {
    specs.clear();
    return parseIniInto(specs, path);
}

// 辅助函数：从 JSON 消息中提取指定字段的值（简单的字符串提取）
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

// 辅助函数：获取 ODBC 错误信息
static std::string odbcErrors(SQLSMALLINT type, SQLHANDLE handle) {
    std::string out;
    SQLSMALLINT i = 1;
    for (;;) {
        SQLCHAR state[6] = {0};
        SQLINTEGER native = 0;
        SQLCHAR text[512] = {0};
        SQLSMALLINT len = 0;
        SQLRETURN rc = SQLGetDiagRecA(type, handle, i, state, &native, text, (SQLSMALLINT)(sizeof(text)), &len);
        if (rc != SQL_SUCCESS) break;
        std::string s = std::string(reinterpret_cast<char*>(state));
        std::string m = std::string(reinterpret_cast<char*>(text));
        if (!out.empty()) out += " | ";
        out += std::string("SQLSTATE=") + s + std::string(", native=") + std::to_string(native) + std::string(", message=") + m;
        i++;
    }
    return out;
}

// 处理查询请求的核心逻辑
std::string QueryRouter::handle(const std::string& msg) {
    static std::mutex sqlMutex; // 简单的互斥锁，防止并发数据库访问冲突
    std::string id = getJsonString(msg, "query");
    if (id.empty()) {
        std::cout << "请求错误: 缺少 query 字段, 原始数据=" << msg << "\n";
        return std::string("{\"code\":\"400\",\"message\":\"missing query\",\"data\":{}}");
    }
    auto it = specs.find(id);
    if (it==specs.end()) {
        std::cout << "请求错误: 未知查询ID: " << id << "\n";
        return std::string("{\"code\":\"400\",\"message\":\"unknown query\",\"data\":{}}");
    }
    const QuerySpec& q = it->second;

    std::lock_guard<std::mutex> lock(sqlMutex);
    SQLHDBC hdbc = DbClient::instance().openTemp();
    if (hdbc == SQL_NULL_HDBC) {
        std::cout << "查询执行错误: 连接失败, 查询ID=" << id << "\n";
        return std::string("{\"code\":\"400\",\"message\":\"connect failed\",\"data\":{}}");
    }
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &stmt) != SQL_SUCCESS) {
        std::cout << "查询执行错误: 语句句柄分配失败, 查询ID=" << id << "\n";
        std::cout << odbcErrors(SQL_HANDLE_DBC, hdbc) << "\n";
        DbClient::instance().closeTemp(hdbc);
        return std::string("{\"code\":\"400\",\"message\":\"stmt alloc failed\",\"data\":{}}");
    }

    std::string sql = q.sql;
    std::string innerJson;
    bool success = false;
    std::string errorMsg;

    // 执行 SQL 语句
    if (SQLExecDirectA(stmt, (SQLCHAR*)sql.c_str(), SQL_NTS) == SQL_SUCCESS) {
        success = true;
        // 标量查询：只返回第一行第一列
        if (q.returns == "scalar") {
            std::string v;
            if (SQLFetch(stmt) == SQL_SUCCESS) {
                char buf[256] = {0};
                SQLLEN ind = 0;
                if (SQLGetData(stmt, 1, SQL_C_CHAR, buf, sizeof(buf), &ind) == SQL_SUCCESS) {
                    v = std::string(buf);
                }
            }
            std::string s = v;
            bool num = (!q.columns.empty() && isNumericType(q.columns[0].second));
            innerJson = std::string("{") + "\"value\":" + (num ? (s.empty()?"0":s) : (std::string("\"")+s+"\"")) + "}";
        } else if (q.returnFormat == "chart_columns" || q.returnFormat == "chart_matrix") {
            // 图表列式查询：将每一列的数据分别组织成数组
            std::vector<std::string> colArrays(q.columns.size(), "[");
            bool firstRow = true;
            for (;;) {
                if (SQLFetch(stmt) != SQL_SUCCESS) break;
                if (!firstRow) {
                    for(auto& s : colArrays) s += ",";
                }
                firstRow = false;
                
                for (size_t i = 0; i < q.columns.size(); ++i) {
                    char buf[256]={0}; SQLLEN ind=0;
                    SQLGetData(stmt, (SQLUSMALLINT)(i+1), SQL_C_CHAR, buf, sizeof(buf), &ind);
                    std::string val = std::string(buf);
                    bool num = isNumericType(q.columns[i].second);
                    colArrays[i] += (num ? (val.empty()?"0":val) : (std::string("\"")+val+"\""));
                }
            }
            for(auto& s : colArrays) s += "]";

            innerJson = "{";
            if (q.returnFormat == "chart_matrix") {
                // 矩阵格式：第一列作为 x 轴，其余列放入 values 数组中
                if (!q.columns.empty()) {
                    // 使用配置中的列名作为key，或者默认使用 "x" ? 
                    // 用户需求中，出库分析示例.json 使用 "x"。
                    // 如果配置了列名，我们优先使用列名。
                    innerJson += "\"" + q.columns[0].first + "\":" + colArrays[0];
                    if (q.columns.size() > 1) {
                        innerJson += ",\"values\":[";
                        for(size_t i=1; i<q.columns.size(); ++i) {
                            if (i > 1) innerJson += ",";
                            innerJson += colArrays[i];
                        }
                        innerJson += "]";
                    }
                }
            } else {
                // 普通列式：每一列作为一个数组，key 为列名
                for(size_t i=0; i<q.columns.size(); ++i) {
                    if (i > 0) innerJson += ",";
                    innerJson += "\"" + q.columns[i].first + "\":" + colArrays[i];
                }
            }
            innerJson += "}";
        } else if (q.returnFormat == "object") {
            // 单对象查询：只返回第一行作为对象
             if (SQLFetch(stmt) == SQL_SUCCESS) {
                innerJson = "{";
                for (size_t i = 0; i < q.columns.size(); ++i) {
                    char buf[256]={0}; SQLLEN ind=0;
                    SQLGetData(stmt, (SQLUSMALLINT)(i+1), SQL_C_CHAR, buf, sizeof(buf), &ind);
                    std::string val = std::string(buf);
                    bool num = isNumericType(q.columns[i].second);
                    if (i > 0) innerJson += ",";
                    innerJson += std::string("\"") + q.columns[i].first + std::string("\":") + (num ? (val.empty()?"0":val) : (std::string("\"")+val+"\""));
                }
                innerJson += "}";
             } else {
                innerJson = "{}";
             }
        } else {
            // 列表查询：返回所有行
            innerJson = "[";
            bool firstRow = true;
            for (;;) {
                if (SQLFetch(stmt) != SQL_SUCCESS) break;
                if (!firstRow) innerJson += ","; firstRow = false;
                innerJson += "{";
                for (size_t i = 0; i < q.columns.size(); ++i) {
                    char buf[256]={0}; SQLLEN ind=0;
                    SQLGetData(stmt, (SQLUSMALLINT)(i+1), SQL_C_CHAR, buf, sizeof(buf), &ind);
                    std::string val = std::string(buf);
                    bool num = isNumericType(q.columns[i].second);
                    innerJson += std::string("\"") + q.columns[i].first + std::string("\":") + (num ? (val.empty()?"0":val) : (std::string("\"")+val+"\""));
                    if (i+1<q.columns.size()) innerJson += ",";
                }
                innerJson += "}";
            }
            innerJson += "]";
        }
    } else {
        std::cout << "查询执行错误: SQL 执行失败, 查询ID=" << id << "\n";
        std::cout << "失败SQL=" << sql << "\n";
        std::cout << odbcErrors(SQL_HANDLE_STMT, stmt) << "\n";
        errorMsg = "exec failed";
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    DbClient::instance().closeTemp(hdbc);

    if (success) {
        return std::string("{\"code\":\"200\",\"message\":\"") + id + "\",\"data\":" + innerJson + "}";
    } else {
        return std::string("{\"code\":\"400\",\"message\":\"") + errorMsg + "\",\"data\":{}}";
    }
}
