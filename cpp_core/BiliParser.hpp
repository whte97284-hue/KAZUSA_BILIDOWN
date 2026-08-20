#pragma once

#include "BiliTypes.hpp"
#include "BiliHttpClient.hpp"
#include "BiliSigner.hpp"
#include <string>
#include <vector>
#include <memory>

class BiliParser {
public:
    BiliParser();
    ~BiliParser();

    // 1. 设置登录凭证 (SESSDATA Cookie)
    void setSessData(const std::string &sessData);

    // 2. 智能解析任意形式的输入 (支持 BV号, AV号, 番剧 ep/ss, 完整网页链接, b23.tv 短链)
    bool normalizeInput(const std::string &rawInput, ParsedInput &result, std::string &errorMsg);

    // 3. 解析视频全量元数据 (包含多 P 列表、UP主、简介、封面、字幕、番剧分季)
    bool fetchVideoDetail(const std::string &rawInput, BiliVideoDetail &detail, std::string &errorMsg);

    // 4. 解析指定分 P 的流媒体直链 (DASH 全画质全编码 + Hi-Res/杜比音轨 + 备用 CDN + TV 接口容灾)
    bool fetchPlayUrl(BiliVideoDetail &detail, int pageIndex, std::string &errorMsg, bool forceTvApi = false);

private:
    BiliHttpClient m_client;
    std::string m_imgKey;
    std::string m_subKey;
    int64_t m_keyExpireTime = 0;

    // 解析 UGC 普通视频
    bool parseUgcView(const std::string &bvid, BiliVideoDetail &detail, std::string &errorMsg);

    // 解析 PGC 番剧/影视视频
    bool parsePgcSeason(const std::string &idType, const std::string &idVal, BiliVideoDetail &detail, std::string &errorMsg);

    // Web 端 PlayURL 解析器
    bool fetchWebPlayUrl(const std::string &bvid, int64_t cid, bool isBangumi, BiliVideoDetail &detail, std::string &errorMsg);

    // TV 端 PlayURL 解析器 (BBDown 经典容灾通道)
    bool fetchTvPlayUrl(const std::string &bvid, int64_t cid, bool isBangumi, BiliVideoDetail &detail, std::string &errorMsg);

    // DASH JSON 数据解构
    bool parseDashJson(const std::string &rawJson, BiliVideoDetail &detail, std::string &errorMsg);
};
