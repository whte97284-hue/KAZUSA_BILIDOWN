#pragma once

#include "BiliTypes.hpp"
#include "BiliHttpClient.hpp"
#include "RateLimiter.hpp"
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <atomic>

// 弹幕单条结构
struct DanmakuEntry {
    double timeSec = 0.0;    // 视频内出现时间 (秒)
    int mode = 1;            // 弹幕模式 (1-3滚动, 4底部, 5顶部)
    int fontSize = 25;       // 字号
    uint32_t color = 0xFFFFFF; // 颜色
    int64_t timestamp = 0;   // 发送时间戳
    std::string senderHash;  // 发送者 CRC32 Hash
    std::string content;     // 弹幕文本
};

// 下载进度回调函数类型: (已下载字节, 总字节, 速度字节/秒, 进度百分比 0.0-100.0)
using ProgressCallback = std::function<void(int64_t downloadedBytes, int64_t totalBytes, double speedBps, double percent)>;

class BiliDownloader {
public:
    BiliDownloader();
    ~BiliDownloader();

    void setSessData(const std::string &sessData);

    // 注入可选的全局限速器 (多线程/单线程共用, nullptr=不限速)
    void setRateLimiter(RateLimiter *limiter) { m_rateLimiter = limiter; }

    // 1. 下载封面图片并保存到文件
    bool downloadCover(const std::string &coverUrl, const std::string &destFilePath, std::string &errorMsg);

    // 2. 下载并解析弹幕池 (输出 XML 文件并返回结构化弹幕列表)
    bool downloadDanmaku(int64_t cid, const std::string &destXmlPath, std::vector<DanmakuEntry> &entries, std::string &errorMsg);

    // 2.5 下载字幕并转换为 SRT 文件 (优先中文轨, 无中文取第一条; 失败不阻塞主任务)
    bool downloadSubtitle(const std::vector<SubtitleTrack> &subtitles, const std::string &destSrtPath, std::string &errorMsg);

    // 3. 真实流媒体下载 (支持大文件流式拉取、限速/限长测试、断点续传、实时测速、可取消)
    // maxBytes   > 0 时仅下载前 maxBytes 字节 (用于极速验证文件头有效性)
    // resumeOffset> 0 时携带 Range: bytes=N- 并从 N 续传 (append 模式; 服务器回 200 则自动从头覆盖)
    // cancel     非空时循环内检查取消标记, 置位立即中断并返回失败 (errorMsg="已取消")
    bool downloadStream(
        const std::string &streamUrl, 
        const std::string &destFilePath, 
        ProgressCallback progressCb = nullptr, 
        int64_t maxBytes = 0,
        std::string *errorMsg = nullptr,
        int64_t resumeOffset = 0,
        std::atomic<bool> *cancel = nullptr
    );

    // 4. 完整的单 P 媒体包下载 (封面 + 可选弹幕/字幕 + 视频轨 + 音频轨)
    bool downloadEpisodePackage(
        const BiliVideoDetail &detail, 
        int pageIndex, 
        const std::string &outputDir, 
        int targetQuality = 0, 
        int targetCodecId = 0,
        bool downloadSubRes = true,   // 是否顺带下载弹幕 danmaku.xml 与字幕 subtitle.srt
        ProgressCallback videoProgressCb = nullptr,
        ProgressCallback audioProgressCb = nullptr,
        std::string *errorMsg = nullptr
    );

private:
    BiliHttpClient m_client;
    RateLimiter *m_rateLimiter = nullptr; // 全局限速器 (由上层注入)

    // 解析 XML 弹幕内容
    bool parseDanmakuXml(const std::string &xmlData, std::vector<DanmakuEntry> &entries);
};
