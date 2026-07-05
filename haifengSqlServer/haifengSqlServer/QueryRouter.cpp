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
#include <cmath>
#include "QueryRouter.h"
#include "DbClient.h"

std::wstring string_to_wstring(const std::string& str, UINT code_page = CP_UTF8) {
    if (str.empty()) return L"";
    
    int size_needed = MultiByteToWideChar(code_page, 0, str.c_str(), (int)str.size(), NULL, 0);
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(code_page, 0, str.c_str(), (int)str.size(), &wstr[0], size_needed);
    return wstr;
}
std::string wstring_to_utf8(const std::wstring& wstr) {
#ifdef _WIN32
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    std::string utf8_str(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &utf8_str[0], len, NULL, NULL);
    utf8_str.pop_back(); // 移除结尾的 '\0'
    return utf8_str;
#else
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    return converter.to_bytes(wstr);
#endif
}
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

// 辅助函数：将数据库返回的字符串转换为合法的 JSON 数字格式，并保留最多两位小数
static std::string formatJsonNumber(const std::string& val) {
    if (val.empty()) return "0";
    std::string s = val;
    // 去除两端可能存在的空白（有些驱动会补空格）
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "0";
    size_t last = s.find_last_not_of(" \t\r\n");
    s = s.substr(first, (last - first + 1));

    if (s[0] == '.') s = "0" + s;
    if (s.size() > 1 && s[0] == '-' && s[1] == '.') s = "-0" + s.substr(1);
    
    // 检查是否包含数字
    bool hasDigit = false;
    for (char c : s) { if (isdigit(c)) { hasDigit = true; break; } }
    if (!hasDigit) return "0";

    // 直接在字符串层面处理小数位数，避免浮点数精度问题
    size_t dotPos = s.find('.');
    if (dotPos == std::string::npos) {
        // 没有小数部分，直接返回
        return s;
    }

    // 有小数部分，进行四舍五入到两位小数
    std::string intPart = s.substr(0, dotPos);
    std::string fracPart = s.substr(dotPos + 1);

    // 截取或补全到至少两位小数
    while (fracPart.length() < 2) {
        fracPart += "0";
    }

    // 四舍五入处理：取前3位小数来判断
    char thirdDigit = '0';
    if (fracPart.length() >= 3) {
        thirdDigit = fracPart[2];
    }

    // 截取前两位小数
    std::string newFrac = fracPart.substr(0, 2);

    // 判断是否需要进位
    bool needCarry = (thirdDigit >= '5');
    if (needCarry) {
        // 从最后一位开始进位
        int i = 1; // 从第二位小数开始
        while (i >= 0 && needCarry) {
            if (newFrac[i] < '9') {
                newFrac[i]++;
                needCarry = false;
            } else {
                newFrac[i] = '0';
                if (i == 0) {
                    // 小数部分全进位了，需要进位到整数部分
                    // 解析整数部分并加1
                    long long intVal = 0;
                    try {
                        intVal = std::stoll(intPart);
                    } catch (...) {
                        // 如果整数部分太大，就不进位了
                        needCarry = false;
                    }
                    if (needCarry) {
                        if (intVal < 0) {
                            intVal--;
                        } else {
                            intVal++;
                        }
                        intPart = std::to_string(intVal);
                        needCarry = false;
                    }
                }
            }
            i--;
        }
    }

    // 构建最终结果
    std::string result = intPart;

    // 检查小数部分是否都是0
    bool allZero = true;
    for (char c : newFrac) {
        if (c != '0') {
            allZero = false;
            break;
        }
    }

    if (!allZero) {
        result += ".";
        result += newFrac;
        // 去除末尾的0（例如 99.50 -> 99.5）
        if (result.back() == '0') {
            result.pop_back();
        }
    }

    return result;
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
    std::wstring wsql = string_to_wstring(sql);
    // 执行 SQL 语句
    if (SQLExecDirectW(stmt, (SQLWCHAR*)wsql.c_str(), SQL_NTS) == SQL_SUCCESS) {
        success = true;
        // 标量查询：只返回第一行第一列
        if (q.returns == "scalar") {
            std::string v;
            if (SQLFetch(stmt) == SQL_SUCCESS) {
                SQLWCHAR buf[512] = {0};
                SQLLEN ind = 0;
                if (SQLGetData(stmt, 1, SQL_C_WCHAR, buf, sizeof(buf), &ind) == SQL_SUCCESS) {
                    if (ind != SQL_NULL_DATA) v = wstring_to_utf8(std::wstring(buf));
                }
            }
            bool num = (!q.columns.empty() && isNumericType(q.columns[0].second));
            innerJson = std::string("{") + "\"value\":" + (num ? formatJsonNumber(v) : (std::string("\"")+v+"\"")) + "}";
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
                    SQLWCHAR buf[512]={0};
                    SQLLEN ind=0;
                    SQLGetData(stmt, (SQLUSMALLINT)(i+1), SQL_C_WCHAR, buf, sizeof(buf) , &ind);
                    std::string val;
                    if (ind != SQL_NULL_DATA) val = wstring_to_utf8(std::wstring(buf));
                    
                    bool num = isNumericType(q.columns[i].second);
                    if (num) {
                        colArrays[i] += formatJsonNumber(val);
                    } else {
                        colArrays[i] += "\"" + val + "\"";
                    }
                }
            }
            for(auto& s : colArrays) s += "]";

            innerJson = "{";
            if (q.returnFormat == "chart_matrix") {
                if (!q.columns.empty()) {
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
                    SQLWCHAR buf[512]={0};
                    SQLLEN ind=0;
                    SQLGetData(stmt, (SQLUSMALLINT)(i+1), SQL_C_WCHAR, buf, sizeof(buf), &ind);
                    std::string val;
                    if (ind != SQL_NULL_DATA) val = wstring_to_utf8(std::wstring(buf));
                    
                    bool num = isNumericType(q.columns[i].second);
                    if (i > 0) innerJson += ",";
                    innerJson += std::string("\"") + q.columns[i].first + std::string("\":") + (num ? formatJsonNumber(val) : (std::string("\"")+val+"\""));
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
                    SQLWCHAR buf[512]={0}; SQLLEN ind=0;
                    SQLGetData(stmt, (SQLUSMALLINT)(i+1), SQL_C_WCHAR, buf, sizeof(buf), &ind);
                    std::string val;
                    if (ind != SQL_NULL_DATA) val = wstring_to_utf8(std::wstring(buf));
                    
                    bool num = isNumericType(q.columns[i].second);
                    if (i > 0) innerJson += ",";
                    innerJson += std::string("\"") + q.columns[i].first + std::string("\":") + (num ? formatJsonNumber(val) : (std::string("\"")+val+"\""));
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

    std::string datatype = "object";
    if (q.returnFormat == "list" || (q.returnFormat != "object" && q.returnFormat != "chart_columns" && q.returnFormat != "chart_matrix" && q.returns != "scalar")) {
        datatype = "list";
    }

    if (success) {
        return std::string("{\"code\":\"200\",\"message\":\"") + id + "\",\"datatype\":\"" + datatype + "\",\"data\":" + innerJson + "}";
    } else {
        return std::string("{\"code\":\"400\",\"message\":\"") + errorMsg + "\",\"datatype\":\"object\",\"data\":{}}";
    }
}
