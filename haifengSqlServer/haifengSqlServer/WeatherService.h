#pragma once
#include <string>
#include <vector>

// 天气服务类，负责获取指定城市的天气信息
// 采用单例模式
class WeatherService {
public:
    // 获取单例实例
    static WeatherService& instance();
    
    // 获取指定城市的天气信息，返回格式化的字符串
    // city: 城市名称（中文）
    std::string getWeather(const std::string& city);

private:
    WeatherService() = default;
    ~WeatherService() = default;
    WeatherService(const WeatherService&) = delete;
    WeatherService& operator=(const WeatherService&) = delete;

    // 执行 HTTP GET 请求
    // domain: 域名
    // path: 请求路径
    std::string performGetRequest(const std::string& domain, const std::string& path);
    
    // 简单的 JSON 值提取
    // json: JSON 字符串
    // key: 要提取的键名
    std::string extractJsonValue(const std::string& json, const std::string& key);
    
};
