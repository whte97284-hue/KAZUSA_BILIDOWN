#include "BiliParser.hpp"
#include "json.hpp"
#include <regex>
#include <iostream>
#include <sstream>

using json = nlohmann::json;

static std::string getQualityDescription(int qn) {
    switch (qn) {
        case 127: return "8K 超高清";
        case 126: return "杜比视界 (Dolby Vision)";
        case 125: return "HDR 臻彩";
        case 120: return "4K 超清";
        case 116: return "1080P 60帧";
        case 112: return "1080P+ 高码率";
        case 80:  return "1080P 高清";
        case 74:  return "720P 60帧";
        case 64:  return "720P 高清";
        case 32:  return "480P 清晰";
        case 16:  return "360P 流畅";
        default:  return std::to_string(qn) + "P";
    }
}

BiliParser::BiliParser() {}
BiliParser::~BiliParser() {}

void BiliParser::setSessData(const std::string &sessData) {
    if (m_client.getSessData() != sessData) {
        m_client.setSessData(sessData);
        m_imgKey.clear();
        m_subKey.clear();
        m_keyExpireTime = 0;
    }
}

// 智能输入解析与清洗
bool BiliParser::normalizeInput(const std::string &rawInput, ParsedInput &result, std::string &errorMsg) {
    std::string input = rawInput;
    // 去除前后空白
    input.erase(0, input.find_first_not_of(" \t\r\n"));
    input.erase(input.find_last_not_of(" \t\r\n") + 1);

    if (input.empty()) {
        errorMsg = "输入不能为空";
        return false;
    }

    // 1. 处理 b23.tv 短链跳转
    if (input.find("b23.tv") != std::string::npos) {
        std::string resolved;
        if (m_client.resolveRedirectUrl(input, resolved)) {
            input = resolved;
        }
    }

    // 2. 提取分 P 参数 (?p=2 或 &p=2)
    std::regex pRegex(R"([?&]p=(\d+))");
    std::smatch pMatch;
    if (std::regex_search(input, pMatch, pRegex)) {
        try {
            result.page = std::stoi(pMatch[1].str());
        } catch (...) {
            result.page = 1;
        }
    } else {
        result.page = 1;
    }

    // B1: 关键 ID 识别 — 全量加边界锚定 (ID 两侧不可为字母/数字), 杜绝误判
    // 文本里的 "step123"/"class5" 等子串; 并调整匹配顺序为
    // BV → av → ep → ss → 纯数字, 消除"ep 优先于 bv"的顺序敏感问题。
    // 边界形态: (?:^|[^0-9A-Za-z]) 前缀 + (?:$|[^0-9A-Za-z]) 后缀
    // 分组语义保持与历史一致: BV/ep/ss 捕获完整 token, av 捕获纯数字
    std::regex bvRegex(R"((?:^|[^0-9A-Za-z])(BV1[0-9A-Za-z]{9})(?:$|[^0-9A-Za-z]))");
    std::smatch m;
    if (std::regex_search(input, m, bvRegex)) {
        result.type = InputType::BVID;
        result.idValue = m[1].str();
        return true;
    }

    std::regex avRegex(R"((?:^|[^0-9A-Za-z])av(\d+)(?:$|[^0-9A-Za-z]))", std::regex_constants::icase);
    if (std::regex_search(input, m, avRegex)) {
        result.type = InputType::AVID;
        result.idValue = m[1].str();
        return true;
    }

    std::regex epRegex(R"((?:^|[^0-9A-Za-z])(ep\d+)(?:$|[^0-9A-Za-z]))", std::regex_constants::icase);
    if (std::regex_search(input, m, epRegex)) {
        result.type = InputType::EP_ID;
        result.idValue = m[1].str();
        return true;
    }

    std::regex ssRegex(R"((?:^|[^0-9A-Za-z])(ss\d+)(?:$|[^0-9A-Za-z]))", std::regex_constants::icase);
    if (std::regex_search(input, m, ssRegex)) {
        result.type = InputType::SS_ID;
        result.idValue = m[1].str();
        return true;
    }

    // 纯数字 → AVID (逐字符校验, 避免对非 ASCII 负值调用 isdigit 的未定义行为)
    bool allDigits = std::all_of(input.begin(), input.end(),
                                 [](unsigned char c) { return std::isdigit(c) != 0; });
    if (allDigits && !input.empty()) {
        result.type = InputType::AVID;
        result.idValue = input;
        return true;
    }

    errorMsg = "无法识别的 B 站链接或视频 ID";
    return false;
}

// 获取全量视频详情 (自动路由 UGC 与 PGC)
bool BiliParser::fetchVideoDetail(const std::string &rawInput, BiliVideoDetail &detail, std::string &errorMsg) {
    ParsedInput parsed;
    if (!normalizeInput(rawInput, parsed, errorMsg)) {
        return false;
    }

    detail = BiliVideoDetail();

    if (parsed.type == InputType::EP_ID) {
        return parsePgcSeason("ep_id", parsed.idValue.substr(2), detail, errorMsg);
    } else if (parsed.type == InputType::SS_ID) {
        return parsePgcSeason("season_id", parsed.idValue.substr(2), detail, errorMsg);
    } else if (parsed.type == InputType::BVID) {
        return parseUgcView(parsed.idValue, detail, errorMsg);
    } else if (parsed.type == InputType::AVID) {
        // 通过 aid 请求 view
        std::string url = "https://api.bilibili.com/x/web-interface/view?aid=" + parsed.idValue;
        std::string rawJson;
        if (!m_client.get(url, rawJson)) {
            errorMsg = "网络请求失败";
            return false;
        }
        try {
            auto root = json::parse(rawJson);
            if (root.value("code", -1) != 0) {
                errorMsg = root.value("message", "视频不存在");
                return false;
            }
            std::string bvid = root["data"].value("bvid", "");
            return parseUgcView(bvid, detail, errorMsg);
        } catch (const std::exception &e) {
            errorMsg = std::string("解析失败: ") + e.what();
            return false;
        }
    }

    errorMsg = "不支持的输入类型";
    return false;
}

// 1. 解析普通 UGC 视频 (带多 P 与字幕)
bool BiliParser::parseUgcView(const std::string &bvid, BiliVideoDetail &detail, std::string &errorMsg) {
    std::string url = "https://api.bilibili.com/x/web-interface/view?bvid=" + bvid;
    std::string rawJson;

    if (!m_client.get(url, rawJson)) {
        errorMsg = "网络连接失败";
        return false;
    }

    try {
        auto root = json::parse(rawJson);
        int code = root.value("code", -1);
        if (code != 0) {
            errorMsg = root.value("message", "视频不存在或已被删除");
            return false;
        }

        auto data = root["data"];
        detail.bvid = bvid;
        detail.aid = data.value("aid", (int64_t)0);
        detail.title = data.value("title", "未知标题");
        detail.desc = data.value("desc", "");
        detail.coverUrl = data.value("pic", "");
        detail.ownerName = data["owner"].value("name", "未知UP主");
        detail.ownerMid = data["owner"].value("mid", (int64_t)0);
        detail.pubdate = data.value("pubdate", (int64_t)0);
        detail.isBangumi = false;
        detail.score = 0.0;

        // 提取 UGC 播放量
        if (data.contains("stat") && data["stat"].is_object()) {
            int64_t v = data["stat"].value("view", (int64_t)0);
            if (v >= 100000000) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%.1f亿", v / 100000000.0);
                detail.views = buf;
            } else if (v >= 10000) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%.1f万", v / 10000.0);
                detail.views = buf;
            } else if (v > 0) {
                detail.views = std::to_string(v);
            }
        }

        // 解析多 P 列表
        detail.pages.clear();
        if (data.contains("pages") && data["pages"].is_array()) {
            for (const auto &pItem : data["pages"]) {
                VideoPage page;
                page.page = pItem.value("page", 1);
                page.cid = pItem.value("cid", (int64_t)0);
                page.part = pItem.value("part", "分P");
                page.duration = pItem.value("duration", (int64_t)0);
                page.firstFrame = pItem.value("first_frame", "");
                detail.pages.push_back(page);
            }
        }

        // 提取 UGC 标签信息
        std::string sInfo = detail.pages.size() > 1 ? ("共 " + std::to_string(detail.pages.size()) + " P") : "单 P 视频";
        if (!detail.ownerName.empty()) {
            sInfo += " · " + detail.ownerName;
        }
        detail.seasonInfo = sInfo;

        // 解析字幕列表
        detail.subtitles.clear();
        if (data.contains("subtitle") && data["subtitle"].contains("list") && data["subtitle"]["list"].is_array()) {
            for (const auto &sItem : data["subtitle"]["list"]) {
                SubtitleTrack sub;
                sub.id = sItem.value("id", (int64_t)0);
                sub.lang = sItem.value("lan", "");
                sub.langDoc = sItem.value("lan_doc", "");
                sub.subtitleUrl = sItem.value("subtitle_url", "");
                if (sub.subtitleUrl.rfind("//", 0) == 0) {
                    sub.subtitleUrl = "https:" + sub.subtitleUrl;
                }
                detail.subtitles.push_back(sub);
            }
        }

        return true;
    } catch (const std::exception &e) {
        errorMsg = std::string("JSON 解析失败: ") + e.what();
        return false;
    }
}

// 2. 解析 PGC 番剧 / 影视剧集 (支持分季与分集)
bool BiliParser::parsePgcSeason(const std::string &idType, const std::string &idVal, BiliVideoDetail &detail, std::string &errorMsg) {
    std::string url = "https://api.bilibili.com/pgc/view/web/season?" + idType + "=" + idVal;
    std::string rawJson;

    if (!m_client.get(url, rawJson)) {
        errorMsg = "网络连接失败";
        return false;
    }

    try {
        auto root = json::parse(rawJson);
        int code = root.value("code", -1);
        if (code != 0) {
            errorMsg = root.value("message", "番剧信息获取失败");
            return false;
        }

        auto result = root["result"];
        detail.isBangumi = true;
        detail.seasonTitle = result.value("season_title", "");
        detail.title = result.value("title", detail.seasonTitle);
        detail.desc = result.value("evaluate", "");
        detail.coverUrl = result.value("cover", "");
        detail.ownerName = "哔哩哔哩番剧";
        detail.pages.clear();

        if (result.contains("episodes") && result["episodes"].is_array()) {
            int pIndex = 1;
            for (const auto &ep : result["episodes"]) {
                VideoPage page;
                page.page = pIndex++;
                page.cid = ep.value("cid", (int64_t)0);
                std::string title = ep.value("title", "");
                std::string longTitle = ep.value("long_title", "");
                page.part = title.empty() ? longTitle : (title + " " + longTitle);
                page.duration = ep.value("duration", (int64_t)0) / 1000;
                page.firstFrame = ep.value("cover", "");
                detail.pages.push_back(page);

                if (detail.bvid.empty()) {
                    detail.bvid = ep.value("bvid", "");
                    detail.aid = ep.value("aid", (int64_t)0);
                }
            }
        }

        // 提取评分 (如 9.2)
        if (result.contains("rating") && result["rating"].is_object()) {
            detail.score = result["rating"].value("score", 0.0);
        } else {
            detail.score = 0.0;
        }

        // 提取番剧播放量
        if (result.contains("stat") && result["stat"].is_object()) {
            int64_t v = result["stat"].value("views", (int64_t)0);
            if (v >= 100000000) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%.1f亿", v / 100000000.0);
                detail.views = buf;
            } else if (v >= 10000) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%.1f万", v / 10000.0);
                detail.views = buf;
            } else if (v > 0) {
                detail.views = std::to_string(v);
            }
        }

        // 提取番剧标签组合 (地区 · 年份)
        std::vector<std::string> tags;
        if (result.contains("areas") && result["areas"].is_array() && !result["areas"].empty()) {
            tags.push_back(result["areas"][0].value("name", ""));
        }
        if (result.contains("publish") && result["publish"].is_object()) {
            std::string pubTime = result["publish"].value("pub_time", "");
            if (pubTime.length() >= 4) {
                tags.push_back(pubTime.substr(0, 4));
            }
        }
        std::string sInfo;
        for (const auto &t : tags) {
            if (!t.empty()) {
                if (!sInfo.empty()) sInfo += " · ";
                sInfo += t;
            }
        }
        detail.seasonInfo = sInfo;

        return true;
    } catch (const std::exception &e) {
        errorMsg = std::string("番剧 JSON 解析失败: ") + e.what();
        return false;
    }
}

// 3. 解析播放流 (主通道 Web Wbi + 自动 TV 容灾兜底)
bool BiliParser::fetchPlayUrl(BiliVideoDetail &detail, int pageIndex, std::string &errorMsg, bool forceTvApi) {
    if (detail.pages.empty()) {
        errorMsg = "无可用的视频分P";
        return false;
    }

    if (pageIndex < 0 || pageIndex >= (int)detail.pages.size()) {
        pageIndex = 0;
    }

    detail.selectedPageIndex = pageIndex;
    int64_t targetCid = detail.pages[pageIndex].cid;
    std::string bvid = detail.bvid;

    if (forceTvApi) {
        return fetchTvPlayUrl(bvid, targetCid, detail.isBangumi, detail, errorMsg);
    }

    // 优先尝试 Web Wbi 接口 (最高规格 8K/4K/AV1)
    if (fetchWebPlayUrl(bvid, targetCid, detail.isBangumi, detail, errorMsg)) {
        return true;
    }

    // 若 Web 端失败 (如风控或无权)，自动无缝降级到 TV 端接口 (BBDown 经典兜底设计)
    std::cout << "[提示] Web 接口解析受限，正在启用 TV 容灾通道...\n";
    std::string tvError;
    if (fetchTvPlayUrl(bvid, targetCid, detail.isBangumi, detail, tvError)) {
        return true;
    }

    return false;
}

// Web 端 PlayURL
bool BiliParser::fetchWebPlayUrl(const std::string &bvid, int64_t cid, bool isBangumi, BiliVideoDetail &detail, std::string &errorMsg) {
    if (!BiliSigner::refreshWbiKeys(m_client, m_imgKey, m_subKey, m_keyExpireTime)) {
        errorMsg = "Wbi 签名密钥初始化失败";
        return false;
    }

    std::map<std::string, std::string> params;
    params["bvid"] = bvid;
    params["cid"] = std::to_string(cid);
    params["qn"] = "127";
    params["fnval"] = "4048"; // DASH 全开 (AV1/HEVC/AVC, Hi-Res, 杜比)
    params["fnver"] = "0";
    params["fourk"] = "1";

    std::string signedQuery = BiliSigner::buildSignedWbiQuery(params, m_imgKey, m_subKey);

    std::string baseUrl = isBangumi 
        ? "https://api.bilibili.com/pgc/player/web/v2/playurl?"
        : "https://api.bilibili.com/x/player/wbi/playurl?";

    std::string url = baseUrl + signedQuery;
    std::cout << "[调试 URL]: " << url << "\n";
    std::string rawJson;

    if (!m_client.get(url, rawJson)) {
        errorMsg = "Web PlayUrl 网络请求失败";
        return false;
    }

    bool parseRes = parseDashJson(rawJson, detail, errorMsg);
    if (!parseRes) {
        std::cout << "[调试] Web PlayURL 返回: " << rawJson << "\n";

        // 兜底: 极少数老视频/受限接口在 fnval=4048 下不返回任何流 (无 dash 也无 durl),
        // 降级 fnval=1 经典模式二次请求拿单流 durl 直链 (对齐 BBDown 的二次回退策略)。
        std::cout << "[提示] DASH 流缺失，降级 fnval=1 二次请求经典 durl...\n";
        params["fnval"] = "1";
        params["qn"] = "64";
        std::string retryQuery = BiliSigner::buildSignedWbiQuery(params, m_imgKey, m_subKey);
        std::string retryUrl = baseUrl + retryQuery;
        std::string retryJson;
        if (m_client.get(retryUrl, retryJson) && parseDashJson(retryJson, detail, errorMsg)) {
            return true;
        }
    }
    return parseRes;
}

// TV 端 PlayURL (BBDown 经典容灾通道)
bool BiliParser::fetchTvPlayUrl(const std::string &bvid, int64_t cid, bool isBangumi, BiliVideoDetail &detail, std::string &errorMsg) {
    std::map<std::string, std::string> params;
    params["bvid"] = bvid;
    params["cid"] = std::to_string(cid);
    params["qn"] = "120";
    params["fnval"] = "4048";
    params["fnver"] = "0";
    params["fourk"] = "1";
    params["build"] = "105600";
    params["device"] = "android_tv";
    params["platform"] = "android";

    std::string signedQuery = BiliSigner::buildSignedTvQuery(params);

    std::string url = "https://api.snm0516.aisee.tv/x/tv/ugc/playurl?" + signedQuery;
    std::string rawJson;

    if (!m_client.get(url, rawJson)) {
        errorMsg = "TV PlayUrl 网络请求失败";
        return false;
    }

    return parseDashJson(rawJson, detail, errorMsg);
}

// 解析 DASH JSON 树
bool BiliParser::parseDashJson(const std::string &rawJson, BiliVideoDetail &detail, std::string &errorMsg) {
    try {
        auto root = json::parse(rawJson);
        int code = root.value("code", -1);
        if (code != 0) {
            errorMsg = root.value("message", "获取播放流失败");
            return false;
        }

        json dashNode;
        json durlNode;
        int durlQuality = 64;
        bool isPreview = false;  // 试看流标识: 非大会员请求 VIP 专享剧集时返回 6 分钟试看

        if (root.contains("data") && root["data"].contains("dash")) {
            dashNode = root["data"]["dash"];
        } else if (root.contains("result") && root["result"].contains("dash")) {
            dashNode = root["result"]["dash"];
        } else if (root.contains("result") && root["result"].contains("video_info") && root["result"]["video_info"].contains("dash")) {
            // PGC v2 规范结构: result.video_info.dash
            dashNode = root["result"]["video_info"]["dash"];
        } else if (root.contains("data") && root["data"].contains("durl")) {
            durlNode = root["data"]["durl"];
            durlQuality = root["data"].value("quality", 64);
        } else if (root.contains("result") && root["result"].contains("durl")) {
            // PGC v1 规范结构: result.durl (当前档位单流 MP4)
            durlNode = root["result"]["durl"];
            durlQuality = root["result"].value("quality", 64);
        } else if (root.contains("result") && root["result"].contains("video_info") && root["result"]["video_info"].contains("durl")) {
            // PGC v2 规范结构: result.video_info.durl (当前档位单流 MP4)
            durlNode = root["result"]["video_info"]["durl"];
            durlQuality = root["result"]["video_info"].value("quality", 64);
        } else if (root.contains("result") && root["result"].contains("video_info") && root["result"]["video_info"].contains("durls")) {
            // PGC v2 规范结构: result.video_info.durls (按画质分组 [ {durl:[...], quality:N}, ... ])
            durlNode = root["result"]["video_info"]["durls"];
            durlQuality = root["result"]["video_info"].value("quality", 64);
        }

        // 会员门禁检测: 非大会员请求 VIP 专享剧集 → error_code=-10403 + is_preview=1 (仅 6 分钟试看)
        if (root.contains("result")) {
            int vipErr = root["result"].value("error_code", 0);
            if (root["result"].contains("video_info")) {
                vipErr = root["result"]["video_info"].value("error_code", vipErr);
                if (root["result"]["video_info"].value("is_preview", 0) == 1) isPreview = true;
            }
            if (vipErr == -10403) isPreview = true;
        }

        detail.videoTracks.clear();
        detail.audioTracks.clear();
        detail.singleStreamMp4 = false;
        detail.isPreview = isPreview;

        // 1. 经典 durl / FLV / 单流 MP4 回退 (兼容平铺数组与按画质分组两种结构)
        if (dashNode.is_null() && !durlNode.is_null()) {
            bool anyTrack = false;
            if (durlNode.is_array() && !durlNode.empty()) {
                // 分组结构: [ { "durl": [...], "quality": 80 }, ... ] (PGC durls 专有)
                if (durlNode[0].is_object() && durlNode[0].contains("durl") && durlNode[0]["durl"].is_array()) {
                    for (const auto &grp : durlNode) {
                        int q = grp.value("quality", durlQuality);
                        const json &flat = grp["durl"];
                        if (!flat.is_array() || flat.empty()) continue;
                        VideoTrack v;
                        v.quality = q;
                        v.qualityDesc = getQualityDescription(q);
                        v.codecId = 7;
                        v.codecName = "AVC (MP4/FLV)";
                        v.baseUrl = flat[0].value("url", "");
                        v.bandwidth = flat[0].value("size", 0);
                        if (flat[0].contains("backup_url") && flat[0]["backup_url"].is_array()) {
                            for (const auto &bUrl : flat[0]["backup_url"]) {
                                v.backupUrls.push_back(bUrl.get<std::string>());
                            }
                        }
                        detail.videoTracks.push_back(v);
                        anyTrack = true;
                    }
                } else {
                    // 平铺数组结构: [ { "url": "...", "size": N, "backup_url": [...] } ]
                    VideoTrack v;
                    v.quality = durlQuality;
                    v.qualityDesc = getQualityDescription(durlQuality);
                    v.codecId = 7;
                    v.codecName = "AVC (MP4/FLV)";
                    v.baseUrl = durlNode[0].value("url", "");
                    v.bandwidth = durlNode[0].value("size", 0);
                    if (durlNode[0].contains("backup_url") && durlNode[0]["backup_url"].is_array()) {
                        for (const auto &bUrl : durlNode[0]["backup_url"]) {
                            v.backupUrls.push_back(bUrl.get<std::string>());
                        }
                    }
                    detail.videoTracks.push_back(v);
                    anyTrack = true;
                }
            }

            if (anyTrack) {
                // 单流 MP4: 音视频内嵌同一文件, 无独立音频轨, 无需混流
                detail.singleStreamMp4 = true;
                if (isPreview) {
                    std::cout << "[提示] 该剧集为大会员专享, 当前账号非大会员, 仅返回 6 分钟试看流 (error_code=-10403)\n";
                }
                return true;
            }
            errorMsg = "durl 流数据为空";
            return false;
        }

        if (dashNode.is_null()) {
            errorMsg = "返回数据中不含可播放音视频流 (可能需要大会员或未收录)";
            return false;
        }

        detail.videoTracks.clear();
        detail.audioTracks.clear();

        // 1. 视频轨
        if (dashNode.contains("video") && dashNode["video"].is_array()) {
            for (const auto &vItem : dashNode["video"]) {
                VideoTrack v;
                v.quality = vItem.value("id", 0);
                v.qualityDesc = getQualityDescription(v.quality);
                v.codecId = vItem.value("codecid", 0);
                v.width = vItem.value("width", 0);
                v.height = vItem.value("height", 0);

                std::string fpsStr = "30";
                if (vItem.contains("frameRate")) {
                    if (vItem["frameRate"].is_string()) {
                        fpsStr = vItem["frameRate"].get<std::string>();
                    } else if (vItem["frameRate"].is_number()) {
                        fpsStr = std::to_string(vItem["frameRate"].get<int>());
                    }
                }
                try { v.fps = std::stoi(fpsStr); } catch (...) { v.fps = 30; }

                v.bandwidth = vItem.value("bandwidth", 0);
                v.baseUrl = vItem.value("baseUrl", "");

                // 提取备用 CDN 地址 (多 CDN 自动容灾)
                if (vItem.contains("backupUrl") && vItem["backupUrl"].is_array()) {
                    for (const auto &bUrl : vItem["backupUrl"]) {
                        v.backupUrls.push_back(bUrl.get<std::string>());
                    }
                }

                if (v.codecId == 13) v.codecName = "AV1";
                else if (v.codecId == 12) v.codecName = "HEVC";
                else v.codecName = "AVC";

                detail.videoTracks.push_back(v);
            }
        }

        // 2. 音频轨
        if (dashNode.contains("audio") && dashNode["audio"].is_array()) {
            for (const auto &aItem : dashNode["audio"]) {
                AudioTrack a;
                a.id = aItem.value("id", 0);
                a.bandwidth = aItem.value("bandwidth", 0);
                a.baseUrl = aItem.value("baseUrl", "");

                if (aItem.contains("backupUrl") && aItem["backupUrl"].is_array()) {
                    for (const auto &bUrl : aItem["backupUrl"]) {
                        a.backupUrls.push_back(bUrl.get<std::string>());
                    }
                }

                if (a.id == 30280) a.qualityName = "192K 高码率";
                else if (a.id == 30232) a.qualityName = "132K 标准";
                else if (a.id == 30216) a.qualityName = "64K 压缩";
                else a.qualityName = "普通音频";

                detail.audioTracks.push_back(a);
            }
        }

        // 3. Hi-Res 无损音频
        if (dashNode.contains("flac") && !dashNode["flac"].is_null()) {
            auto flacAudio = dashNode["flac"]["audio"];
            if (!flacAudio.is_null()) {
                AudioTrack a;
                a.id = 30255;
                a.qualityName = "Hi-Res 无损音质";
                a.baseUrl = flacAudio.value("baseUrl", "");
                detail.audioTracks.insert(detail.audioTracks.begin(), a);
            }
        }

        // 4. 杜比全景声 (Dolby Atmos)
        if (dashNode.contains("dolby") && !dashNode["dolby"].is_null()) {
            auto dolbyAudio = dashNode["dolby"]["audio"];
            if (dolbyAudio.is_array() && !dolbyAudio.empty()) {
                AudioTrack a;
                a.id = 30250;
                a.qualityName = "杜比全景声 (Dolby Atmos)";
                a.baseUrl = dolbyAudio[0].value("baseUrl", "");
                detail.audioTracks.insert(detail.audioTracks.begin(), a);
            }
        }

        if (isPreview) {
            std::cout << "[提示] 该剧集为大会员专享, 当前账号非大会员, 仅返回 6 分钟试看流 (error_code=-10403)\n";
        }
        return true;
    } catch (const std::exception &e) {
        errorMsg = std::string("DASH 数据解析异常: ") + e.what();
        return false;
    }
}
