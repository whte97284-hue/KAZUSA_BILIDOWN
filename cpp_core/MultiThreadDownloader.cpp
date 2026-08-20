#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "MultiThreadDownloader.hpp"
#include "RateLimiter.hpp"
#include "BiliHttpClient.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <chrono>
#include <algorithm>
#include <windows.h>
#include <winhttp.h>

static std::wstring u8toW(const std::string &u8) {
    if (u8.empty()) return L"";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), (int)u8.size(), NULL, 0);
    if (wlen <= 0) return L"";
    std::wstring ws(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), (int)u8.size(), &ws[0], wlen);
    return ws;
}

// 容器魔数快速校验 (复用 BiliDownloader 实现)
static bool isValidMediaHeader(const char *buf, size_t len) {
    if (len < 8) return false;
    std::string_view header(buf, std::min(len, size_t(32)));
    if (header.find("ftyp") != std::string_view::npos ||
        header.find("moov") != std::string_view::npos ||
        header.find("moof") != std::string_view::npos ||
        header.find("styp") != std::string_view::npos ||
        header.find("mdat") != std::string_view::npos ||
        header.find("sidx") != std::string_view::npos) return true;
    if (buf[0] == '{' || buf[0] == '<') return false;
    return false;
}

MultiThreadDownloader::MultiThreadDownloader() {}
MultiThreadDownloader::~MultiThreadDownloader() {}

void MultiThreadDownloader::setSessData(const std::string &sessData) {
    m_sessData = sessData;
}

void MultiThreadDownloader::setRateLimiter(RateLimiter *limiter) {
    m_rateLimiter = limiter;
}

bool MultiThreadDownloader::probeStreamSize(const std::string &streamUrl, int64_t &outTotalSize, std::string &errorMsg) {
    outTotalSize = -1;
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
    WinHttpSetTimeouts(hSession, 8000, 8000, 10000, 10000);

    std::wstring headerStr = L"Referer: https://www.bilibili.com/\r\n"
                             L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)\r\n"
                             L"Accept-Encoding: identity\r\n"
                             L"Range: bytes=0-0\r\n";
    if (!m_sessData.empty()) {
        std::string cookie = "Cookie: SESSDATA=" + m_sessData + "\r\n";
        headerStr += std::wstring(cookie.begin(), cookie.end());
    }

    std::wstring currentUrl(url.begin(), url.end());
    bool ok = false;

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
            errorMsg = "创建 HTTP 请求失败";
            break;
        }

        DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

        if (!WinHttpSendRequest(hRequest, headerStr.c_str(), (DWORD)headerStr.length(),
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(hRequest, NULL)) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            errorMsg = "网络请求失败";
            break;
        }

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

        if (statusCode == 301 || statusCode == 302 || statusCode == 303 || statusCode == 307 || statusCode == 308) {
            DWORD locLen = 0;
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, NULL, &locLen, WINHTTP_NO_HEADER_INDEX);
            if (locLen > 0) {
                std::wstring locBuf(locLen / sizeof(wchar_t), 0);
                if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, &locBuf[0], &locLen, WINHTTP_NO_HEADER_INDEX)) {
                    currentUrl = locBuf;
                    WinHttpCloseHandle(hRequest);
                    WinHttpCloseHandle(hConnect);
                    continue;
                }
            }
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            errorMsg = "重定向地址获取失败";
            break;
        }

        if (statusCode == 206) {
            DWORD crLen = 0;
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CUSTOM, L"Content-Range", NULL, &crLen, WINHTTP_NO_HEADER_INDEX);
            if (crLen > 0) {
                std::wstring crBuf(crLen / sizeof(wchar_t), 0);
                if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CUSTOM, L"Content-Range", &crBuf[0], &crLen, WINHTTP_NO_HEADER_INDEX)) {
                    // Content-Range 值始终是纯 ASCII (如 "bytes 0-1023/20480"), 直接按宽字符解析, 避免 wchar→char 丢失转换
                    crBuf.resize(crLen / sizeof(wchar_t));
                    size_t slashPos = crBuf.rfind(L'/');
                    if (slashPos != std::wstring::npos) {
                        try {
                            outTotalSize = std::stoll(crBuf.substr(slashPos + 1));
                            ok = (outTotalSize > 0);
                        } catch (...) {
                            ok = false;
                        }
                    }
                }
            }
        } else if (statusCode == 200) {
            DWORD clLen = sizeof(DWORD);
            DWORD cl = 0;
            if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &cl, &clLen, WINHTTP_NO_HEADER_INDEX)) {
                outTotalSize = cl;
                ok = (outTotalSize > 0);
            }
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        break;
    }

    WinHttpCloseHandle(hSession);
    return ok;
}

bool MultiThreadDownloader::downloadChunk(
    const std::string &url,
    int64_t startByte,
    int64_t endByte,
    const std::string &chunkFilePath,
    int chunkIndex,
    std::atomic<int64_t> &totalDownloadedBytes,
    std::string &err,
    std::atomic<bool> *cancel
) {
    int64_t expectedSize = endByte - startByte + 1;
    
    // 检查断点续传: 是否已存在且已完整
    std::wstring wChunkPath = u8toW(chunkFilePath);
    WIN32_FILE_ATTRIBUTE_DATA fileAttr;
    int64_t existingBytes = 0;
    if (GetFileAttributesExW(wChunkPath.c_str(), GetFileExInfoStandard, &fileAttr)) {
        existingBytes = (static_cast<int64_t>(fileAttr.nFileSizeHigh) << 32) | fileAttr.nFileSizeLow;
        if (existingBytes == expectedSize) {
            totalDownloadedBytes += existingBytes;
            return true; // 分片已完整，直接跳过
        } else if (existingBytes > expectedSize) {
            DeleteFileW(wChunkPath.c_str());
            existingBytes = 0;
        } else {
            totalDownloadedBytes += existingBytes;
        }
    }

    int64_t reqStart = startByte + existingBytes;
    if (reqStart > endByte) return true;

    // 逐分片重试最多 3 次 (指数退避)
    for (int retry = 0; retry < 3; ++retry) {
        if (cancel && cancel->load()) {
            err = "下载被取消";
            return false;
        }
        if (retry > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300 * (1 << (retry - 1))));
        }

        std::wstring proxyServer;
        bool hasProxy = BiliHttpClient::detectSystemProxy(proxyServer);
        HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
                                         hasProxy ? WINHTTP_ACCESS_TYPE_NAMED_PROXY : WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                         hasProxy ? proxyServer.c_str() : WINHTTP_NO_PROXY_NAME,
                                         WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) continue;
        WinHttpSetTimeouts(hSession, 10000, 10000, 20000, 20000);

        std::string rangeHdr = "Range: bytes=" + std::to_string(reqStart) + "-" + std::to_string(endByte) + "\r\n";
        std::wstring headerStr = L"Referer: https://www.bilibili.com/\r\n"
                                 L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)\r\n"
                                 L"Accept-Encoding: identity\r\n";
        headerStr += std::wstring(rangeHdr.begin(), rangeHdr.end());
        if (!m_sessData.empty()) {
            std::string cookie = "Cookie: SESSDATA=" + m_sessData + "\r\n";
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

            if (!WinHttpCrackUrl(currentUrl.c_str(), (DWORD)currentUrl.length(), 0, &urlComp)) break;

            std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
            std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength + urlComp.dwExtraInfoLength);
            HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), urlComp.nPort, 0);
            if (!hConnect) break;

            DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
            HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL,
                                                   WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
            if (!hRequest) { WinHttpCloseHandle(hConnect); break; }

            DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
            WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

            if (!WinHttpSendRequest(hRequest, headerStr.c_str(), (DWORD)headerStr.length(),
                                    WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
                !WinHttpReceiveResponse(hRequest, NULL)) {
                WinHttpCloseHandle(hRequest);
                WinHttpCloseHandle(hConnect);
                break;
            }

            DWORD statusCode = 0;
            DWORD statusSize = sizeof(statusCode);
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

            if (statusCode == 301 || statusCode == 302 || statusCode == 303 || statusCode == 307 || statusCode == 308) {
                DWORD locLen = 0;
                WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, NULL, &locLen, WINHTTP_NO_HEADER_INDEX);
                if (locLen > 0) {
                    std::wstring locBuf(locLen / sizeof(wchar_t), 0);
                    if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, &locBuf[0], &locLen, WINHTTP_NO_HEADER_INDEX)) {
                        currentUrl = locBuf;
                        WinHttpCloseHandle(hRequest);
                        WinHttpCloseHandle(hConnect);
                        continue;
                    }
                }
                WinHttpCloseHandle(hRequest);
                WinHttpCloseHandle(hConnect);
                break;
            }

            if (statusCode != 206 && statusCode != 200) {
                err = "CDN 返回异常状态码: " + std::to_string(statusCode);
                WinHttpCloseHandle(hRequest);
                WinHttpCloseHandle(hConnect);
                break;
            }

            // 打开文件写入 (追加模式)
            std::ofstream outFile(wChunkPath, std::ios::binary | std::ios::app);
            if (!outFile.is_open()) {
                err = "无法创建分片文件: " + chunkFilePath;
                WinHttpCloseHandle(hRequest);
                WinHttpCloseHandle(hConnect);
                break;
            }

            std::vector<char> buffer(64 * 1024);
            DWORD bytesRead = 0;
            bool isFirstRead = (existingBytes == 0);
            bool readOk = true;

            while (WinHttpReadData(hRequest, buffer.data(), (DWORD)buffer.size(), &bytesRead) && bytesRead > 0) {
                if (cancel && cancel->load()) {
                    readOk = false;
                    err = "下载已取消";
                    break;
                }

                // 首次读取且为分片 0 时做魔数校验
                if (isFirstRead && chunkIndex == 0) {
                    if (!isValidMediaHeader(buffer.data(), bytesRead)) {
                        readOk = false;
                        err = "魔数校验失败: CDN 返回了伪 200 拦截页面";
                        break;
                    }
                    isFirstRead = false;
                }

                // 限速钩子
                if (m_rateLimiter) {
                    m_rateLimiter->acquire(bytesRead);
                }

                outFile.write(buffer.data(), bytesRead);
                totalDownloadedBytes += bytesRead;
            }

            outFile.close();
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);

            if (readOk) {
                // 校验分片实际大小
                if (GetFileAttributesExW(wChunkPath.c_str(), GetFileExInfoStandard, &fileAttr)) {
                    int64_t actualSize = (static_cast<int64_t>(fileAttr.nFileSizeHigh) << 32) | fileAttr.nFileSizeLow;
                    if (actualSize == expectedSize) {
                        success = true;
                    } else {
                        err = "分片下载未完成: " + std::to_string(actualSize) + "/" + std::to_string(expectedSize);
                    }
                }
            }
            break;
        }

        WinHttpCloseHandle(hSession);
        if (success) return true;
    }

    return false;
}

bool MultiThreadDownloader::mergeChunks(
    const std::vector<std::string> &chunkFiles,
    const std::string &tempPartFile,
    const std::string &finalDestFile,
    std::string &err
) {
    std::wstring wPart = u8toW(tempPartFile);
    std::ofstream outMerged(wPart, std::ios::binary);
    if (!outMerged.is_open()) {
        err = "无法创建合并目标文件: " + tempPartFile;
        return false;
    }

    std::vector<char> buffer(4 * 1024 * 1024); // 4MB 流式缓冲区
    for (const auto &chunkPath : chunkFiles) {
        std::wstring wChunk = u8toW(chunkPath);
        std::ifstream inChunk(wChunk, std::ios::binary);
        if (!inChunk.is_open()) {
            outMerged.close();
            err = "无法读取分片文件: " + chunkPath;
            return false;
        }
        while (inChunk.read(buffer.data(), buffer.size()) || inChunk.gcount() > 0) {
            outMerged.write(buffer.data(), inChunk.gcount());
        }
        inChunk.close();
    }
    outMerged.close();

    // 删除分片临时文件
    for (const auto &chunkPath : chunkFiles) {
        DeleteFileW(u8toW(chunkPath).c_str());
    }

    // 原子改名至最终文件
    std::wstring wFinal = u8toW(finalDestFile);
    DeleteFileW(wFinal.c_str()); // 若已存在先删除
    if (!MoveFileExW(wPart.c_str(), wFinal.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        err = "原子重命名最终文件失败: " + finalDestFile;
        return false;
    }

    return true;
}

bool MultiThreadDownloader::downloadStreamParallel(
    const std::string &streamUrl,
    const std::vector<std::string> &backupUrls,
    const std::string &destFilePath,
    int maxThreads,
    ProgressCallback progressCb,
    std::string *errorMsg,
    std::atomic<bool> *cancel
) {
    std::vector<std::string> allUrls;
    allUrls.push_back(streamUrl);
    for (const auto &bUrl : backupUrls) {
        if (!bUrl.empty()) allUrls.push_back(bUrl);
    }

    std::string tempPartFile = destFilePath + ".part";

    // 逐个 CDN 尝试 (主 CDN + 备用 CDN 整体容灾)
    for (size_t cdnIdx = 0; cdnIdx < allUrls.size(); ++cdnIdx) {
        const std::string &currentCdnUrl = allUrls[cdnIdx];
        if (cancel && cancel->load()) {
            if (errorMsg) *errorMsg = "下载被取消";
            return false;
        }

        int64_t totalSize = -1;
        std::string probeErr;
        bool probeOk = probeStreamSize(currentCdnUrl, totalSize, probeErr);

        // 如果文件小于 5MB 或探测不支持 Range 或配置线程数 <= 1，直接退化单线程
        const int64_t minChunkSize = 5 * 1024 * 1024;
        if (!probeOk || totalSize < minChunkSize || maxThreads <= 1) {
            BiliDownloader fallbackDownloader;
            if (!m_sessData.empty()) fallbackDownloader.setSessData(m_sessData);
            fallbackDownloader.setRateLimiter(m_rateLimiter); // 继承限速钩子
            return fallbackDownloader.downloadStream(currentCdnUrl, destFilePath, progressCb,
                                                     0, errorMsg, 0, cancel);
        }

        // 计算动态分片数
        int chunkCount = std::min(maxThreads, static_cast<int>(totalSize / minChunkSize));
        if (chunkCount < 1) chunkCount = 1;
        if (chunkCount == 1) {
            BiliDownloader fallbackDownloader;
            if (!m_sessData.empty()) fallbackDownloader.setSessData(m_sessData);
            fallbackDownloader.setRateLimiter(m_rateLimiter); // 继承限速钩子
            return fallbackDownloader.downloadStream(currentCdnUrl, destFilePath, progressCb,
                                                     0, errorMsg, 0, cancel);
        }

        int64_t chunkSize = totalSize / chunkCount;
        struct ChunkMeta {
            int index;
            int64_t start;
            int64_t end;
            std::string filePath;
            bool ok = false;
            std::string err;
        };

        std::vector<ChunkMeta> chunks(chunkCount);
        std::vector<std::string> chunkFilePaths;
        for (int i = 0; i < chunkCount; ++i) {
            chunks[i].index = i;
            chunks[i].start = i * chunkSize;
            chunks[i].end = (i == chunkCount - 1) ? (totalSize - 1) : ((i + 1) * chunkSize - 1);
            chunks[i].filePath = destFilePath + ".part." + std::to_string(i);
            chunkFilePaths.push_back(chunks[i].filePath);
        }

        std::atomic<int64_t> totalDownloaded(0);
        std::atomic<bool> threadCancel(false);

        // 启动进度监听定时器线程
        std::atomic<bool> progressRunning(true);
        std::thread progressThread([&]() {
            int64_t lastBytes = 0;
            auto lastTime = std::chrono::steady_clock::now();
            while (progressRunning.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (cancel && cancel->load()) {
                    threadCancel = true;
                    break;
                }
                int64_t currentBytes = totalDownloaded.load();
                auto now = std::chrono::steady_clock::now();
                double elapsed = std::chrono::duration<double>(now - lastTime).count();
                if (elapsed >= 0.1 && progressCb) {
                    double speed = (currentBytes - lastBytes) / elapsed;
                    double pct = (totalSize > 0) ? (static_cast<double>(currentBytes) / totalSize * 100.0) : 0.0;
                    progressCb(currentBytes, totalSize, speed, pct);
                    lastBytes = currentBytes;
                    lastTime = now;
                }
            }
        });

        // 并发拉取各分片
        std::vector<std::thread> workers;
        for (int i = 0; i < chunkCount; ++i) {
            workers.emplace_back([&, i]() {
                chunks[i].ok = downloadChunk(
                    currentCdnUrl,
                    chunks[i].start,
                    chunks[i].end,
                    chunks[i].filePath,
                    chunks[i].index,
                    totalDownloaded,
                    chunks[i].err,
                    cancel
                );
            });
        }

        for (auto &w : workers) {
            if (w.joinable()) w.join();
        }

        progressRunning = false;
        if (progressThread.joinable()) progressThread.join();

        // 检查所有分片是否成功
        bool allChunksOk = true;
        std::string firstErr;
        for (int i = 0; i < chunkCount; ++i) {
            if (!chunks[i].ok) {
                allChunksOk = false;
                if (firstErr.empty()) firstErr = chunks[i].err;
            }
        }

        if (allChunksOk) {
            std::string mergeErr;
            if (mergeChunks(chunkFilePaths, tempPartFile, destFilePath, mergeErr)) {
                if (progressCb) {
                    progressCb(totalSize, totalSize, 0, 100.0);
                }
                return true;
            } else {
                if (errorMsg) *errorMsg = mergeErr;
                return false;
            }
        } else {
            if (errorMsg) *errorMsg = firstErr;
            // 失败时进入下一个 CDN 尝试
            continue;
        }
    }

    if (errorMsg && errorMsg->empty()) *errorMsg = "所有 CDN 下载均失败";
    return false;
}
