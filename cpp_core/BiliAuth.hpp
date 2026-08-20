#pragma once

#include <string>
#include <map>
#include <cstdint>
#include "BiliHttpClient.hpp"

// 扫码二维码信息
struct QrCodeInfo {
    std::string qrcodeKey;   // 用于轮询状态的 key
    std::string url;         // 二维码内容 URL (前端可将其渲染为二维码图片)
};

// 扫码状态枚举
enum class QrPollStatus {
    WAIT_SCAN = 86101,       // 未扫码
    WAIT_CONFIRM = 86090,    // 已扫码未确认
    SUCCESS = 0,             // 扫码成功
    EXPIRED = 86038,         // 二维码已失效
    ERROR_OCCURRED = -1      // 请求出错
};

// 账号会话状态 (对齐成熟项目: 精确区分网络错误 / 凭证过期 / 服务器异常,
// 供定时刷新决定是"静默跳过"、"refreshToken 自动续期"还是"降级要求重新登录")
enum class AuthError {
    OK = 0,        // 会话有效
    NETWORK = 1,   // 网络层失败 (断网 / 超时) → 保留旧资料, 下次再试
    EXPIRED = 2,   // SESSDATA 过期 / 未登录 → 尝试自动续期
    SERVER = 3     // 服务器返回异常 (JSON 解析失败 / 未知 code) → 视为临时故障
};

// 当前登录用户信息
struct UserProfile {
    bool isLogin = false;
    int64_t mid = 0;         // 用户 UID
    std::string uname;       // 昵称
    std::string face;        // 头像 URL
    int level = 0;           // 等级 (0-6)
    int vipType = 0;         // 0=无, 1=月度, 2=年度/大会员
    int vipStatus = 0;       // 0=未生效, 1=有效
    std::string vipLabel;    // "年度大会员", "大会员", "普通用户"
    double money = 0.0;      // 硬币数
};

// 本地持久化配置结构
struct AppConfig {
    std::string sessdata;
    std::string bili_jct;
    std::string dedeUserId;
    std::string refreshToken;
    std::string downloadPath = "./downloads";
    int defaultQuality = 120; // 默认画质优先 (120=4K, 80=1080P)
    int defaultCodec = 12;    // 默认编码优先 (12=HEVC, 13=AV1, 7=AVC)
    std::string themeColor = "#FF1E42"; // 默认激光红

    // 混流配置 (Bento4 内嵌无损 remux)
    bool muxEnabled = true;       // 全媒体下载完成后自动合并 video.m4s + audio.m4s, 成功后源 m4s 自动清理
    int maxDownloadThreads = 4;   // 多线程分片并发数 (1=关闭, 默认4)

    // 附属资源配置 (弹幕 danmaku.xml + 字幕 subtitle.srt)
    bool downloadSubRes = true;   // 下载视频时是否顺带下载弹幕与字幕, 默认开启保持原行为

    // 限速配置 (全局限速, 字节/秒; 0 = 不限速)
    int maxDownloadSpeedKB = 0;   // 下载限速 (KB/s, 0=不限速)

    // 下载完成提示音 (用户自定义本地音频文件路径)
    // 受版权音频不入库, 由用户自备文件并配置路径, 空路径 = 静默
    bool completionSoundEnabled = true;
    std::string completionSoundPath;   // 支持 mp3/wav 等本地音频
};

class BiliAuth {
public:
    BiliAuth();
    ~BiliAuth();

    // 1. 生成登录二维码信息 (给 QML 前端展示)
    bool generateQrCode(QrCodeInfo &qrInfo, std::string &errorMsg);

    // 2. 轮询二维码扫码状态 (QML 前端每隔 2 秒调用一次)
    QrPollStatus pollQrCode(const std::string &qrcodeKey, AppConfig &outConfig, std::string &message);

    // 3. 获取当前登录账号的详细信息 (大会员状态、昵称、等级、头像)
    bool fetchUserProfile(const std::string &sessdata, UserProfile &profile, std::string &errorMsg);
    bool fetchUserProfile(const AppConfig &config, UserProfile &profile, std::string &errorMsg);

    // 4. 会话健康检查: 返回精确错误分类 (OK / NETWORK / EXPIRED / SERVER)
    //    供定时刷新机制判断是否需要 refreshToken 自动续期或降级要求重新登录
    AuthError checkSession(const AppConfig &config, UserProfile &profile, std::string &errorMsg);

    // 5. refreshToken 自动续期: SESSDATA 过期时用 refresh_token 换取新凭证,
    //    成功后原地更新 config (新 SESSDATA / bili_jct / DedeUserID / refresh_token)
    bool refreshSession(AppConfig &config, std::string &errorMsg);

    // 6. 本地持久化配置管理 (读取与保存 config.json)
    static bool loadConfig(const std::string &filePath, AppConfig &config);
    static bool saveConfig(const std::string &filePath, const AppConfig &config);

private:
    BiliHttpClient m_client;
};
