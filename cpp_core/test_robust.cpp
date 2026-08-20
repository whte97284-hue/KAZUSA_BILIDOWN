// ============================================================================
//  内核健壮性修复专项验证 (RobustTest)
//  覆盖 2026-08-21 高优先级健壮性审查的三项修复, 纯本地运行不依赖网络:
//    A1: 206 续传 Content-Range 起始字节校验        (BiliDownloader.cpp parseContentRange)
//    A2: 无 Content-Length 时媒体容器盒链完整性校验 (BiliDownloader.cpp isValidMediaContainer)
//    B1: normalizeInput URL 正则边界锚定 + 匹配顺序  (BiliParser.cpp normalizeInput)
//  用法: RobustTest
// ============================================================================
#define NOMINMAX
#include "BiliTypes.hpp"
#include "BiliParser.hpp"
#include <windows.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdio>
#include <algorithm>

// BiliDownloader.cpp 内部健壮性辅助函数 (外部链接, 供本测试直接调用)
bool parseContentRange(const wchar_t *crBuf, int64_t &start, int64_t &end, int64_t &total);
bool isValidMediaContainer(const std::string &filePath, std::string &error);

static int g_pass = 0;
static int g_fail = 0;

static void check(bool ok, const std::string &name) {
    if (ok) {
        ++g_pass;
        std::cout << "  [PASS] " << name << "\n";
    } else {
        ++g_fail;
        std::cout << "  [FAIL] " << name << "\n";
    }
}

// ===================== A1: Content-Range 解析 =====================
static void testA1() {
    std::cout << "\n--- A1: parseContentRange (206 续传起始字节校验) ---\n";
    int64_t s, e, t;

    check(parseContentRange(L"bytes 0-499/12345", s, e, t) && s == 0 && e == 499 && t == 12345,
          "标准 206: bytes 0-499/12345");
    check(parseContentRange(L"bytes 500-999/12345", s, e, t) && s == 500 && t == 12345,
          "续传中段: bytes 500-999/12345");
    check(parseContentRange(L"bytes 100-199/10000", s, e, t) && s == 100,
          "起始字节 100 正确提取");
    // CDN 偶发省略 "bytes " 前缀 (RFC 要求必须有, 但部分实现不规范)
    check(parseContentRange(L"0-99/5000", s, e, t) && s == 0 && t == 5000,
          "省略 bytes 前缀: 0-99/5000");
    // total 未知 (Total-Unknown 形态)
    check(parseContentRange(L"bytes 0-99/*", s, e, t) && s == 0 && t == -1,
          "未知总大小: bytes 0-99/*");
    // 416 专属格式 (bytes */total, 无 start-end) 必须拒绝
    check(!parseContentRange(L"bytes */12345", s, e, t),
          "416 格式 bytes */12345 拒绝");
    // 垃圾/畸形输入必须拒绝
    check(!parseContentRange(L"garbage", s, e, t), "纯垃圾输入拒绝");
    check(!parseContentRange(L"", s, e, t), "空输入拒绝");
    check(!parseContentRange(L"bytes -100/2000", s, e, t), "负起始字节拒绝");
    check(!parseContentRange(L"bytes abc-100/2000", s, e, t), "非数字起始拒绝");
    check(parseContentRange(L"bytes 2000-2999/999999999999", s, e, t) && s == 2000 && t == 999999999999LL,
          "超大 total (12 位) 解析无溢出");
}

// ===================== A2: 媒体容器盒链完整性 =====================
struct BoxSpec { const char *type; uint32_t payload; };

static void writeBoxes(const std::string &path, const std::vector<BoxSpec> &boxes, int64_t limit = -1) {
    std::vector<char> data;
    for (const auto &b : boxes) {
        uint32_t size = 8 + b.payload;
        char hdr[8] = {
            char((size >> 24) & 0xFF), char((size >> 16) & 0xFF),
            char((size >> 8) & 0xFF),  char(size & 0xFF),
            b.type[0], b.type[1], b.type[2], b.type[3]
        };
        data.insert(data.end(), hdr, hdr + 8);
        data.insert(data.end(), b.payload, '\0');
    }
    if (limit >= 0 && limit < (int64_t)data.size()) data.resize((size_t)limit);
    std::ofstream f(path, std::ios::binary);
    f.write(data.data(), (std::streamsize)data.size());
    f.close();
}

static void testA2() {
    std::cout << "\n--- A2: isValidMediaContainer (未知大小截断检测) ---\n";
    const std::string base = "_robust_a2_";
    std::string err;

    // 1. 合法标准 MP4 顶层盒链: ftyp + moov + mdat
    std::string valid = base + "valid.mp4";
    writeBoxes(valid, { {"ftyp", 16}, {"moov", 48}, {"mdat", 128} });
    check(isValidMediaContainer(valid, err), "合法 MP4 (ftyp+moov+mdat) 通过");

    // 2. 合法 fMP4 片段: styp + moof + mdat (DASH 分片形态)
    std::string fmp4 = base + "fmp4.m4s";
    writeBoxes(fmp4, { {"styp", 16}, {"moof", 64}, {"mdat", 256} });
    check(isValidMediaContainer(fmp4, err), "合法 fMP4 (styp+moof+mdat) 通过");

    // 3. 截断: 合法文件在中途被切断 (mdat 盒体不完整) → 必须拦截
    std::string trunc = base + "trunc.mp4";
    writeBoxes(trunc, { {"ftyp", 16}, {"moov", 48}, {"mdat", 128} }, 100);
    check(!isValidMediaContainer(trunc, err), "mdat 中途截断 → 拒绝");

    // 4. 仅头部无媒体数据盒 (只拿到 ftyp, 无 moov/moof/mdat) → 必须拦截
    std::string head = base + "head.mp4";
    writeBoxes(head, { {"ftyp", 16} });
    check(!isValidMediaContainer(head, err), "仅 ftyp 无数据盒 → 拒绝");

    // 5. 纯文本垃圾 (反爬拦截页) → 必须拦截
    std::string garbage = base + "garbage.txt";
    {
        std::ofstream f(garbage, std::ios::binary);
        f.write("This is definitely not an MP4 file!!!", 38);
    }
    check(!isValidMediaContainer(garbage, err), "纯文本垃圾 → 拒绝");

    // 6. 空文件 / 不存在文件 → 必须拦截
    std::string empty = base + "empty.mp4";
    writeBoxes(empty, {});
    check(!isValidMediaContainer(empty, err), "空文件 → 拒绝");
    check(!isValidMediaContainer(base + "nope.mp4", err), "不存在文件 → 拒绝");

    // 清理
    std::remove(valid.c_str());
    std::remove(fmp4.c_str());
    std::remove(trunc.c_str());
    std::remove(head.c_str());
    std::remove(garbage.c_str());
    std::remove(empty.c_str());
}

// ===================== B1: normalizeInput 边界锚定 =====================
static void testB1() {
    std::cout << "\n--- B1: normalizeInput (URL 正则锚定 + 匹配顺序) ---\n";
    BiliParser parser;
    ParsedInput r;
    std::string e;

    auto expect = [&](const std::string &input, bool wantOk, InputType wantType,
                      const std::string &wantId, int wantPage, const std::string &name) {
        r = ParsedInput();
        e.clear();
        bool ok = parser.normalizeInput(input, r, e);
        bool good = (ok == wantOk);
        if (good && ok) {
            good = (r.type == wantType);
            if (wantType != InputType::AVID) good = good && (r.idValue == wantId);
            else good = good && (r.idValue == wantId); // av 也是纯数字串
            if (wantPage >= 0) good = good && (r.page == wantPage);
        }
        check(good, name + "  →  " + input);
        if (!good) {
            std::cout << "          got ok=" << ok << " type=" << (int)r.type
                      << " id='" << r.idValue << "' page=" << r.page
                      << " err='" << e << "'\n";
        }
    };

    // 正常路径 (回归保护)
    expect("BV1xx411c7xx", true, InputType::BVID, "BV1xx411c7xx", 1, "纯 BV 号");
    expect("https://www.bilibili.com/video/BV1xx411c7xx", true, InputType::BVID, "BV1xx411c7xx", 1, "UGC 完整链接");
    expect("https://www.bilibili.com/video/BV1xx411c7xx?p=2", true, InputType::BVID, "BV1xx411c7xx", 2, "UGC 链接带 p=2");
    expect("av170001", true, InputType::AVID, "170001", 1, "av 号");
    expect("AV170001", true, InputType::AVID, "170001", 1, "AV 大写");
    expect("170001", true, InputType::AVID, "170001", 1, "纯数字 aid");
    expect("https://www.bilibili.com/bangumi/play/ss1702", true, InputType::SS_ID, "ss1702", 1, "番剧 ss 链接");
    expect("https://www.bilibili.com/bangumi/play/ep123456", true, InputType::EP_ID, "ep123456", 1, "番剧 ep 链接");
    expect("ss1702", true, InputType::SS_ID, "ss1702", 1, "纯 ss 号");
    expect("ep123456", true, InputType::EP_ID, "ep123456", 1, "纯 ep 号");
    expect(" 大家看看这个视频 BV1xx411c7xx 支持一下", true, InputType::BVID, "BV1xx411c7xx", 1, "文本中内嵌 BV");

    // B1 修复点: 边界锚定 — 嵌入单词内的 ep/ss 子串不得误判
    expect("step123", false, InputType::BVID, "", 1, "step123 内嵌 ep123 拒绝");
    expect("class5", false, InputType::BVID, "", 1, "class5 内嵌 ss5 拒绝");
    expect("ep12345abc", false, InputType::BVID, "", 1, "ep 后紧跟字母拒绝");

    // B1 修复点: 匹配顺序 — BV 优先于 query 里的 ss/ep
    expect("https://www.bilibili.com/video/BV1xx411c7xx?from=ss123", true, InputType::BVID, "BV1xx411c7xx", 1, "BV 优先于 query 中的 ss");
    expect("https://www.bilibili.com/video/BV1xx411c7xx?spm_id_from=333.ep999", true, InputType::BVID, "BV1xx411c7xx", 1, "BV 优先于 query 中的 ep");
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    setvbuf(stdout, nullptr, _IONBF, 0);

    std::cout << "================================================================================\n";
    std::cout << "  内核健壮性修复专项验证 (A1/A2/B1)\n";
    std::cout << "================================================================================\n";

    testA1();
    testA2();
    testB1();

    std::cout << "\n================================================================================\n";
    std::cout << "  结果: " << g_pass << " 通过 / " << g_fail << " 失败\n";
    std::cout << "================================================================================\n";
    return g_fail == 0 ? 0 : 1;
}
