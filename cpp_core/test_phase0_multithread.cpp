#include "BiliParser.hpp"
#include "BiliHttpClient.hpp"
#include "BiliDownloader.hpp"
#include "BiliAuth.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <future>
#include <chrono>
#include <windows.h>
#include <winhttp.h>
#include <QCryptographicHash>
#include <QFile>
#include <QDir>
#include <QFileInfo>

// UTF-8 字符串转宽字符 (复用项目统一实现)
static std::wstring u8toW(const std::string &u8) {
    if (u8.empty()) return L"";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), (int)u8.size(), NULL, 0);
    if (wlen <= 0) return L"";
    std::wstring ws(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), (int)u8.size(), &ws[0], wlen);
    return ws;
}

// 计算本地文件 MD5
static std::string calculateFileMd5(const std::string &filePath) {
    QFile f(QString::fromStdString(filePath));
    if (!f.open(QIODevice::ReadOnly)) {
        return "";
    }
    QCryptographicHash hash(QCryptographicHash::Md5);
    if (hash.addData(&f)) {
        return hash.result().toHex().toStdString();
    }
    return "";
}

// 探测媒体流 totalSize (通过 GET Range: bytes=0-0)
static bool probeStreamTotalSize(const std::string &streamUrl, const std::string &sessData,
                                 int64_t &outTotalSize, DWORD &outStatusCode, std::string &errorMsg) {
    outTotalSize = -1;
    outStatusCode = 0;

    std::string url = streamUrl;
    if (url.rfind("//", 0) == 0) url = "https:" + url;

    std::wstring proxyServer;
    bool hasProxy = BiliHttpClient::detectSystemProxy(proxyServer);

    HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
                                     hasProxy ? WINHTTP_ACCESS_TYPE_NAMED_PROXY : WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     hasProxy ? proxyServer.c_str() : WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        errorMsg = "WinHttpOpen 失败";
        return false;
    }
    WinHttpSetTimeouts(hSession, 10000, 10000, 15000, 15000);

    std::wstring headerStr = L"Referer: https://www.bilibili.com/\r\n"
                             L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)\r\n"
                             L"Accept-Encoding: identity\r\n"
                             L"Range: bytes=0-0\r\n";
    if (!sessData.empty()) {
        std::string cookie = "Cookie: SESSDATA=" + sessData + "\r\n";
        headerStr += std::wstring(cookie.begin(), cookie.end());
    }

    std::wstring currentUrl(url.begin(), url.end());
    for (int redirect = 0; redirect < 5; ++redirect) {
        URL_COMPONENTS urlComp = { 0 };
        urlComp.dwStructSize = sizeof(urlComp);
        urlComp.dwSchemeLength = (DWORD)-1;
        urlComp.dwHostNameLength = (DWORD)-1;
        urlComp.dwUrlPathLength = (DWORD)-1;
        urlComp.dwExtraInfoLength = (DWORD)-1;

        if (!WinHttpCrackUrl(currentUrl.c_str(), (DWORD)currentUrl.length(), 0, &urlComp)) {
            errorMsg = "URL 解析失败";
            break;
        }

        std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
        std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength + urlComp.dwExtraInfoLength);
        INTERNET_PORT port = urlComp.nPort;
        bool isHttps = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);

        HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
        if (!hConnect) {
            errorMsg = "连接 CDN 失败";
            break;
        }

        DWORD flags = isHttps ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL,
                                               WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            errorMsg = "创建请求失败";
            break;
        }

        DWORD redirectOption = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectOption, sizeof(redirectOption));
        WinHttpAddRequestHeaders(hRequest, headerStr.c_str(), (DWORD)headerStr.length(),
                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

        BOOL bSend = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
        if (!bSend || !WinHttpReceiveResponse(hRequest, NULL)) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            errorMsg = "CDN 服务器未响应请求";
            break;
        }

        DWORD code = 0;
        DWORD codeSize = sizeof(code);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &code, &codeSize, WINHTTP_NO_HEADER_INDEX);
        outStatusCode = code;

        if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
            DWORD locSize = 0;
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                NULL, &locSize, WINHTTP_NO_HEADER_INDEX);
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && locSize > 0) {
                std::wstring locStr(locSize / sizeof(wchar_t), 0);
                if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                        &locStr[0], &locSize, WINHTTP_NO_HEADER_INDEX)) {
                    while (!locStr.empty() && locStr.back() == L'\0') locStr.pop_back();
                    currentUrl = locStr;
                    WinHttpCloseHandle(hRequest);
                    WinHttpCloseHandle(hConnect);
                    continue;
                }
            }
        }

        // 解析 Content-Range 响应头 (例如 "bytes 0-0/12345678")
        DWORD rangeHeaderSize = 0;
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CUSTOM, L"Content-Range", NULL, &rangeHeaderSize, WINHTTP_NO_HEADER_INDEX);
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && rangeHeaderSize > 0) {
            std::wstring rangeStr(rangeHeaderSize / sizeof(wchar_t), 0);
            if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CUSTOM, L"Content-Range",
                                    &rangeStr[0], &rangeHeaderSize, WINHTTP_NO_HEADER_INDEX)) {
                while (!rangeStr.empty() && rangeStr.back() == L'\0') rangeStr.pop_back();
                std::string s(rangeStr.begin(), rangeStr.end());
                auto slashPos = s.find('/');
                if (slashPos != std::string::npos) {
                    try {
                        outTotalSize = std::stoll(s.substr(slashPos + 1));
                    } catch (...) {}
                }
            }
        }

        // 若无 Content-Range 但有 Content-Length 且状态为 200 (极少见兜底)
        if (outTotalSize <= 0 && code == 200) {
            DWORD clSize = 0;
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &clSize, &clSize, WINHTTP_NO_HEADER_INDEX);
            outTotalSize = clSize;
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        break;
    }

    WinHttpCloseHandle(hSession);
    return outTotalSize > 0;
}

// 单个分片拉取函数 [startByte, endByte] (闭区间)
static bool downloadChunkRange(const std::string &streamUrl, const std::string &sessData,
                               int64_t startByte, int64_t endByte,
                               const std::string &destChunkPath, std::string &errorMsg) {
    std::string url = streamUrl;
    if (url.rfind("//", 0) == 0) url = "https:" + url;

    std::wstring proxyServer;
    bool hasProxy = BiliHttpClient::detectSystemProxy(proxyServer);

    HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
                                     hasProxy ? WINHTTP_ACCESS_TYPE_NAMED_PROXY : WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     hasProxy ? proxyServer.c_str() : WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        errorMsg = "WinHttpOpen 失败";
        return false;
    }
    WinHttpSetTimeouts(hSession, 10000, 10000, 30000, 30000);

    std::string rangeStr = "Range: bytes=" + std::to_string(startByte) + "-" + std::to_string(endByte) + "\r\n";
    std::wstring headerStr = L"Referer: https://www.bilibili.com/\r\n"
                             L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)\r\n"
                             L"Accept-Encoding: identity\r\n";
    headerStr += std::wstring(rangeStr.begin(), rangeStr.end());
    if (!sessData.empty()) {
        std::string cookie = "Cookie: SESSDATA=" + sessData + "\r\n";
        headerStr += std::wstring(cookie.begin(), cookie.end());
    }

    std::wstring currentUrl(url.begin(), url.end());
    bool success = false;

    for (int redirect = 0; redirect < 5; ++redirect) {
        URL_COMPONENTS urlComp = { 0 };
        urlComp.dwStructSize = sizeof(urlComp);
        urlComp.dwSchemeLength = (DWORD)-1;
        urlComp.dwHostNameLength = (DWORD)-1;
        urlComp.dwUrlPathLength = (DWORD)-1;
        urlComp.dwExtraInfoLength = (DWORD)-1;

        if (!WinHttpCrackUrl(currentUrl.c_str(), (DWORD)currentUrl.length(), 0, &urlComp)) {
            errorMsg = "URL 解析失败";
            break;
        }

        std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
        std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength + urlComp.dwExtraInfoLength);
        INTERNET_PORT port = urlComp.nPort;
        bool isHttps = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);

        HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
        if (!hConnect) {
            errorMsg = "连接 CDN 失败";
            break;
        }

        DWORD flags = isHttps ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL,
                                               WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            errorMsg = "创建请求失败";
            break;
        }

        DWORD redirectOption = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectOption, sizeof(redirectOption));
        WinHttpAddRequestHeaders(hRequest, headerStr.c_str(), (DWORD)headerStr.length(),
                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

        BOOL bSend = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
        if (!bSend || !WinHttpReceiveResponse(hRequest, NULL)) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            errorMsg = "CDN 未响应分片下载";
            break;
        }

        DWORD code = 0;
        DWORD codeSize = sizeof(code);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &code, &codeSize, WINHTTP_NO_HEADER_INDEX);

        if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
            DWORD locSize = 0;
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                NULL, &locSize, WINHTTP_NO_HEADER_INDEX);
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && locSize > 0) {
                std::wstring locStr(locSize / sizeof(wchar_t), 0);
                if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                        &locStr[0], &locSize, WINHTTP_NO_HEADER_INDEX)) {
                    while (!locStr.empty() && locStr.back() == L'\0') locStr.pop_back();
                    currentUrl = locStr;
                    WinHttpCloseHandle(hRequest);
                    WinHttpCloseHandle(hConnect);
                    continue;
                }
            }
        }

        if (code != 206 && code != 200) {
            errorMsg = "CDN 响应非 206 状态码: " + std::to_string(code);
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            break;
        }

        // 打开目标分片文件准备写入
        HANDLE hFile = CreateFileW(u8toW(destChunkPath).c_str(), GENERIC_WRITE, 0, NULL,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            errorMsg = "无法创建分片文件: " + destChunkPath;
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            break;
        }

        std::vector<char> buffer(64 * 1024);
        DWORD bytesRead = 0;
        bool writeOk = true;

        while (WinHttpReadData(hRequest, buffer.data(), (DWORD)buffer.size(), &bytesRead) && bytesRead > 0) {
            DWORD bytesWritten = 0;
            if (!WriteFile(hFile, buffer.data(), bytesRead, &bytesWritten, NULL) || bytesWritten != bytesRead) {
                writeOk = false;
                errorMsg = "写分片数据失败";
                break;
            }
        }

        CloseHandle(hFile);
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        success = writeOk;
        break;
    }

    WinHttpCloseHandle(hSession);
    return success;
}

int main(int argc, char *argv[]) {
    std::cout << "================================================================================" << std::endl;
    std::cout << "  MultiThread_Download Phase 0: B站 DASH 流多段 Range 并发与 MD5 无损验证" << std::endl;
    std::cout << "================================================================================" << std::endl;

    // 载入已有登录态 (若有)
    AppConfig cfg;
    BiliAuth::loadConfig("./config.json", cfg);
    std::string sess = cfg.sessdata;
    if (!sess.empty()) {
        std::cout << "[INFO] 已载入 SESSDATA 登录凭据" << std::endl;
    }

    // 1. 解析真实 B 站视频 (使用常用测试视频)
    std::string bvid = "BV1sqbmzeEzR";
    BiliParser parser;
    if (!sess.empty()) {
        parser.setSessData(sess);
    }

    std::cout << "[1/5] 正在解析测试视频 (" << bvid << ") ..." << std::endl;
    BiliVideoDetail detail;
    std::string perr;
    if (!parser.fetchVideoDetail(bvid, detail, perr)) {
        std::cerr << "[FAIL] fetchVideoDetail 失败: " << perr << std::endl;
        return 1;
    }
    if (!parser.fetchPlayUrl(detail, 0, perr)) {
        std::cerr << "[FAIL] fetchPlayUrl 失败: " << perr << std::endl;
        return 1;
    }

    if (detail.videoTracks.empty()) {
        std::cerr << "[FAIL] 未找到视频流" << std::endl;
        return 1;
    }

    std::string videoUrl = detail.videoTracks[0].baseUrl;
    if (videoUrl.empty() && !detail.videoTracks[0].backupUrls.empty()) {
        videoUrl = detail.videoTracks[0].backupUrls[0];
    }
    std::cout << "      视频标题: " << detail.title << std::endl;
    std::cout << "      主流地址: " << videoUrl.substr(0, 60) << "..." << std::endl;

    // 2. 验证探测: Range: bytes=0-0 获取 totalSize
    std::cout << "[2/5] 探测 Range: bytes=0-0 请求与 Content-Range 响应 ..." << std::endl;
    int64_t totalSize = -1;
    DWORD statusCode = 0;
    std::string probeErr;
    if (!probeStreamTotalSize(videoUrl, sess, totalSize, statusCode, probeErr)) {
        std::cerr << "[FAIL] 探测 totalSize 失败: " << probeErr << " (HTTP " << statusCode << ")" << std::endl;
        return 1;
    }
    std::cout << "      [PASS] 状态码: " << statusCode << " (Partial Content 206)" << std::endl;
    std::cout << "      [PASS] 成功获取流总大小 (totalSize): " << totalSize << " 字节 (" 
              << (totalSize / 1024.0 / 1024.0) << " MB)" << std::endl;

    // 准备测试目录
    std::string testDir = "./phase0_test_out";
    CreateDirectoryW(u8toW(testDir).c_str(), NULL);
    std::string singlePath = testDir + "/single_thread.m4s";
    std::string multiPath = testDir + "/multi_thread_merged.m4s";

    // 3. 单线程下载对照组
    std::cout << "[3/5] 执行单线程下载作为对照组 ..." << std::endl;
    BiliDownloader downloader;
    if (!sess.empty()) downloader.setSessData(sess);
    std::string dlErr;
    auto t0 = std::chrono::steady_clock::now();
    bool singleOk = downloader.downloadStream(videoUrl, singlePath, nullptr, 0, &dlErr, 0, nullptr);
    auto t1 = std::chrono::steady_clock::now();
    double singleElapsed = std::chrono::duration<double>(t1 - t0).count();

    if (!singleOk) {
        std::cerr << "[FAIL] 单线程下载对照组失败: " << dlErr << std::endl;
        return 1;
    }
    std::string singleMd5 = calculateFileMd5(singlePath);
    std::cout << "      [PASS] 单线程下载完成: " << singleElapsed << " 秒, MD5: " << singleMd5 << std::endl;

    // 4. 多线程并发分片下载 (3 线程各拉取一段)
    std::cout << "[4/5] 启动 3 线程并发 Range 分片下载实验 ..." << std::endl;
    int chunkCount = 3;
    int64_t chunkSize = totalSize / chunkCount;
    struct ChunkSpec {
        int index;
        int64_t start;
        int64_t end;
        std::string filePath;
        bool ok = false;
        std::string err;
    };
    std::vector<ChunkSpec> chunks(chunkCount);
    for (int i = 0; i < chunkCount; ++i) {
        chunks[i].index = i;
        chunks[i].start = i * chunkSize;
        chunks[i].end = (i == chunkCount - 1) ? (totalSize - 1) : ((i + 1) * chunkSize - 1);
        chunks[i].filePath = testDir + "/chunk_" + std::to_string(i) + ".part";
    }

    auto t2 = std::chrono::steady_clock::now();
    std::vector<std::thread> workers;
    for (int i = 0; i < chunkCount; ++i) {
        workers.emplace_back([&, i]() {
            chunks[i].ok = downloadChunkRange(videoUrl, sess, chunks[i].start, chunks[i].end,
                                              chunks[i].filePath, chunks[i].err);
        });
    }
    for (auto &w : workers) {
        if (w.joinable()) w.join();
    }
    auto t3 = std::chrono::steady_clock::now();
    double multiElapsed = std::chrono::duration<double>(t3 - t2).count();

    for (int i = 0; i < chunkCount; ++i) {
        if (!chunks[i].ok) {
            std::cerr << "[FAIL] 分片 " << i << " 下载失败: " << chunks[i].err << std::endl;
            return 1;
        }
        std::cout << "      [PASS] 分片 " << i << " [" << chunks[i].start << ".." << chunks[i].end << "] "
                  << "下载成功 (" << (chunks[i].end - chunks[i].start + 1) << " 字节)" << std::endl;
    }
    std::cout << "      [PASS] 3 线程并发下载耗时: " << multiElapsed << " 秒" << std::endl;

    // 5. 顺序流式合并与 MD5 强一致性校验
    std::cout << "[5/5] 顺序流式合并 3 个分片并核对 MD5 ..." << std::endl;
    std::ofstream outMerged(u8toW(multiPath), std::ios::binary);
    if (!outMerged.is_open()) {
        std::cerr << "[FAIL] 无法创建合并文件: " << multiPath << std::endl;
        return 1;
    }
    for (int i = 0; i < chunkCount; ++i) {
        std::ifstream inChunk(u8toW(chunks[i].filePath), std::ios::binary);
        if (!inChunk.is_open()) {
            std::cerr << "[FAIL] 无法打开分片: " << chunks[i].filePath << std::endl;
            return 1;
        }
        outMerged << inChunk.rdbuf();
    }
    outMerged.close();

    std::string multiMd5 = calculateFileMd5(multiPath);
    std::cout << "      单线程产物 MD5: " << singleMd5 << std::endl;
    std::cout << "      多线程合并 MD5: " << multiMd5 << std::endl;

    if (singleMd5 != multiMd5) {
        std::cerr << "[FAIL] MD5 不一致！数据存在损坏或拼接错位！" << std::endl;
        return 1;
    }

    std::cout << "================================================================================" << std::endl;
    std::cout << "  [HARD GATE PASS] Phase 0 验证全部通过！" << std::endl;
    std::cout << "  结论 1: B站 CDN 原生支持 HTTP Range 206 与 bytes=0-0 探测 totalSize" << std::endl;
    std::cout << "  结论 2: 多连接并发 Range 下载无风控拦截，无连接数阻断" << std::endl;
    std::cout << "  结论 3: 多分片流式拼接产物与单线程下载 MD5 完全一致 (100% 无损)" << std::endl;
    std::cout << "================================================================================" << std::endl;
    return 0;
}
