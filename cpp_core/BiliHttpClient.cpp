#include "BiliHttpClient.hpp"
#include <windows.h>
#include <winhttp.h>
#include <sstream>
#include <iomanip>
#include <iostream>

#pragma comment(lib, "winhttp.lib")

BiliHttpClient::BiliHttpClient() {
    m_userAgent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36";
}

BiliHttpClient::~BiliHttpClient() {}

// 探测当前用户系统代理 (Clash / v2ray 等通过系统设置注入的代理)
// WINHTTP_ACCESS_TYPE_DEFAULT_PROXY 只认 netsh winhttp 代理, 与用户常用的
// IE/系统代理(Clash)是两套独立配置, 必须显式读取 IE 代理配置才能穿透海外代理。
bool BiliHttpClient::detectSystemProxy(std::wstring &proxyServer) {
    proxyServer.clear();
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG cfg;
    ZeroMemory(&cfg, sizeof(cfg));
    if (!WinHttpGetIEProxyConfigForCurrentUser(&cfg)) return false;
    bool found = (cfg.lpszProxy != nullptr && wcslen(cfg.lpszProxy) > 0);
    if (found) proxyServer = cfg.lpszProxy;
    if (cfg.lpszAutoConfigUrl) GlobalFree(cfg.lpszAutoConfigUrl);
    if (cfg.lpszProxyBypass) GlobalFree(cfg.lpszProxyBypass);
    if (cfg.lpszProxy) GlobalFree(cfg.lpszProxy);
    return found;
}

void BiliHttpClient::setSessData(const std::string &sessData) {
    m_sessData = sessData;
}

std::string BiliHttpClient::getSessData() const {
    return m_sessData;
}

void BiliHttpClient::setCookie(const std::string &cookie) {
    m_fullCookie = cookie;
}

std::string BiliHttpClient::urlEncode(const std::string &value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (unsigned char c : value) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << std::uppercase << int(c);
        }
    }
    return escaped.str();
}

std::string BiliHttpClient::urlDecode(const std::string &value) {
    std::string result;
    result.reserve(value.length());

    for (size_t i = 0; i < value.length(); ++i) {
        if (value[i] == '%' && i + 2 < value.length()) {
            int h1 = value[i + 1];
            int h2 = value[i + 2];
            auto hexToInt = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return 0;
            };
            result += static_cast<char>((hexToInt(h1) << 4) | hexToInt(h2));
            i += 2;
        } else if (value[i] == '+') {
            result += ' ';
        } else {
            result += value[i];
        }
    }
    return result;
}

bool BiliHttpClient::get(const std::string &url, std::string &responseBody, 
                         const std::map<std::string, std::string> &headers, 
                         int timeoutSec) {
    std::vector<std::string> unusedCookies;
    return get(url, responseBody, unusedCookies, headers, timeoutSec);
}

bool BiliHttpClient::get(const std::string &url, std::string &responseBody,
                         std::vector<std::string> &responseCookies,
                         const std::map<std::string, std::string> &headers,
                         int timeoutSec) {
    return request(L"GET", url, "", headers, responseCookies, responseBody, timeoutSec);
}

bool BiliHttpClient::post(const std::string &url, const std::string &body, std::string &responseBody,
                          const std::map<std::string, std::string> &headers,
                          int timeoutSec) {
    std::vector<std::string> unusedCookies;
    return request(L"POST", url, body, headers, unusedCookies, responseBody, timeoutSec);
}

bool BiliHttpClient::post(const std::string &url, const std::string &body, std::string &responseBody,
                          std::vector<std::string> &responseCookies,
                          const std::map<std::string, std::string> &headers,
                          int timeoutSec) {
    return request(L"POST", url, body, headers, responseCookies, responseBody, timeoutSec);
}

// WinHTTP 通用请求内核 (GET / POST 共用)
// 覆盖: 系统代理(IE/Clash)穿透、GZIP 自动解压、302 自动跟随、自定义 Cookie 头、
//       Set-Cookie 响应头提取、POST 表单体发送 (自动 Content-Type / Content-Length)。
bool BiliHttpClient::request(const wchar_t *method, const std::string &url, const std::string &body,
                             const std::map<std::string, std::string> &headers,
                             std::vector<std::string> &responseCookies,
                             std::string &responseBody, int timeoutSec) {
    responseCookies.clear();
    std::wstring wUrl(url.begin(), url.end());

    URL_COMPONENTS urlComp = { 0 };
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = (DWORD)-1;
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;
    urlComp.dwExtraInfoLength = (DWORD)-1;

    if (!WinHttpCrackUrl(wUrl.c_str(), (DWORD)wUrl.length(), 0, &urlComp)) {
        return false;
    }

    std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength + urlComp.dwExtraInfoLength);
    INTERNET_PORT port = urlComp.nPort;
    bool isHttps = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);

    std::wstring wUserAgent(m_userAgent.begin(), m_userAgent.end());
    std::wstring proxyServer;
    bool hasProxy = detectSystemProxy(proxyServer);
    HINTERNET hSession = WinHttpOpen(wUserAgent.c_str(),
                                     hasProxy ? WINHTTP_ACCESS_TYPE_NAMED_PROXY : WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     hasProxy ? proxyServer.c_str() : WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    // 设置超时时间 (毫秒)
    DWORD timeoutMs = timeoutSec * 1000;
    WinHttpSetTimeouts(hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    // 启用 GZIP / Deflate 自动解压
    DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
    WinHttpSetOption(hSession, WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof(decompression));

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD flags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, method, path.c_str(), NULL,
                                           WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // 设置自动跟随重定向
    DWORD redirectOption = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectOption, sizeof(redirectOption));

    // 禁用 WinHTTP 自带的 Cookie 管理，完全使用我们自定义的 Cookie 头
    DWORD disableFeature = WINHTTP_DISABLE_COOKIES;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_DISABLE_FEATURE, &disableFeature, sizeof(disableFeature));

    // 完整现代浏览器伪装 Header (绕过 B 站安全 WAF 策略)
    std::wstring headerStr = 
        L"Referer: https://www.bilibili.com/\r\n"
        L"Origin: https://www.bilibili.com\r\n"
        L"Sec-Ch-Ua: \"Chromium\";v=\"128\", \"Not;A=Brand\";v=\"24\", \"Google Chrome\";v=\"128\"\r\n"
        L"Sec-Ch-Ua-Mobile: ?0\r\n"
        L"Sec-Ch-Ua-Platform: \"Windows\"\r\n"
        L"Sec-Fetch-Dest: empty\r\n"
        L"Sec-Fetch-Mode: cors\r\n"
        L"Sec-Fetch-Site: same-site\r\n"
        L"Accept: application/json, text/plain, */*\r\n"
        L"Accept-Encoding: gzip, deflate\r\n";

    // POST 表单体: 显式声明 Content-Type 与 Content-Length (WinHTTP 不会自动补全后者)
    if (wcscmp(method, L"POST") == 0 && !body.empty()) {
        headerStr += L"Content-Type: application/x-www-form-urlencoded\r\n";
        headerStr += L"Content-Length: " + std::to_wstring(body.size()) + L"\r\n";
    }

    // 组装 Cookie (精确格式，无多余分号和空格)
    std::string cookieStr = "";
    if (!m_sessData.empty()) {
        cookieStr += "SESSDATA=" + m_sessData;
    }
    if (!m_fullCookie.empty()) {
        if (!cookieStr.empty()) cookieStr += "; ";
        cookieStr += m_fullCookie;
    }
    if (!cookieStr.empty()) {
        headerStr += L"Cookie: " + std::wstring(cookieStr.begin(), cookieStr.end()) + L"\r\n";
    }

    for (const auto &kv : headers) {
        std::string h = kv.first + ": " + kv.second + "\r\n";
        headerStr += std::wstring(h.begin(), h.end());
    }

    WinHttpAddRequestHeaders(hRequest, headerStr.c_str(), (DWORD)headerStr.length(), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

    BOOL bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                       body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.c_str(),
                                       (DWORD)body.size(), (DWORD)body.size(), 0);
    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, NULL);
    }

    // 提取所有 Set-Cookie 响应头
    if (bResults) {
        DWORD dwIndex = 0;
        for (;;) {
            DWORD dwBufLen = 0;
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_SET_COOKIE,
                                WINHTTP_HEADER_NAME_BY_INDEX, NULL, &dwBufLen, &dwIndex);
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && dwBufLen > 0) {
                std::vector<wchar_t> buf(dwBufLen / sizeof(wchar_t) + 1);
                if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_SET_COOKIE,
                                        WINHTTP_HEADER_NAME_BY_INDEX, buf.data(), &dwBufLen, &dwIndex)) {
                    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, buf.data(), -1, NULL, 0, NULL, NULL);
                    if (utf8Len > 0) {
                        std::vector<char> utf8Buf(utf8Len);
                        WideCharToMultiByte(CP_UTF8, 0, buf.data(), -1, utf8Buf.data(), utf8Len, NULL, NULL);
                        responseCookies.push_back(std::string(utf8Buf.data()));
                    }
                } else {
                    break;
                }
            } else {
                break;
            }
        }
    }

    if (bResults) {
        DWORD dwSize = 0;
        responseBody.clear();
        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
            if (dwSize == 0) break;

            std::vector<char> buffer(dwSize);
            DWORD dwDownloaded = 0;
            if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
                responseBody.append(buffer.data(), dwDownloaded);
            }
        } while (dwSize > 0);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return (bResults == TRUE);
}

bool BiliHttpClient::resolveRedirectUrl(const std::string &shortUrl, std::string &finalUrl) {
    std::wstring wUrl(shortUrl.begin(), shortUrl.end());

    URL_COMPONENTS urlComp = { 0 };
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = (DWORD)-1;
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;
    urlComp.dwExtraInfoLength = (DWORD)-1;

    if (!WinHttpCrackUrl(wUrl.c_str(), (DWORD)wUrl.length(), 0, &urlComp)) {
        return false;
    }

    std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength + urlComp.dwExtraInfoLength);
    INTERNET_PORT port = urlComp.nPort;
    bool isHttps = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);

    std::wstring wUserAgent(m_userAgent.begin(), m_userAgent.end());
    std::wstring proxyServer;
    bool hasProxy = detectSystemProxy(proxyServer);
    HINTERNET hSession = WinHttpOpen(wUserAgent.c_str(),
                                     hasProxy ? WINHTTP_ACCESS_TYPE_NAMED_PROXY : WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     hasProxy ? proxyServer.c_str() : WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD flags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL,
                                           WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // 禁用自动跳转，以便主动读取 Location 头
    DWORD redirectOption = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectOption, sizeof(redirectOption));

    BOOL bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, NULL);
    }

    if (bResults) {
        DWORD dwSize = 0;
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, NULL, &dwSize, WINHTTP_NO_HEADER_INDEX);
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && dwSize > 0) {
            std::vector<wchar_t> headerBuffer(dwSize / sizeof(wchar_t) + 1, 0);
            if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, headerBuffer.data(), &dwSize, WINHTTP_NO_HEADER_INDEX)) {
                int utf8Len = WideCharToMultiByte(CP_UTF8, 0, headerBuffer.data(), -1, NULL, 0, NULL, NULL);
                if (utf8Len > 0) {
                    std::vector<char> utf8Buf(utf8Len);
                    WideCharToMultiByte(CP_UTF8, 0, headerBuffer.data(), -1, utf8Buf.data(), utf8Len, NULL, NULL);
                    finalUrl = utf8Buf.data();
                }
            }
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return !finalUrl.empty();
}
