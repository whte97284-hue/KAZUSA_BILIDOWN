#include "BiliSigner.hpp"
#include "json.hpp"
#include <windows.h>
#include <wincrypt.h>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <algorithm>

#pragma comment(lib, "advapi32.lib")

using json = nlohmann::json;

// 官方 54 位字符重排混淆表 (BBDown / bilibili-API-collect 规范)
static const int MIXIN_KEY_ENC_TAB[64] = {
    46, 47, 18, 2, 53, 8, 23, 32, 15, 50, 10, 31, 58, 3, 45, 35, 27, 43, 5, 49,
    33, 9, 42, 19, 29, 28, 14, 39, 12, 38, 41, 13, 37, 48, 7, 16, 24, 55, 40,
    61, 26, 17, 0, 1, 60, 51, 30, 4, 22, 25, 54, 21, 56, 59, 6, 63, 57, 62, 11,
    36, 20, 34, 44, 52
};

std::string BiliSigner::md5(const std::string &input) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    BYTE rgbHash[16];
    DWORD cbHash = 16;
    std::string result = "";

    if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        if (CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
            if (CryptHashData(hHash, (const BYTE*)input.c_str(), (DWORD)input.length(), 0)) {
                if (CryptGetHashParam(hHash, HP_HASHVAL, rgbHash, &cbHash, 0)) {
                    std::ostringstream oss;
                    for (DWORD i = 0; i < cbHash; i++) {
                        oss << std::hex << std::setw(2) << std::setfill('0') << (int)rgbHash[i];
                    }
                    result = oss.str();
                }
            }
            CryptDestroyHash(hHash);
        }
        CryptReleaseContext(hProv, 0);
    }
    return result;
}

bool BiliSigner::refreshWbiKeys(BiliHttpClient &client, std::string &imgKey, std::string &subKey, int64_t &expireTime) {
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    if (!imgKey.empty() && now < expireTime) {
        return true;
    }

    std::string rawJson;
    if (!client.get("https://api.bilibili.com/x/web-interface/nav", rawJson)) {
        return false;
    }

    try {
        auto root = json::parse(rawJson);
        auto wbiImg = root["data"]["wbi_img"];
        std::string imgUrl = wbiImg.value("img_url", "");
        std::string subUrl = wbiImg.value("sub_url", "");

        auto getFilename = [](const std::string &url) {
            size_t slash = url.find_last_of('/');
            size_t dot = url.find_last_of('.');
            if (slash != std::string::npos && dot != std::string::npos && dot > slash) {
                return url.substr(slash + 1, dot - slash - 1);
            }
            return std::string("");
        };

        imgKey = getFilename(imgUrl);
        subKey = getFilename(subUrl);
        expireTime = now + 3600; // 缓存 1 小时
        return !imgKey.empty() && !subKey.empty();
    } catch (...) {
        return false;
    }
}

std::string BiliSigner::buildSignedWbiQuery(
    const std::map<std::string, std::string> &params, 
    const std::string &imgKey, 
    const std::string &subKey
) {
    // 依 54 位混淆表提取 32 位 mixin_key
    std::string rawKey = imgKey + subKey;
    std::string mixinKey = "";
    for (int i = 0; i < 32; ++i) {
        int idx = MIXIN_KEY_ENC_TAB[i];
        if (idx < (int)rawKey.length()) {
            mixinKey += rawKey[idx];
        }
    }

    std::map<std::string, std::string> signedParams = params;
    int64_t wts = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    signedParams["wts"] = std::to_string(wts);

    // 升序排序并构建 Query String
    std::ostringstream queryOss;
    bool first = true;
    for (const auto &kv : signedParams) {
        if (!first) queryOss << "&";
        first = false;

        // 过滤 B 站特殊非法字符: !'()*
        std::string cleanVal = kv.second;
        cleanVal.erase(std::remove_if(cleanVal.begin(), cleanVal.end(), [](char c) {
            return c == '!' || c == '\'' || c == '(' || c == ')' || c == '*';
        }), cleanVal.end());

        queryOss << BiliHttpClient::urlEncode(kv.first) << "=" << BiliHttpClient::urlEncode(cleanVal);
    }

    std::string queryStr = queryOss.str();
    std::string toHash = queryStr + mixinKey;
    std::string w_rid = md5(toHash);

    // 拼接最终 URL Query 字符串: queryStr + "&w_rid=" + w_rid
    return queryStr + "&w_rid=" + w_rid;
}

std::string BiliSigner::buildSignedTvQuery(
    const std::map<std::string, std::string> &params,
    const std::string &appKey,
    const std::string &appSec
) {
    std::map<std::string, std::string> signedParams = params;
    signedParams["appkey"] = appKey;
    int64_t ts = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    signedParams["ts"] = std::to_string(ts);

    // 字典序排序
    std::ostringstream queryOss;
    bool first = true;
    for (const auto &kv : signedParams) {
        if (!first) queryOss << "&";
        first = false;
        queryOss << BiliHttpClient::urlEncode(kv.first) << "=" << BiliHttpClient::urlEncode(kv.second);
    }

    std::string toHash = queryOss.str() + appSec;
    std::string sign = md5(toHash);

    return queryOss.str() + "&sign=" + sign;
}
