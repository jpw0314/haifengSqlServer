#pragma once
#include <string>
#include <vector>
#include <map>

struct QuerySpec {
    std::string id;
    std::string returns;
    std::vector<std::pair<std::string,std::string>> columns;
    std::string sql;
    std::vector<std::pair<std::string,std::string>> params;
    std::string param_mode;
    std::string wrap; // standard | none
};

class QueryRouter {
public:
    static QueryRouter& instance();
    bool load(const std::string& path);
    bool loadDir(const std::string& dir);
    std::string handle(const std::string& msg);
    size_t count() const;
private:
    QueryRouter();
    std::map<std::string, QuerySpec> specs;
};
