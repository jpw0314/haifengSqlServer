#include "WeatherService.h"
#include <windows.h>
#include <wininet.h>
#include <iostream>
#include <sstream>
#include <iomanip>

WeatherService& WeatherService::instance() {
    static WeatherService instance;
    return instance;
}

// 使用 WinInet API 执行 HTTP GET 请求
std::string WeatherService::performGetRequest(const std::string& domain, const std::string& path) {
    HINTERNET hInternet = NULL, hConnect = NULL, hRequest = NULL;
    std::string response;

    // 1. 初始化 WinInet
    hInternet = InternetOpenA("WeatherService/1.0", 
                              INTERNET_OPEN_TYPE_PRECONFIG, 
                              NULL, NULL, 0);

    if (hInternet) {
        // 2. 连接到服务器 (HTTPS 默认端口)
        hConnect = InternetConnectA(hInternet, domain.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 
                                    NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    }

    if (hConnect) {
        // 3. 创建 HTTP 请求
        hRequest = HttpOpenRequestA(hConnect, "GET", path.c_str(),
                                    NULL, NULL, NULL, 
                                    INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD, 0);
    }

    if (hRequest) {
        // 4. 发送请求
        if (HttpSendRequestA(hRequest, NULL, 0, NULL, 0)) {
            // 5. 读取响应数据
            char buffer[4096];
            DWORD bytesRead = 0;
            while (InternetReadFile(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
                response.append(buffer, bytesRead);
            }
        }
    }

    // 清理资源
    if (hRequest) InternetCloseHandle(hRequest);
    if (hConnect) InternetCloseHandle(hConnect);
    if (hInternet) InternetCloseHandle(hInternet);

    return response;
}

// 辅助函数：从 JSON 字符串中提取指定键的值
std::string WeatherService::extractJsonValue(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\":";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return "";

    pos += searchKey.length();
    
    // 跳过空白字符
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
        pos++;
    }

    if (pos >= json.length()) return "";

    // 如果值是字符串，提取引号内的内容
    if (json[pos] == '\"') {
        size_t end = json.find("\"", pos + 1);
        if (end == std::string::npos) return "";
        return json.substr(pos + 1, end - pos - 1);
    }
    // 如果值是数字、布尔值或 null，直接提取
    else {
        size_t end = pos;
        while (end < json.length() && (isdigit(json[end]) || json[end] == '.' || json[end] == '-' || json[end] == 'e' || json[end] == 'E' || isalpha(json[end]))) {
            end++;
        }
        return json.substr(pos, end - pos);
    }
}

// 获取天气信息的主流程
std::string WeatherService::getWeather(const std::string& city) {
    // 1. 地理编码：获取城市的经纬度
    // API: https://geocoding-api.open-meteo.com/v1/search?name={city}&count=1&language=zh&format=json
    
    std::string geoDomain = "geocoding-api.open-meteo.com";
    std::string geoPath = "/v1/search?name=" + city + "&count=1&language=zh&format=json";

    std::string geoResponse = performGetRequest(geoDomain, geoPath);
    
    if (geoResponse.empty()) return "Error: Failed to fetch location data.";

    // 解析经纬度
    if (geoResponse.find("\"results\"") == std::string::npos) {
        return "Error: City not found.";
    }

    std::string lat = extractJsonValue(geoResponse, "latitude");
    std::string lon = extractJsonValue(geoResponse, "longitude");

    if (lat.empty() || lon.empty()) return "Error: Could not parse location data.";

    // 2. 天气查询：根据经纬度获取当前天气
    // API: https://api.open-meteo.com/v1/forecast?latitude={lat}&longitude={lon}&current_weather=true
    
    std::string weatherDomain = "api.open-meteo.com";
    std::string weatherPath = "/v1/forecast?latitude=" + lat + "&longitude=" + lon + "&current_weather=true";

    std::string weatherResponse = performGetRequest(weatherDomain, weatherPath);

    if (weatherResponse.empty()) return "Error: Failed to fetch weather data.";

    size_t currentPos = weatherResponse.find("\"current_weather\"");
    if (currentPos == std::string::npos) return "Error: No weather data.";
    
    std::string currentJson = weatherResponse.substr(currentPos);
    
    std::string temp = extractJsonValue(currentJson, "temperature");
    std::string windspeed = extractJsonValue(currentJson, "windspeed");
    std::string code = extractJsonValue(currentJson, "weathercode");

    // 3. 解析天气代码
    std::string weatherDesc = "Unknown";
    int iCode = -1;
    try {
        if (!code.empty()) iCode = std::stoi(code);
    } catch(...) {}

    if (iCode == 0) weatherDesc = "晴朗";
    else if (iCode >= 1 && iCode <= 3) weatherDesc = "多云";
    else if (iCode == 45 || iCode == 48) weatherDesc = "雾";
    else if (iCode >= 51 && iCode <= 55) weatherDesc = "毛毛雨";
    else if (iCode >= 61 && iCode <= 67) weatherDesc = "雨";
    else if (iCode >= 71 && iCode <= 77) weatherDesc = "雪";
    else if (iCode >= 80 && iCode <= 82) weatherDesc = "阵雨";
    else if (iCode >= 95) weatherDesc = "雷雨";
    
    // 4. 格式化输出
    std::stringstream ss;
    ss << "城市: " << city << "\n";
    ss << "天气: " << weatherDesc << "\n";
    ss << "温度: " << temp << "°C\n";
    ss << "风速: " << windspeed << " km/h";

    return (ss.str());
}
