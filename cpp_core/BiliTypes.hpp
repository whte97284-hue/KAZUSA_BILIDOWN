#pragma once

#include <string>
#include <vector>
#include <cstdint>

// 视频清晰度定义 (与 B 站官方 Quality ID 严格对齐)
enum class QualityId : int {
    Q_8K = 127,
    Q_DOLBY_VISION = 126,
    Q_HDR = 125,
    Q_4K = 120,
    Q_1080P60 = 116,
    Q_1080P_PLUS = 112,
    Q_1080P = 80,
    Q_720P60 = 74,
    Q_720P = 64,
    Q_480P = 32,
    Q_360P = 16
};

// 视频编码格式定义
enum class CodecId : int {
    AVC = 7,       // H.264 / AVC (最兼容)
    HEVC = 12,     // H.265 / HEVC (高画质低码率)
    AV1 = 13       // AV1 (新一代下一代开源编码)
};

// 视频流轨道详细信息
struct VideoTrack {
    int quality = 0;                     // 127, 120, 116, 80...
    std::string qualityDesc;             // "4K 超清", "1080P 60帧", "1080P 高清"...
    int codecId = 0;                     // 7, 12, 13
    std::string codecName;               // "AV1", "HEVC", "AVC"
    int width = 0;
    int height = 0;
    int fps = 0;
    int bandwidth = 0;                   // 码率 (bps)
    std::string baseUrl;                 // 主 CDN 真实直链
    std::vector<std::string> backupUrls; // 备用 CDN 直链列表 (提高下载容灾)
};

// 音频流轨道详细信息
struct AudioTrack {
    int id = 0;                          // 30280=192K, 30232=132K, 30216=64K, 30250=杜比, 30255=Hi-Res
    std::string qualityName;             // "Hi-Res 无损", "杜比全景声", "192K 高码率"...
    int bandwidth = 0;
    std::string baseUrl;                 // 主音频 CDN 直链
    std::vector<std::string> backupUrls; // 备用音频 CDN 直链
};

// 字幕流信息
struct SubtitleTrack {
    int64_t id = 0;
    std::string lang;                    // "zh-CN", "zh-TW", "en-US"
    std::string langDoc;                 // "中文（简体）", "English"
    std::string subtitleUrl;             // 字幕 json / bcc / srt 直链
};

// 分 P / 分集信息 (UGC 分 P 或 番剧 Episode)
struct VideoPage {
    int page = 1;                        // P 数序号
    int64_t cid = 0;                     // 该 P 的唯一 CID
    std::string part;                    // 该 P 的分集标题
    int64_t duration = 0;                // 该 P 时长 (秒)
    std::string firstFrame;              // 封面图
};

// 视频全量解析元数据
struct BiliVideoDetail {
    std::string bvid;                    // BV 号
    int64_t aid = 0;                     // AV 号
    std::string title;                   // 视频总标题
    std::string desc;                    // 视频简介
    std::string coverUrl;                // 封面图 URL
    std::string ownerName;               // UP 主昵称 / 出品方
    int64_t ownerMid = 0;                // UP 主 UID
    int64_t pubdate = 0;                 // 发布时间戳
    bool isBangumi = false;              // 是否为番剧 / 影视 (PGC)
    std::string seasonTitle;             // 所属番剧季名称 (仅番剧)
    double score = 0.0;                  // 评分 (如 9.2)
    std::string seasonInfo;              // 组合标签 (如 "全 24 话 · 日本 · 2026")
    std::string views;                   // 格式化播放量 (如 "1.2亿")
    
    std::vector<VideoPage> pages;        // 所有分 P 列表
    int selectedPageIndex = 0;           // 当前选中的 P 索引 (默认 0)

    // 当前选定分 P 的流媒体轨道
    std::vector<VideoTrack> videoTracks;
    std::vector<AudioTrack> audioTracks;
    std::vector<SubtitleTrack> subtitles;

    // 单流 MP4 标识: durl 单文件 (番剧传统 MP4 / fnval=1 降级流)。
    // 音视频已内嵌在同一文件, 无独立音频轨, 下载后无需混流。
    bool singleStreamMp4 = false;

    // 会员试看流标识: 非大会员请求 VIP 专享剧集 → 仅 6 分钟试看 (error_code=-10403 + is_preview=1)
    bool isPreview = false;
};

// 解析输入类型
enum class InputType {
    BVID,       // BV1xx
    AVID,       // av12345 / 12345
    EP_ID,      // ep12345 (番剧分集)
    SS_ID,      // ss12345 (番剧季)
    URL,        // https://...
    SHORT_URL   // https://b23.tv/...
};

struct ParsedInput {
    InputType type = InputType::BVID;
    std::string idValue;
    int page = 1;
};
