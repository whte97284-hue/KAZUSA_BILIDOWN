#pragma once

#include "BiliDownloader.hpp"
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <cstdint>

// 前置声明: 全局速率控制限速器 (由另一执行方注入, 本引擎仅调用 acquire 钩子)
class RateLimiter;

/**
 * @brief 多线程分片并发下载引擎 (MultiThreadDownloader)
 * 对标 BBDown -mt / aria2 并发加速，把单个媒体流 (video.m4s / audio.m4s)
 * 按 5MB 粒度动态切分为 N 个 Range 片段并行拉取，显著提升大文件下载吞吐。
 * 
 * 严格保留现有防线：
 * 1. 伪 200 拦截与首片魔数校验 (isValidMediaChunk)
 * 2. 手动重定向跟踪 (WINHTTP_OPTION_REDIRECT_POLICY_NEVER)
 * 3. Referer/Cookie/Accept-Encoding 逐跳透传
 * 4. 分片级断点续传 (.part.N) 与流式合并
 * 5. 全局取消与 CDN 容灾
 */
class MultiThreadDownloader {
public:
    MultiThreadDownloader();
    ~MultiThreadDownloader();

    // 设置登录凭证 (SESSDATA)
    void setSessData(const std::string &sessData);

    // 注入可选的全局速率限制器
    void setRateLimiter(RateLimiter *limiter);

    /**
     * @brief 探测媒体流真实大小 (通过 GET Range: bytes=0-0)
     * @param streamUrl 媒体直链
     * @param outTotalSize 输出总字节数 (若不支持 Range 返回 -1)
     * @param errorMsg 错误信息输出
     * @return true 探测成功且支持 Range 206
     */
    bool probeStreamSize(const std::string &streamUrl, int64_t &outTotalSize, std::string &errorMsg);

    /**
     * @brief 多线程分片并发下载媒体流
     * @param streamUrl 主 CDN 媒体直链
     * @param backupUrls 备用 CDN 媒体直链列表 (容灾备用)
     * @param destFilePath 最终文件保存路径 (内部走 .part 及分片临时文件)
     * @param maxThreads 最大并发线程数 (<=1 或文件<5MB 自动走单线程)
     * @param progressCb 进度回调函数 (downloadedBytes, totalBytes, speedBps, percent)
     * @param errorMsg 错误详情输出
     * @param cancel 取消标志指针
     * @return true 下载并合并成功, false 失败
     */
    bool downloadStreamParallel(
        const std::string &streamUrl,
        const std::vector<std::string> &backupUrls,
        const std::string &destFilePath,
        int maxThreads,
        ProgressCallback progressCb,
        std::string *errorMsg = nullptr,
        std::atomic<bool> *cancel = nullptr
    );

private:
    std::string m_sessData;
    RateLimiter *m_rateLimiter = nullptr;

    // 单分片下载工作函数
    bool downloadChunk(
        const std::string &url,
        int64_t startByte,
        int64_t endByte,
        const std::string &chunkFilePath,
        int chunkIndex,
        std::atomic<int64_t> &totalDownloadedBytes,
        std::string &err,
        std::atomic<bool> *cancel
    );

    // 流式合并各分片
    bool mergeChunks(
        const std::vector<std::string> &chunkFiles,
        const std::string &tempPartFile,
        const std::string &finalDestFile,
        std::string &err
    );
};
