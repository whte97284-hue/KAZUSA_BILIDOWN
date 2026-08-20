#include "BiliAuth.hpp"
#include "json.hpp"
#include <fstream>
#include <sstream>
#include <QDebug>
#include <iostream>
#include <regex>

using json = nlohmann::json;

// 从 Set-Cookie 响应头串中提取指定 Cookie 值 (QR 登录与 refreshToken 续期共用)
static std::string extractCookieValue(const std::vector<std::string> &cookies,
                                      const std::string &name) {
    std::regex reg("(?:^|[;\\s])" + name + "=([^;\\r\\n]+)");
    for (const auto &c : cookies) {
        std::smatch m;
        if (std::regex_search(c, m, reg)) {
            return m[1].str();
        }
    }
    return "";
}

BiliAuth::BiliAuth() {}
BiliAuth::~BiliAuth() {}

// 1. 生成登录二维码
bool BiliAuth::generateQrCode(QrCodeInfo &qrInfo, std::string &errorMsg) {
    std::string url = "https://passport.bilibili.com/x/passport-login/web/qrcode/generate";
    std::string rawJson;

    if (!m_client.get(url, rawJson, {}, 10)) {
        errorMsg = "请求生成二维码失败";
        return false;
    }

    try {
        auto root = json::parse(rawJson);
        if (root.value("code", -1) != 0) {
            errorMsg = root.value("message", "生成二维码失败");
            return false;
        }

        auto data = root["data"];
        qrInfo.qrcodeKey = data.value("qrcode_key", "");
        qrInfo.url = data.value("url", "");
        return !qrInfo.qrcodeKey.empty() && !qrInfo.url.empty();
    } catch (const std::exception &e) {
        errorMsg = std::string("JSON 解析异常: ") + e.what();
        return false;
    }
}

// 2. 轮询二维码状态
QrPollStatus BiliAuth::pollQrCode(const std::string &qrcodeKey, AppConfig &outConfig, std::string &message) {
    std::string url = "https://passport.bilibili.com/x/passport-login/web/qrcode/poll?qrcode_key=" + qrcodeKey;
    std::string rawJson;
    std::vector<std::string> setCookies;

    if (!m_client.get(url, rawJson, setCookies, {}, 10)) {
        message = "网络请求失败";
        return QrPollStatus::ERROR_OCCURRED;
    }

    try {
        auto root = json::parse(rawJson);
        auto data = root["data"];
        int code = data.value("code", -1);
        message = data.value("message", "");

        if (code == 0) {
            // 新版 B 站 API: 凭证通过 Set-Cookie 响应头返回
            // 格式: "SESSDATA=abc%2Cdef; Path=/; Domain=.bilibili.com; ..."
            outConfig.sessdata = extractCookieValue(setCookies, "SESSDATA");
            outConfig.bili_jct = extractCookieValue(setCookies, "bili_jct");
            outConfig.dedeUserId = extractCookieValue(setCookies, "DedeUserID");

            // 兼容旧版: 如果 Set-Cookie 为空则回退到 URL 参数提取
            if (outConfig.sessdata.empty()) {
                std::string redirectUrl = data.value("url", "");
                auto extractParam = [](const std::string &urlStr, const std::string &key) -> std::string {
                    std::regex reg("[?&]" + key + "=([^&]+)");
                    std::smatch match;
                    if (std::regex_search(urlStr, match, reg)) {
                        return match[1].str();
                    }
                    return "";
                };
                outConfig.sessdata = extractParam(redirectUrl, "SESSDATA");
                outConfig.bili_jct = extractParam(redirectUrl, "bili_jct");
                outConfig.dedeUserId = extractParam(redirectUrl, "DedeUserID");
            }

            // 保存 refresh_token (新版 API 提供, 用于凭证续期)
            if (data.contains("refresh_token")) {
                outConfig.refreshToken = data.value("refresh_token", "");
            }

            qDebug() << "[QR-LOGIN] pollQrCode SUCCESS"
                     << "sessdata.len=" << outConfig.sessdata.size()
                     << "bili_jct.len=" << outConfig.bili_jct.size()
                     << "DedeUserID=" << QString::fromStdString(outConfig.dedeUserId)
                     << "setCookies.count=" << setCookies.size();
            for (size_t i = 0; i < setCookies.size(); ++i) {
                qDebug() << "  Set-Cookie[" << i << "]=" << QString::fromStdString(setCookies[i]).left(80);
            }
            return QrPollStatus::SUCCESS;
        } else if (code == 86101) {
            return QrPollStatus::WAIT_SCAN;
        } else if (code == 86090) {
            return QrPollStatus::WAIT_CONFIRM;
        } else if (code == 86038) {
            return QrPollStatus::EXPIRED;
        }

        return QrPollStatus::ERROR_OCCURRED;
    } catch (const std::exception &e) {
        message = e.what();
        return QrPollStatus::ERROR_OCCURRED;
    }
}

// 3. 获取用户完整画像 (账号昵称、大会员状态、头像等)
bool BiliAuth::fetchUserProfile(const std::string &sessdata, UserProfile &profile, std::string &errorMsg) {
    std::string url = "https://api.bilibili.com/x/web-interface/nav";
    std::string rawJson;

    BiliHttpClient client;
    client.setSessData(sessdata);

    if (!client.get(url, rawJson, {}, 10)) {
        errorMsg = "网络连接失败";
        return false;
    }

    try {
        auto root = json::parse(rawJson);
        if (root.value("code", -1) != 0) {
            errorMsg = root.value("message", "获取个人资料失败");
            return false;
        }

        auto data = root["data"];
        profile.isLogin = data.value("isLogin", false);
        if (!profile.isLogin) {
            errorMsg = "未登录或 SESSDATA 已过期";
            return false;
        }

        profile.mid = data.value("mid", (int64_t)0);
        profile.uname = data.value("uname", "B站用户");
        
        std::string face = data.value("face", "");
        if (face.rfind("//", 0) == 0) {
            face = "https:" + face;
        } else if (face.rfind("http://", 0) == 0) {
            face = "https://" + face.substr(7);
        }
        if (face.find(".webp") != std::string::npos && face.find("@") == std::string::npos) {
            face += "@120w_120h_1c.png";
        }
        profile.face = face;
        profile.money = data.value("money", 0.0);

        if (data.contains("level_info")) {
            profile.level = data["level_info"].value("current_level", 0);
        }

        profile.vipType = data.value("vipType", 0);
        profile.vipStatus = data.value("vipStatus", 0);

        if (data.contains("vip_label")) {
            profile.vipLabel = data["vip_label"].value("text", "");
        }
        if (profile.vipLabel.empty()) {
            if (profile.vipType == 2 && profile.vipStatus == 1) {
                profile.vipLabel = "年度大会员";
            } else if (profile.vipType == 1 && profile.vipStatus == 1) {
                profile.vipLabel = "大会员";
            } else {
                profile.vipLabel = "普通正式会员";
            }
        }

        return true;
    } catch (const std::exception &e) {
        errorMsg = std::string("解析用户信息异常: ") + e.what();
        return false;
    }
}

// 4. 持久化读取与写入 config.json
bool BiliAuth::loadConfig(const std::string &filePath, AppConfig &config) {
    std::ifstream ifs(filePath);
    if (!ifs.is_open()) return false;

    try {
        json j;
        ifs >> j;
        config.sessdata = j.value("sessdata", "");
        config.bili_jct = j.value("bili_jct", "");
        config.dedeUserId = j.value("dedeUserId", "");
        config.refreshToken = j.value("refreshToken", "");
        config.downloadPath = j.value("downloadPath", "./downloads");
        config.defaultQuality = j.value("defaultQuality", 120);
        config.defaultCodec = j.value("defaultCodec", 12);
        config.themeColor = j.value("themeColor", "#FF1E42");
        config.muxEnabled = j.value("muxEnabled", true);
        config.maxDownloadThreads = j.value("maxDownloadThreads", 4);
        config.downloadSubRes = j.value("downloadSubRes", true);
        config.maxDownloadSpeedKB = j.value("maxDownloadSpeedKB", 0);
        config.completionSoundEnabled = j.value("completionSoundEnabled", true);
        config.completionSoundPath = j.value("completionSoundPath", "");
        return true;
    } catch (...) {
        return false;
    }
}

bool BiliAuth::saveConfig(const std::string &filePath, const AppConfig &config) {
    try {
        json j;
        j["sessdata"] = config.sessdata;
        j["bili_jct"] = config.bili_jct;
        j["dedeUserId"] = config.dedeUserId;
        j["refreshToken"] = config.refreshToken;
        j["downloadPath"] = config.downloadPath;
        j["defaultQuality"] = config.defaultQuality;
        j["defaultCodec"] = config.defaultCodec;
        j["themeColor"] = config.themeColor;
        j["muxEnabled"] = config.muxEnabled;
        j["maxDownloadThreads"] = config.maxDownloadThreads;
        j["downloadSubRes"] = config.downloadSubRes;
        j["maxDownloadSpeedKB"] = config.maxDownloadSpeedKB;
        j["completionSoundEnabled"] = config.completionSoundEnabled;
        j["completionSoundPath"] = config.completionSoundPath;

        std::ofstream ofs(filePath);
        if (!ofs.is_open()) return false;

        ofs << j.dump(4);
        return true;
    } catch (...) {
        return false;
    }
}

// 4. 会话健康检查: 精确区分 网络失败 / 凭证过期 / 服务器异常
AuthError BiliAuth::checkSession(const AppConfig &config, UserProfile &profile, std::string &errorMsg) {
    std::string url = "https://api.bilibili.com/x/web-interface/nav";
    std::string rawJson;

    BiliHttpClient client;
    std::string cookieHeader = "SESSDATA=" + config.sessdata;
    if (!config.bili_jct.empty()) cookieHeader += "; bili_jct=" + config.bili_jct;
    if (!config.dedeUserId.empty()) cookieHeader += "; DedeUserID=" + config.dedeUserId;
    client.setCookie(cookieHeader);

    if (!client.get(url, rawJson, {}, 10)) {
        errorMsg = "网络连接失败";
        return AuthError::NETWORK;
    }

    try {
        auto root = json::parse(rawJson);
        int code = root.value("code", -1);
        if (code != 0) {
            // nav 接口未登录返回 code=-101 (账号未登录) → 凭证过期
            errorMsg = root.value("message", "获取个人资料失败");
            return AuthError::EXPIRED;
        }

        auto data = root["data"];
        profile.isLogin = data.value("isLogin", false);
        if (!profile.isLogin) {
            errorMsg = "未登录或 SESSDATA 已过期";
            return AuthError::EXPIRED;
        }

        profile.mid = data.value("mid", (int64_t)0);
        profile.uname = data.value("uname", "B站用户");

        std::string face = data.value("face", "");
        if (face.rfind("//", 0) == 0) {
            face = "https:" + face;
        } else if (face.rfind("http://", 0) == 0) {
            face = "https://" + face.substr(7);
        }
        if (face.find(".webp") != std::string::npos && face.find("@") == std::string::npos) {
            face += "@120w_120h_1c.png";
        }
        profile.face = face;
        profile.money = data.value("money", 0.0);

        if (data.contains("level_info")) {
            profile.level = data["level_info"].value("current_level", 0);
        }

        profile.vipType = data.value("vipType", 0);
        profile.vipStatus = data.value("vipStatus", 0);

        if (data.contains("vip_label")) {
            profile.vipLabel = data["vip_label"].value("text", "");
        }
        if (profile.vipLabel.empty()) {
            if (profile.vipType == 2 && profile.vipStatus == 1) {
                profile.vipLabel = "年度大会员";
            } else if (profile.vipType == 1 && profile.vipStatus == 1) {
                profile.vipLabel = "大会员";
            } else {
                profile.vipLabel = "正式会员";
            }
        }

        return AuthError::OK;
    } catch (const std::exception &e) {
        errorMsg = e.what();
        return AuthError::SERVER;
    }
}

bool BiliAuth::fetchUserProfile(const AppConfig &config, UserProfile &profile, std::string &errorMsg) {
    return checkSession(config, profile, errorMsg) == AuthError::OK;
}

// 5. refreshToken 自动续期 (对齐成熟项目: 凭证过期时静默换新, 避免用户重复扫码)
//    接口: POST /x/passport-login/web/cookie/refresh
//    入参: Cookie(SESSDATA+bili_jct+DedeUserID) + 表单(csrf + refresh_token)
//    返回: JSON 体内下发新 refresh_token; Set-Cookie 头下发新 SESSDATA/bili_jct/DedeUserID
bool BiliAuth::refreshSession(AppConfig &config, std::string &errorMsg) {
    if (config.refreshToken.empty()) {
        errorMsg = "缺少 refresh_token，无法自动续期，请重新扫码登录";
        return false;
    }

    std::string cookieHeader = "SESSDATA=" + config.sessdata;
    if (!config.bili_jct.empty()) cookieHeader += "; bili_jct=" + config.bili_jct;
    if (!config.dedeUserId.empty()) cookieHeader += "; DedeUserID=" + config.dedeUserId;

    // csrf 与 refresh_token 均需 URL 编码 (refresh_token 内含 % 等特殊字符)
    std::string body = "csrf=" + BiliHttpClient::urlEncode(config.bili_jct)
                     + "&refresh_token=" + BiliHttpClient::urlEncode(config.refreshToken);

    BiliHttpClient client;
    client.setCookie(cookieHeader);
    std::string rawJson;
    std::vector<std::string> setCookies;
    if (!client.post("https://passport.bilibili.com/x/passport-login/web/cookie/refresh",
                     body, rawJson, setCookies, {}, 10)) {
        errorMsg = "网络请求失败";
        return false;
    }

    try {
        auto root = json::parse(rawJson);
        int code = root.value("code", -1);
        if (code != 0) {
            // code=-101 等 → refresh_token 已失效, 凭证彻底过期, 需重新扫码
            errorMsg = root.value("message", "续期失败") + " (code=" + std::to_string(code) + ")";
            return false;
        }

        // 新 refresh_token (接口每次续期都会轮换, 必须落盘覆盖旧值)
        if (root.contains("data") && root["data"].contains("refresh_token")) {
            config.refreshToken = root["data"].value("refresh_token", "");
        }

        // 新 SESSDATA / bili_jct / DedeUserID 通过 Set-Cookie 下发
        std::string newSess = extractCookieValue(setCookies, "SESSDATA");
        std::string newJct = extractCookieValue(setCookies, "bili_jct");
        std::string newDede = extractCookieValue(setCookies, "DedeUserID");
        if (!newSess.empty()) config.sessdata = newSess;
        if (!newJct.empty()) config.bili_jct = newJct;
        if (!newDede.empty()) config.dedeUserId = newDede;

        qDebug() << "[AUTH-REFRESH] refreshSession SUCCESS"
                 << "newSessdata.len=" << config.sessdata.size()
                 << "setCookies.count=" << setCookies.size();
        return true;
    } catch (const std::exception &e) {
        errorMsg = std::string("解析续期响应异常: ") + e.what();
        return false;
    }
}
