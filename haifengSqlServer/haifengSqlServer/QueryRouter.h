#pragma once
#include <string>
#include <vector>
#include <map>

// 查询规格说明结构体，对应 .ini 配置文件中的一个查询定义
struct QuerySpec {
    std::string id;                                         // 查询唯一标识
    std::string returns;                                    // 返回类型: "scalar" (单值) 或 "array" (多行)
    std::vector<std::pair<std::string,std::string>> columns; // 结果列定义 (列名, 类型)
    std::string sql;                                        // SQL 查询语句
    std::vector<std::pair<std::string,std::string>> params; // 参数列表 (目前未完全使用)
    std::string param_mode;                                 // 参数模式
    std::string wrap;                                       // 包装模式: "standard" | "none"
    std::string returnFormat;                               // 返回数据格式: "list"(默认), "object", "chart_columns", "chart_matrix"
};

// 查询路由器类，负责加载查询配置并处理查询请求
// 采用单例模式
class QueryRouter {
public:
    // 获取单例实例
    static QueryRouter& instance();

    // 加载单个配置文件
    // path: 文件路径
    bool load(const std::string& path);

    // 加载目录下的所有 .ini 配置文件
    // dir: 目录路径
    bool loadDir(const std::string& dir);

    // 处理查询请求消息
    // msg: JSON 格式的请求消息
    // 返回: JSON 格式的响应消息
    std::string handle(const std::string& msg);

    // 获取已加载的查询数量
    size_t count() const;

private:
    QueryRouter();
    std::map<std::string, QuerySpec> specs; // 存储查询规格的映射表: id -> spec
};
