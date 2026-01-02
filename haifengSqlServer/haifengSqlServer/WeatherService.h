#pragma once
#include <string>
#include <vector>

class WeatherService {
public:
    static WeatherService& instance();
    
    // 获取指定城市的天气信息，返回格式化的字符串
    std::string getWeather(const std::string& city);

private:
    WeatherService() = default;
    ~WeatherService() = default;
    WeatherService(const WeatherService&) = delete;
    WeatherService& operator=(const WeatherService&) = delete;

    // 执行 HTTP GET 请求
    std::string performGetRequest(const std::string& domain, const std::string& path);
    
    // 简单的 JSON 值提取
    std::string extractJsonValue(const std::string& json, const std::string& key);
    
    // 字符串转换帮助函数
    std::string Utf8ToGbk(const std::string& str);
};
