#pragma once

#include <string>
#include <map>
#include <cstdint>
#include "BiliHttpClient.hpp"

class BiliSigner {
public:
    // 计算通用 MD5 (十六进制小写)
    static std::string md5(const std::string &input);

    // ================= 1. Web 端 Wbi 签名 =================
    // 获取/刷新 Wbi Key
    static bool refreshWbiKeys(BiliHttpClient &client, std::string &imgKey, std::string &subKey, int64_t &expireTime);

    // 对 Query 参数进行 Wbi 签名，直接返回已升序排好且带 wts 与 w_rid 的完整 URL Query 字符串
    static std::string buildSignedWbiQuery(
        const std::map<std::string, std::string> &params, 
        const std::string &imgKey, 
        const std::string &subKey
    );

    // ================= 2. TV / APP 接口签名 (BBDown 经典兜底通道) =================
    // 对 TV 端请求进行 MD5 签名，直接返回已签名的 Query 字符串
    static std::string buildSignedTvQuery(
        const std::map<std::string, std::string> &params,
        const std::string &appKey = "4409e2cf0d1439b2",
        const std::string &appSec = "59b43e04ad6965f34319062b478f83dd"
    );
};
