// 番剧多P解析完备性验证:
//  1. parsePgcSeason 是否正确填充多 P (episodes → pages)
//  2. fetchPlayUrl 对第 0 / 第 N P 是否都能拿到流 (bvid 为第 1 P 时)
//  3. 各 P 的单流 MP4 / DASH 标识是否正确
#define NOMINMAX
#include "BiliParser.hpp"
#include "BiliAuth.hpp"
#include <iostream>
#include <fstream>
#include <windows.h>

int main(int argc, char *argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    setvbuf(stdout, nullptr, _IONBF, 0);

    std::string input = argc > 1 ? argv[1] : "ss1702";
    int maxP = argc > 2 ? std::atoi(argv[2]) : 3;

    // 读登录态
    std::string sess;
    {
        std::ifstream f("./config.json");
        if (f) {
            std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            auto pos = all.find("\"sessdata\"");
            if (pos != std::string::npos) {
                auto s = all.find('"', pos + 11);
                auto e = all.find('"', s + 1);
                sess = all.substr(s + 1, e - s - 1);
            }
        }
    }

    BiliParser parser;
    if (!sess.empty()) parser.setSessData(sess);

    std::string err;
    BiliVideoDetail detail;
    if (!parser.fetchVideoDetail(input, detail, err)) {
        std::cout << "[FAIL] fetchVideoDetail: " << err << "\n";
        return 1;
    }
    std::cout << "标题: " << detail.title << "\n";
    std::cout << "isBangumi: " << detail.isBangumi << "\n";
    std::cout << "bvid(第1P): " << detail.bvid << "\n";
    std::cout << "分P总数: " << detail.pages.size() << "\n";
    for (size_t i = 0; i < detail.pages.size(); ++i) {
        const auto &pg = detail.pages[i];
        std::cout << "  [" << i << "] P" << pg.page << " cid=" << pg.cid
                  << " dur=" << pg.duration << "s part='" << pg.part.substr(0, 40) << "'\n";
    }

    std::cout << "\n--- 逐 P 取流验证 ---\n";
    int okCount = 0;
    for (int i = 0; i < static_cast<int>(detail.pages.size()) && i < maxP; ++i) {
        BiliVideoDetail d2 = detail;
        std::string e2;
        bool ok = parser.fetchPlayUrl(d2, i, e2);
        std::cout << "  P[" << i << "] fetchPlayUrl: " << (ok ? "OK" : "FAIL: " + e2)
                  << " | videoTracks=" << d2.videoTracks.size()
                  << " audioTracks=" << d2.audioTracks.size()
                  << " singleStreamMp4=" << d2.singleStreamMp4 << "\n";
        if (ok) ++okCount;
    }
    std::cout << "\n取流成功 P 数: " << okCount << " / " << std::min<int>(maxP, (int)detail.pages.size()) << "\n";
    return okCount > 0 ? 0 : 1;
}
