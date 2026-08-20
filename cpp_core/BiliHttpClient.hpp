#pragma once

#include <string>
#include <map>
#include <vector>

class BiliHttpClient {
public:
    BiliHttpClient();
    ~BiliHttpClient();

    // 设置全局 Cookie (如 SESSDATA)
    void setSessData(const std::string &sessData);
    std::string getSessData() const;

    // 设置自定义 Cookie 串
    void setCookie(const std::string &cookie);

    // 发送 GET 请求 (自动跟随 302 重定向、自动 GZIP 解压)
    bool get(const std::string &url, std::string &responseBody, 
             const std::map<std::string, std::string> &headers = {}, 
             int timeoutSec = 10);

    // 重载: 同时提取 Set-Cookie 响应头 (用于 QR 登录获取 SESSDATA)
    bool get(const std::string &url, std::string &responseBody,
             std::vector<std::string> &responseCookies,
             const std::map<std::string, std::string> &headers = {},
             int timeoutSec = 10);

    // 发送 POST 请求 (application/x-www-form-urlencoded, 用于 refresh_token 续期等)
    bool post(const std::string &url, const std::string &body, std::string &responseBody,
              const std::map<std::string, std::string> &headers = {},
              int timeoutSec = 10);

    // 重载: 同时提取 Set-Cookie 响应头 (续期接口通过 Set-Cookie 下发新 SESSDATA)
    bool post(const std::string &url, const std::string &body, std::string &responseBody,
              std::vector<std::string> &responseCookies,
              const std::map<std::string, std::string> &headers = {},
              int timeoutSec = 10);

    // 获取短链跳转后的真实目标 URL (用于 b23.tv 逆向)
    bool resolveRedirectUrl(const std::string &shortUrl, std::string &finalUrl);

    // 探测当前用户系统代理 (IE/Clash/v2ray 等), 命中则返回代理服务器串。
    // 注意: WINHTTP_ACCESS_TYPE_DEFAULT_PROXY 读的是 netsh winhttp 配置, 与
    // 用户常用的系统代理(Clash 等)是两套东西, 必须走 IE 代理配置才能穿透海外代理。
    static bool detectSystemProxy(std::wstring &proxyServer);

    // URL 编解码通用工具
    static std::string urlEncode(const std::string &value);
    static std::string urlDecode(const std::string &value);

private:
    // WinHTTP 通用请求内核: GET/POST 共用 (自动 GZIP 解压、系统代理、Cookie 头组装)
    bool request(const wchar_t *method, const std::string &url, const std::string &body,
                 const std::map<std::string, std::string> &headers,
                 std::vector<std::string> &responseCookies,
                 std::string &responseBody, int timeoutSec);

    std::string m_sessData;
    std::string m_fullCookie;
    std::string m_userAgent;
};
