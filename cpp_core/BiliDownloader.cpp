#include "BiliDownloader.hpp"
#include "json.hpp"
#include <windows.h>
#include <winhttp.h>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <regex>
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <string_view>

#pragma comment(lib, "winhttp.lib")

using json = nlohmann::json;
using namespace std::chrono;

// UTF-8 std::string → 宽字符路径。
// 注意: 不能用 std::filesystem::u8path (MSVC 上对 GBK 系统代码页的中文会抛
// ERROR_NO_UNICODE_TRANSLATION), 必须用 MultiByteToWideChar(CP_UTF8) 手工转码。
static std::filesystem::path u8p(const std::string &s) {
    if (s.empty()) return std::filesystem::path();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return std::filesystem::path();
    std::wstring ws(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &ws[0], n);
    return std::filesystem::path(ws);
}

// 伪 200 拦截体识别: ISO-BMFF (MP4/fMP4/m4s) 关键 Box 魔数判定。
// B 站 CDN 反盗链/代理拦截常返回 "200 + 几十字节错误体" 而非 403,
// 因此在流式下载收到第一个 Chunk 时立即判定, 非媒体内容直接拒绝落盘。
static bool isValidMediaChunk(const char *buf, size_t len) {
    if (len < 16) return false;
    std::string_view header(buf, len < 64 ? len : 64);
    // 1. 完整 MP4 / Init Segment (ftyp Box)
    if (header.find("ftyp") != std::string_view::npos) return true;
    // 2. Fragmented MP4 流分片 (moof / styp / mdat / sidx)
    if (header.find("moof") != std::string_view::npos ||
        header.find("styp") != std::string_view::npos ||
        header.find("mdat") != std::string_view::npos ||
        header.find("sidx") != std::string_view::npos) return true;
    // 3. 拦截体快速排查: 以文本/JSON/XML 开头直接判定为非媒体
    if (buf[0] == '{' || buf[0] == '<') return false;
    return false;
}

// A1: 解析 Content-Range 响应头: "bytes start-end/total" (CDN 可能省略 "bytes " 前缀)
// 起始字节与续传偏移必须严格一致, 否则服务器忽略/错配了 Range 分段, 盲目追加会写坏 .part。
// (非 static 便于 RobustTest 单元测试直接调用)
bool parseContentRange(const wchar_t *crBuf, int64_t &start, int64_t &end, int64_t &total) {
    start = end = total = -1;
    if (!crBuf) return false;
    std::wstring s(crBuf);
    size_t seg = s.find(L"bytes ");
    seg = (seg == std::wstring::npos) ? 0 : seg + 6;
    size_t dash = s.find(L'-', seg);
    size_t slash = s.find(L'/', dash == std::wstring::npos ? 0 : dash);
    if (dash == std::wstring::npos || slash == std::wstring::npos || slash <= dash) return false;
    auto isDigits = [](const std::wstring &str) {
        return !str.empty() && std::all_of(str.begin(), str.end(),
                                           [](wchar_t c) { return c >= L'0' && c <= L'9'; });
    };
    try {
        std::wstring startStr = s.substr(seg, dash - seg);
        std::wstring endStr = s.substr(dash + 1, slash - dash - 1);
        std::wstring totalStr = s.substr(slash + 1);
        if (!isDigits(startStr) || !isDigits(endStr) || (!isDigits(totalStr) && totalStr != L"*")) return false;
        start = _wcstoi64(startStr.c_str(), nullptr, 10);
        end = _wcstoi64(endStr.c_str(), nullptr, 10);
        total = (totalStr == L"*") ? -1 : _wcstoi64(totalStr.c_str(), nullptr, 10);
    } catch (...) { return false; }
    return (start >= 0);
}

// A2: 未知大小媒体下载 (无 Content-Length/Content-Range) 的容器完整性校验。
// 遍历 ISO-BMFF 顶层 Box 链, 校验每个 Box 声明长度不越过文件边界,
// 以拦截"连接中途断流但字节数未知"导致的截断产物 (字节数看不出, 结构能看出)。
// 仅做顶层盒链扫描 (每盒只读 8~16 字节头), 不解析媒体内部, 开销可忽略。
// (非 static 便于 RobustTest 单元测试直接调用)
bool isValidMediaContainer(const std::string &filePath, std::string &error) {
    HANDLE h = CreateFileW(u8p(filePath).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { error = "无法打开已下载文件"; return false; }
    LARGE_INTEGER li;
    if (!GetFileSizeEx(h, &li) || li.QuadPart < 8) {
        CloseHandle(h);
        error = "文件为空或过小";
        return false;
    }
    const int64_t fileSize = li.QuadPart;

    auto readAt = [&](int64_t off, void *buf, DWORD n) -> bool {
        LARGE_INTEGER pos; pos.QuadPart = off;
        if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) return false;
        DWORD rd = 0;
        return ReadFile(h, buf, n, &rd, NULL) && rd == n;
    };

    auto boxTypeOf = [](const unsigned char *hdr) -> std::string {
        char t[5] = { char(hdr[4]), char(hdr[5]), char(hdr[6]), char(hdr[7]), 0 };
        return std::string(t);
    };

    int64_t pos = 0;
    bool seenPayload = false; // 是否出现媒体数据盒 (moov/moof/mdat), 仅 ftyp/styp 等头部不算完整
    while (pos + 8 <= fileSize) {
        unsigned char hdr[16] = { 0 };
        if (!readAt(pos, hdr, 8)) {
            CloseHandle(h);
            error = "盒头读取失败";
            return false;
        }
        uint32_t size32 = (uint32_t(hdr[0]) << 24) | (uint32_t(hdr[1]) << 16) |
                          (uint32_t(hdr[2]) << 8) | uint32_t(hdr[3]);
        int64_t boxSize;
        if (size32 == 1) {
            // 64-bit largesize: 8 字节头 + 8 字节大尺寸
            if (!readAt(pos, hdr, 16)) {
                CloseHandle(h);
                error = "largesize 读取失败";
                return false;
            }
            uint64_t ls = 0;
            for (int i = 8; i < 16; ++i) ls = (ls << 8) | hdr[i];
            boxSize = (int64_t)ls;
            if (boxSize < 16) { CloseHandle(h); error = "非法 largesize"; return false; }
        } else if (size32 == 0) {
            // size=0 → 该 Box 声明延伸至文件末尾 (ISO-BMFF 合法形态, 通常为最后一盒)
            boxSize = fileSize - pos;
        } else if (size32 < 8) {
            CloseHandle(h);
            error = "非法 Box 头 (size<8)";
            return false;
        } else {
            boxSize = size32;
        }

        std::string t = boxTypeOf(hdr);
        if (t == "moov" || t == "moof" || t == "mdat") seenPayload = true;

        if (boxSize > fileSize - pos) {
            // Box 声明长度越过文件末尾 → 该盒被截断 (连接中途断流的典型特征)
            CloseHandle(h);
            error = "Box(" + t + ") 声明长度超出文件边界, 疑似传输中断";
            return false;
        }
        pos += boxSize;
        if (pos >= fileSize) break;
    }

    CloseHandle(h);
    if (pos != fileSize) {
        error = "文件尾部存在未解析残余数据, 疑似截断";
        return false;
    }
    if (!seenPayload) {
        error = "未发现媒体数据盒 (moov/moof/mdat), 疑似仅头部或截断";
        return false;
    }
    return true;
}

BiliDownloader::BiliDownloader() {}
BiliDownloader::~BiliDownloader() {}

void BiliDownloader::setSessData(const std::string &sessData) {
    m_client.setSessData(sessData);
}

// 1. 下载封面
bool BiliDownloader::downloadCover(const std::string &coverUrl, const std::string &destFilePath, std::string &errorMsg) {
    std::string url = coverUrl;
    if (url.rfind("//", 0) == 0) {
        url = "https:" + url;
    }

    std::string data;
    if (!m_client.get(url, data, {}, 10)) {
        errorMsg = "封面网络下载失败";
        return false;
    }

    if (data.empty()) {
        errorMsg = "封面数据为空";
        return false;
    }

    std::ofstream ofs(u8p(destFilePath), std::ios::binary);
    if (!ofs.is_open()) {
        errorMsg = "无法创建封面本地文件: " + destFilePath;
        return false;
    }

    ofs.write(data.data(), data.size());
    ofs.close();
    return true;
}

// 2. 下载并解析弹幕池
bool BiliDownloader::downloadDanmaku(int64_t cid, const std::string &destXmlPath, std::vector<DanmakuEntry> &entries, std::string &errorMsg) {
    std::string url = "https://comment.bilibili.com/" + std::to_string(cid) + ".xml";
    std::string xmlData;

    if (!m_client.get(url, xmlData, {}, 10)) {
        errorMsg = "弹幕 XML 请求失败";
        return false;
    }

    if (xmlData.empty()) {
        errorMsg = "获取到的弹幕内容为空";
        return false;
    }

    // 写入本地 XML 文件
    if (!destXmlPath.empty()) {
        std::ofstream ofs(u8p(destXmlPath), std::ios::binary);
        if (ofs.is_open()) {
            ofs.write(xmlData.data(), xmlData.size());
            ofs.close();
        }
    }

    // 正则解析 XML 弹幕节点
    return parseDanmakuXml(xmlData, entries);
}

// 2.5 下载字幕并转换为 SRT 文件 (优先中文轨, 无中文取第一条; 失败不阻塞主任务)
bool BiliDownloader::downloadSubtitle(const std::vector<SubtitleTrack> &subtitles, const std::string &destSrtPath, std::string &errorMsg) {
    if (subtitles.empty()) return false;

    // 优先选择中文轨道 (zh-CN / 中文), 否则取第一条
    const SubtitleTrack *best = nullptr;
    for (const auto &s : subtitles) {
        if (s.lang.rfind("zh", 0) == 0 || s.langDoc.find("中文") != std::string::npos) {
            best = &s;
            break;
        }
    }
    if (!best) best = &subtitles[0];

    std::string url = best->subtitleUrl;
    if (url.rfind("//", 0) == 0) {
        url = "https:" + url;
    }

    // 拉取字幕 JSON (B 站 CC 字幕: {"body":[{"from":..,"to":..,"content":..}]})
    std::string data;
    if (!m_client.get(url, data, {}, 10)) {
        errorMsg = "字幕 JSON 请求失败";
        return false;
    }
    if (data.empty()) {
        errorMsg = "字幕数据为空";
        return false;
    }

    json root;
    try {
        root = json::parse(data);
    } catch (const std::exception &e) {
        errorMsg = std::string("字幕 JSON 解析失败: ") + e.what();
        return false;
    }

    if (!root.contains("body") || !root["body"].is_array() || root["body"].empty()) {
        errorMsg = "字幕 body 为空";
        return false;
    }

    // 组装 SRT 内容
    std::ostringstream srt;
    int idx = 1;
    for (const auto &item : root["body"]) {
        if (!item.contains("content")) continue;
        double from = item.value("from", 0.0);
        double to   = item.value("to", 0.0);

        // 时间戳格式化 HH:MM:SS,mmm
        auto fmtTs = [](double sec) {
            int ms  = static_cast<int>((sec - static_cast<int>(sec)) * 1000.0);
            int s   = static_cast<int>(sec) % 60;
            int m   = (static_cast<int>(sec) / 60) % 60;
            int h   = static_cast<int>(sec) / 3600;
            char buf[32];
            snprintf(buf, sizeof(buf), "%02d:%02d:%02d,%03d", h, m, s, ms);
            return std::string(buf);
        };

        srt << idx++ << "\n";
        srt << fmtTs(from) << " --> " << fmtTs(to) << "\n";
        srt << item["content"].get<std::string>() << "\n\n";
    }

    if (idx == 1) {
        errorMsg = "字幕内容为空";
        return false;
    }

    std::ofstream ofs(u8p(destSrtPath), std::ios::binary);
    if (!ofs.is_open()) {
        errorMsg = "无法创建字幕本地文件: " + destSrtPath;
        return false;
    }
    ofs.write(srt.str().data(), srt.str().size());
    ofs.close();
    return true;
}

bool BiliDownloader::parseDanmakuXml(const std::string &xmlData, std::vector<DanmakuEntry> &entries) {
    entries.clear();
    // 匹配格式: <d p="0.123,1,25,16777215,1600000000,0,d38e2e92,123456789">弹幕文本</d>
    std::regex dmRegex("<d p=\"([^\"]+)\">([^<]*)</d>");
    auto words_begin = std::sregex_iterator(xmlData.begin(), xmlData.end(), dmRegex);
    auto words_end = std::sregex_iterator();

    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
        std::smatch match = *i;
        std::string pAttr = match[1].str();
        std::string text = match[2].str();

        DanmakuEntry entry;
        entry.content = text;

        // 分割 p 属性字段
        std::stringstream ss(pAttr);
        std::string item;
        std::vector<std::string> pList;
        while (std::getline(ss, item, ',')) {
            pList.push_back(item);
        }

        if (pList.size() >= 7) {
            try {
                entry.timeSec = std::stod(pList[0]);
                entry.mode = std::stoi(pList[1]);
                entry.fontSize = std::stoi(pList[2]);
                entry.color = static_cast<uint32_t>(std::stoul(pList[3]));
                entry.timestamp = std::stoll(pList[4]);
                entry.senderHash = pList[6];
            } catch (...) {}
        }

        entries.push_back(entry);
    }
    return !entries.empty();
}

// 3. 真实大文件流式下载 (支持取消中断 + Range 断点续传)
bool BiliDownloader::downloadStream(
    const std::string &streamUrl, 
    const std::string &destFilePath, 
    ProgressCallback progressCb, 
    int64_t maxBytes,
    std::string *errorMsg,
    int64_t resumeOffset,
    std::atomic<bool> *cancel
) {
    // 协议相对地址补全 (DASH base_url 可能以 // 开头, WinHttpCrackUrl 无法识别)
    std::string url = streamUrl;
    if (url.rfind("//", 0) == 0) url = "https:" + url;

    // 系统代理探测 (Clash/台湾代理等经系统设置注入, 必须走 IE 代理配置穿透)
    std::wstring proxyServer;
    bool hasProxy = BiliHttpClient::detectSystemProxy(proxyServer);

    HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
                                     hasProxy ? WINHTTP_ACCESS_TYPE_NAMED_PROXY : WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     hasProxy ? proxyServer.c_str() : WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        if (errorMsg) *errorMsg = "WinHttpOpen 失败";
        return false;
    }

    // 设置长传输超时
    WinHttpSetTimeouts(hSession, 10000, 10000, 30000, 30000);

    // 严密防盗链 Header: 标准 Referer (带尾斜杠) + 强制 identity 防 gzip 篡改二进制流
    std::wstring headerStr = L"Referer: https://www.bilibili.com/\r\n"
                             L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)\r\n"
                             L"Accept-Encoding: identity\r\n";
    std::string sess = m_client.getSessData();
    if (!sess.empty()) {
        std::string cookie = "Cookie: SESSDATA=" + sess + "\r\n";
        headerStr += std::wstring(cookie.begin(), cookie.end());
    }
    // 断点续传: 请求 Range: bytes=N- (服务器支持则回 206)
    if (resumeOffset > 0) {
        std::string range = "Range: bytes=" + std::to_string(resumeOffset) + "-\r\n";
        headerStr += std::wstring(range.begin(), range.end());
    }

    HINTERNET hConnect = NULL;
    HINTERNET hRequest = NULL;
    std::wstring currentUrl(url.begin(), url.end());
    DWORD statusCode = 0;
    bool requestFailed = true;

    // 手动重定向循环: WinHTTP 自动 302 跳转会剥离 Referer/Cookie 导致 CDN 403,
    // 必须关掉自动跳转, 逐跳携带完整请求头重发 (对齐成熟下载器做法)
    for (int redirect = 0; redirect < 5; ++redirect) {
        URL_COMPONENTS urlComp = { 0 };
        urlComp.dwStructSize = sizeof(urlComp);
        urlComp.dwSchemeLength = (DWORD)-1;
        urlComp.dwHostNameLength = (DWORD)-1;
        urlComp.dwUrlPathLength = (DWORD)-1;
        urlComp.dwExtraInfoLength = (DWORD)-1;

        if (!WinHttpCrackUrl(currentUrl.c_str(), (DWORD)currentUrl.length(), 0, &urlComp)) {
            if (errorMsg) *errorMsg = "URL 解析失败";
            break;
        }

        std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
        std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength + urlComp.dwExtraInfoLength);
        INTERNET_PORT port = urlComp.nPort;
        bool isHttps = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);

        hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
        if (!hConnect) {
            if (errorMsg) *errorMsg = "连接 CDN 服务器失败";
            break;
        }

        DWORD flags = isHttps ? WINHTTP_FLAG_SECURE : 0;
        hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL,
                                      WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      flags);
        if (!hRequest) {
            if (errorMsg) *errorMsg = "创建下载请求句柄失败";
            break;
        }

        // 关闭自动重定向, 改用手动逐跳跟随 (保住 Referer/Cookie 防 403)
        DWORD redirectOption = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectOption, sizeof(redirectOption));

        WinHttpAddRequestHeaders(hRequest, headerStr.c_str(), (DWORD)headerStr.length(), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

        BOOL bSend = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
        if (!bSend || !WinHttpReceiveResponse(hRequest, NULL)) {
            if (errorMsg) *errorMsg = "CDN 服务器未响应下载请求";
            break;
        }

        DWORD code = 0;
        DWORD codeSize = sizeof(code);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &code, &codeSize, WINHTTP_NO_HEADER_INDEX);

        // 命中重定向: 读取 Location 并解析为绝对地址, 携带原 Header 重发
        if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
            DWORD dwSize = 0;
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                NULL, &dwSize, WINHTTP_NO_HEADER_INDEX);
            std::wstring location;
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && dwSize > 0) {
                std::vector<wchar_t> locBuf(dwSize / sizeof(wchar_t) + 1, 0);
                if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                        locBuf.data(), &dwSize, WINHTTP_NO_HEADER_INDEX)) {
                    location = locBuf.data();
                }
            }
            WinHttpCloseHandle(hRequest); hRequest = NULL;
            WinHttpCloseHandle(hConnect); hConnect = NULL;
            if (location.empty()) {
                if (errorMsg) *errorMsg = "CDN 重定向但缺少 Location";
                break;
            }
            // 解析 Location → 绝对地址 (支持绝对/协议相对/路径相对三种形态)
            if (location.compare(0, 7, L"http://") == 0 || location.compare(0, 8, L"https://") == 0) {
                currentUrl = location;
            } else if (location.compare(0, 2, L"//") == 0) {
                currentUrl = (isHttps ? L"https:" : L"http:") + location;
            } else {
                std::wstring prefix = (isHttps ? L"https://" : L"http://") + host;
                if (port != 80 && port != 443) prefix += L":" + std::to_wstring(port);
                currentUrl = (location[0] == L'/') ? prefix + location : prefix + L"/" + location;
            }
            continue;
        }

        statusCode = code;
        requestFailed = false;
        break;
    }

    if (requestFailed) {
        if (errorMsg && errorMsg->empty()) *errorMsg = "CDN 重定向次数超限或请求失败";
        if (hRequest) WinHttpCloseHandle(hRequest);
        if (hConnect) WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // Range 越界 (416): 说明 .part 已超过或等于服务器文件末尾
    if (statusCode == 416 && resumeOffset > 0) {
        // 解析 Content-Range: bytes */<total> 判定 .part 是否已完整
        int64_t serverTotal = 0;
        wchar_t crBuf[128] = { 0 };
        DWORD crSize = sizeof(crBuf);
        if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_RANGE, WINHTTP_HEADER_NAME_BY_INDEX,
                                crBuf, &crSize, WINHTTP_NO_HEADER_INDEX)) {
            std::wstring cr = crBuf;
            auto star = cr.find(L"*/");
            if (star != std::wstring::npos) serverTotal = _wcstoi64(cr.c_str() + star + 2, nullptr, 10);
        }
        bool partComplete = false;
        if (serverTotal > 0) {
            HANDLE hP = CreateFileW(u8p(destFilePath).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hP != INVALID_HANDLE_VALUE) {
                LARGE_INTEGER sz;
                if (GetFileSizeEx(hP, &sz)) partComplete = (sz.QuadPart >= serverTotal);
                CloseHandle(hP);
            }
        }
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        if (partComplete) return true; // .part 已完整, 由上层原子改名收尾
        DeleteFileW(u8p(destFilePath).c_str()); // 残缺: 删除以便从 0 完整重下
        if (errorMsg) *errorMsg = "服务器拒绝续传 (Range 越界)，已重置部分文件";
        return false;
    }

    if (statusCode != 200 && statusCode != 206) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        if (errorMsg) *errorMsg = "CDN 拒绝请求，HTTP 状态码: " + std::to_string(statusCode);
        return false;
    }

    // ===== A1: 续传决策 — 必须校验 206 Content-Range 起始字节 =====
    // 服务器回 200 → 从头覆盖; 回 206 但起始字节 ≠ resumeOffset → 分段不一致,
    // 盲目追加会把 .part 写坏, 同样回退为从头重下 (安全兜底)。
    bool resuming = (statusCode == 206) && (resumeOffset > 0);
    int64_t contentRangeTotal = 0;
    if (resuming) {
        wchar_t crBuf[192] = { 0 };
        DWORD crSize = sizeof(crBuf);
        if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_RANGE, WINHTTP_HEADER_NAME_BY_INDEX,
                                crBuf, &crSize, WINHTTP_NO_HEADER_INDEX)) {
            int64_t crStart = -1, crEnd = -1, crTotal = -1;
            if (parseContentRange(crBuf, crStart, crEnd, crTotal)) {
                if (crStart != resumeOffset) resuming = false; // 起始字节不符 → 从头重下
                if (crTotal > 0) contentRangeTotal = crTotal;
            } else {
                resuming = false; // 206 却无合法 Content-Range → 无法确认分段, 保守从头重下
            }
        } else {
            resuming = false; // 206 却缺 Content-Range 头 (异常服务器) → 从头重下
        }
    }
    if (!resuming) resumeOffset = 0; // 服务器回 200 或分段不符: 忽略续传, 从头覆盖

    // 获取总文件大小 (优先级: Content-Range 的 total > Content-Length)
    // 用字符串解析以支持 >4GB; 两者都缺失 → totalBytes=0 (未知大小, 末尾做容器完整性校验)
    int64_t totalBytes = 0;
    if (resuming && contentRangeTotal > 0) {
        totalBytes = contentRangeTotal;
    } else {
        wchar_t lenBuf[64] = { 0 };
        DWORD lenSize = sizeof(lenBuf);
        if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                                lenBuf, &lenSize, WINHTTP_NO_HEADER_INDEX)) {
            totalBytes = _wcstoi64(lenBuf, nullptr, 10);
        }
        if (resuming) totalBytes += resumeOffset; // 206 的 Content-Length 为剩余字节数
    }

    // 续传则追加写入, 否则覆盖
    std::ios::openmode openMode = std::ios::binary;
    if (resuming) openMode |= std::ios::app;
    std::ofstream ofs(u8p(destFilePath), openMode);
    if (!ofs.is_open()) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        if (errorMsg) *errorMsg = "无法创建本地写入文件: " + destFilePath;
        return false;
    }

    int64_t downloadedBytes = resumeOffset;
    auto startTime = high_resolution_clock::now();
    auto lastCallbackTime = startTime;

    std::vector<char> buffer(64 * 1024); // 64KB 缓冲区
    DWORD dwRead = 0;
    bool readError = false;      // 是否发生传输中断 (区别于正常 EOF)
    bool maxBytesReached = false;
    bool firstChunk = true;      // 首块魔数校验 (仅全新下载生效)

    for (;;) {
        if (cancel && cancel->load()) break;
        if (!WinHttpReadData(hRequest, buffer.data(), (DWORD)buffer.size(), &dwRead)) {
            // 正常流末尾不会报错; 其余错误码视为传输中断 (网络闪断/服务器断开)
            DWORD err = GetLastError();
            readError = (err != ERROR_SUCCESS && err != ERROR_HANDLE_EOF);
            break;
        }
        if (dwRead == 0) break; // 正常 EOF

        // 伪 200 拦截体识别: 全新下载的首块必须是合法媒体容器,
        // 否则是反爬拦截/代理注入的文本页, 立即拒绝落盘并触发备份 CDN 容灾
        if (firstChunk) {
            firstChunk = false;
            if (!resuming && !isValidMediaChunk(buffer.data(), dwRead)) {
                ofs.close();
                DeleteFileW(u8p(destFilePath).c_str()); // 不留下破坏性小文件
                WinHttpCloseHandle(hRequest);
                WinHttpCloseHandle(hConnect);
                WinHttpCloseHandle(hSession);
                if (errorMsg) *errorMsg = "CDN 返回非媒体内容(疑似反爬拦截)，已放弃该节点";
                return false;
            }
        }

        if (m_rateLimiter) {
            m_rateLimiter->acquire(dwRead);
        }

        ofs.write(buffer.data(), dwRead);
        downloadedBytes += dwRead;

        auto now = high_resolution_clock::now();
        double elapsedTotalSec = duration<double>(now - startTime).count();
        double currentSpeed = elapsedTotalSec > 0 ? ((downloadedBytes - resumeOffset) / elapsedTotalSec) : 0.0;
        double percent = (totalBytes > 0) ? ((double)downloadedBytes * 100.0 / (double)totalBytes) : 0.0;

        if (progressCb && duration<double, std::milli>(now - lastCallbackTime).count() > 100) {
            progressCb(downloadedBytes, totalBytes, currentSpeed, percent);
            lastCallbackTime = now;
        }

        if (maxBytes > 0 && (downloadedBytes - resumeOffset) >= maxBytes) {
            maxBytesReached = true; // 达到指定截断长度 (测试用, 不算不完整)
            break;
        }
    }

    bool cancelled = cancel && cancel->load();
    ofs.close();
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (cancelled) {
        if (errorMsg) *errorMsg = "已取消";
        return false;
    }

    // 完整性校验 (1): 已知总大小却未下载完整 → 视为传输中断 (保留 .part 供续传)
    if (!maxBytesReached && !readError && totalBytes > 0 && downloadedBytes < totalBytes) {
        readError = true;
    }
    if (readError) {
        if (errorMsg) {
            *errorMsg = "下载中断: 已获取 " + std::to_string(downloadedBytes)
                      + " / " + std::to_string(totalBytes) + " 字节";
        }
        return false;
    }

    // 完整性校验 (2): 未知总大小 (无 Content-Length/Content-Range) 的全新下载,
    // 无法靠字节数判定是否完整, 用媒体容器盒链校验拦截"中途断流但看似完成"的截断产物。
    if (!maxBytesReached && !resuming && downloadedBytes > resumeOffset && totalBytes <= 0) {
        std::string verr;
        if (!isValidMediaContainer(destFilePath, verr)) {
            if (errorMsg) *errorMsg = "下载内容不完整: " + verr;
            return false; // 保留 .part, 由上层重试/CDN 容灾接管
        }
    }

    if (progressCb) {
        auto now = high_resolution_clock::now();
        double elapsedTotalSec = duration<double>(now - startTime).count();
        double finalSpeed = elapsedTotalSec > 0 ? ((downloadedBytes - resumeOffset) / elapsedTotalSec) : 0.0;
        progressCb(downloadedBytes, totalBytes, finalSpeed, 100.0);
    }

    return (downloadedBytes > resumeOffset);
}

// 4. 完整的单 P 媒体包下载
bool BiliDownloader::downloadEpisodePackage(
    const BiliVideoDetail &detail, 
    int pageIndex, 
    const std::string &outputDir, 
    int targetQuality, 
    int targetCodecId,
    bool downloadSubRes,
    ProgressCallback videoProgressCb,
    ProgressCallback audioProgressCb,
    std::string *errorMsg
) {
    CreateDirectoryW(u8p(outputDir).c_str(), NULL);

    // 1. 下载封面
    if (!detail.coverUrl.empty()) {
        std::string err;
        downloadCover(detail.coverUrl, outputDir + "/cover.jpg", err);
    }

    // 2. 下载弹幕 (受全局附属资源开关控制, 关闭时跳过)
    if (downloadSubRes && pageIndex >= 0 && pageIndex < (int)detail.pages.size()) {
        int64_t cid = detail.pages[pageIndex].cid;
        std::vector<DanmakuEntry> dms;
        std::string err;
        downloadDanmaku(cid, outputDir + "/danmaku.xml", dms, err);
    }

    // 2.5 下载字幕 (转 SRT, 尽力而为, 失败不阻塞主任务; 受同一开关控制)
    if (downloadSubRes && !detail.subtitles.empty()) {
        std::string err;
        downloadSubtitle(detail.subtitles, outputDir + "/subtitle.srt", err);
    }

    // 3. 选择最佳视频轨道
    const VideoTrack *bestVideo = nullptr;
    for (const auto &v : detail.videoTracks) {
        if (targetQuality > 0 && v.quality != targetQuality) continue;
        if (targetCodecId > 0 && v.codecId != targetCodecId) continue;
        bestVideo = &v;
        break;
    }
    if (!bestVideo && !detail.videoTracks.empty()) {
        bestVideo = &detail.videoTracks[0];
    }

    if (bestVideo && !bestVideo->baseUrl.empty()) {
        std::string videoOut = outputDir + "/video.m4s";
        if (!downloadStream(bestVideo->baseUrl, videoOut, videoProgressCb, 0, errorMsg)) {
            return false;
        }
    }

    // 4. 选择最佳音频轨道
    if (!detail.audioTracks.empty() && !detail.audioTracks[0].baseUrl.empty()) {
        std::string audioOut = outputDir + "/audio.m4s";
        if (!downloadStream(detail.audioTracks[0].baseUrl, audioOut, audioProgressCb, 0, errorMsg)) {
            return false;
        }
    }

    return true;
}
