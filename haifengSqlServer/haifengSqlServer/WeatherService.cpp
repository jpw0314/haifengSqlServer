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

std::string WeatherService::performGetRequest(const std::string& domain, const std::string& path) {
    HINTERNET hInternet = NULL, hConnect = NULL, hRequest = NULL;
    std::string response;

    // 1. Initialize WinInet
    hInternet = InternetOpenA("WeatherService/1.0", 
                              INTERNET_OPEN_TYPE_PRECONFIG, 
                              NULL, NULL, 0);

    if (hInternet) {
        // 2. Connect to the server
        hConnect = InternetConnectA(hInternet, domain.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 
                                    NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    }

    if (hConnect) {
        // 3. Create the request
        hRequest = HttpOpenRequestA(hConnect, "GET", path.c_str(),
                                    NULL, NULL, NULL, 
                                    INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD, 0);
    }

    if (hRequest) {
        // 4. Send the request
        if (HttpSendRequestA(hRequest, NULL, 0, NULL, 0)) {
            // 5. Read the response
            char buffer[4096];
            DWORD bytesRead = 0;
            while (InternetReadFile(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
                response.append(buffer, bytesRead);
            }
        }
    }

    // Cleanup
    if (hRequest) InternetCloseHandle(hRequest);
    if (hConnect) InternetCloseHandle(hConnect);
    if (hInternet) InternetCloseHandle(hInternet);

    return response;
}

std::string WeatherService::extractJsonValue(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\":";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return "";

    pos += searchKey.length();
    
    // Skip whitespace
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
        pos++;
    }

    if (pos >= json.length()) return "";

    // Check if value is a string
    if (json[pos] == '\"') {
        size_t end = json.find("\"", pos + 1);
        if (end == std::string::npos) return "";
        return json.substr(pos + 1, end - pos - 1);
    }
    // Check if value is a number or boolean or null
    else {
        size_t end = pos;
        while (end < json.length() && (isdigit(json[end]) || json[end] == '.' || json[end] == '-' || json[end] == 'e' || json[end] == 'E' || isalpha(json[end]))) {
            end++;
        }
        return json.substr(pos, end - pos);
    }
}

std::string WeatherService::Utf8ToGbk(const std::string& str) {
    if (str.empty()) return "";
    
    // UTF-8 -> Wide
    int wLen = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
    if (wLen <= 0) return str;
    std::vector<wchar_t> wBuf(wLen);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, wBuf.data(), wLen);

    // Wide -> GBK (CP_ACP or 936)
    int aLen = WideCharToMultiByte(CP_ACP, 0, wBuf.data(), -1, NULL, 0, NULL, NULL);
    if (aLen <= 0) return str;
    std::vector<char> aBuf(aLen);
    WideCharToMultiByte(CP_ACP, 0, wBuf.data(), -1, aBuf.data(), aLen, NULL, NULL);

    return std::string(aBuf.data());
}

std::string WeatherService::getWeather(const std::string& city) {
    // 1. Geocoding: Get Lat/Lon for the city
    // https://geocoding-api.open-meteo.com/v1/search?name={city}&count=1&language=zh&format=json
    
    std::string geoDomain = "geocoding-api.open-meteo.com";
    std::string geoPath = "/v1/search?name=" + city + "&count=1&language=zh&format=json";

    std::string geoResponse = performGetRequest(geoDomain, geoPath);
    
    if (geoResponse.empty()) return "Error: Failed to fetch location data.";

    // Parse Lat/Lon
    if (geoResponse.find("\"results\"") == std::string::npos) {
        return "Error: City not found.";
    }

    std::string lat = extractJsonValue(geoResponse, "latitude");
    std::string lon = extractJsonValue(geoResponse, "longitude");

    if (lat.empty() || lon.empty()) return "Error: Could not parse location data.";

    // 2. Weather: Get current weather
    // https://api.open-meteo.com/v1/forecast?latitude={lat}&longitude={lon}&current_weather=true
    
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

    // Weather codes interpretation
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
    
    std::stringstream ss;
    ss << "城市: " << city << "\n";
    ss << "天气: " << weatherDesc << "\n";
    ss << "温度: " << temp << "°C\n";
    ss << "风速: " << windspeed << " km/h";

    return (ss.str());
}
