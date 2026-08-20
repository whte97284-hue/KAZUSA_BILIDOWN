// 探针: 直接测试 CDN 流下载 (绕过队列, 定位卡点)
#define NOMINMAX
#include "BiliParser.hpp"
#include "BiliDownloader.hpp"
#include "BiliAuth.hpp"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <thread>
#include <chrono>
#include <windows.h>

int main(int argc, char *argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    std::string bv = argc > 1 ? argv[1] : "BV1sqbmzeEzR";

    // 读 config.json 拿登录态
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
    std::cout << "sessdata: " << (sess.empty() ? "(游客)" : "已载入") << "\n";

    BiliParser parser;
    BiliDownloader dl;
    if (!sess.empty()) { parser.setSessData(sess); dl.setSessData(sess); }

    std::string err;
    BiliVideoDetail detail;
    if (!parser.fetchVideoDetail(bv, detail, err)) {
        std::cout << "[FAIL] fetchVideoDetail: " << err << "\n";
        return 1;
    }
    if (!parser.fetchPlayUrl(detail, 0, err)) {
        std::cout << "[FAIL] fetchPlayUrl: " << err << "\n";
        return 1;
    }
    std::cout << "标题: " << detail.title << "\n";
    std::cout << "视频轨数: " << detail.videoTracks.size() << ", 音频轨数: " << detail.audioTracks.size() << "\n";

    // 复现 runDownload 场景: 从 std::thread 中重复 fetchPlayUrl (模拟 worker 重解析)
    std::cout << "\n--- 复现: std::thread 中重复 fetchPlayUrl ---\n";
    std::thread([&]() {
        for (int i = 0; i < 2; ++i) {
            auto t0 = std::chrono::steady_clock::now();
            std::string e2;
            BiliVideoDetail d2 = detail;
            bool ok2 = parser.fetchPlayUrl(d2, 0, e2);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();
            std::cout << "  第" << (i + 1) << "次 thread内 fetchPlayUrl: "
                      << (ok2 ? "OK" : "FAIL: " + e2) << " | 耗时 " << ms << "ms\n";
        }
    }).join();

    // 找 360P AVC (quality=16, codecId=7), 否则用第一个
    const VideoTrack *v = nullptr;
    for (const auto &t : detail.videoTracks) {
        if (t.quality == 16 && t.codecId == 7) { v = &t; break; }
    }
    if (!v) v = &detail.videoTracks[0];
    if (!v) { std::cout << "[FAIL] 无视频轨\n"; return 1; }

    std::cout << "选择轨道: " << v->qualityDesc << " | " << v->codecName
              << " " << v->width << "x" << v->height << "\n";
    std::cout << "主CDN: " << v->baseUrl.substr(0, 120) << "\n";
    std::cout << "备份CDN数: " << v->backupUrls.size() << "\n";
    for (size_t i = 0; i < v->backupUrls.size(); ++i)
        std::cout << "  备份[" << i << "]: " << v->backupUrls[i].substr(0, 120) << "\n";

    // 直接测主 CDN 流下载, 限 2MB
    std::string out = "./probe_stream.m4s.part";
    DeleteFileA(out.c_str());
    std::cout << "\n--- 直接下载主CDN (限2MB) ---\n";
    auto cb = [](int64_t dl, int64_t total, double speed, double pct) {
        std::cout << "\r  进度: " << (dl / 1024) << "KB / " << (total / 1024) << "KB | "
                  << std::fixed << std::setprecision(2) << (speed / 1024 / 1024) << " MB/s" << std::flush;
    };
    bool ok = dl.downloadStream(v->baseUrl, out, cb, 2 * 1024 * 1024, &err, 0, nullptr);
    std::cout << "\n  结果: " << (ok ? "[OK] 下载成功" : "[FAIL] " + err) << "\n";

    // 备份 CDN 逐个快速测 (2MB)
    for (size_t i = 0; i < v->backupUrls.size(); ++i) {
        DeleteFileA(out.c_str());
        std::string berr;
        std::cout << "--- 备份CDN[" << i << "] (限2MB) ---\n";
        bool bok = dl.downloadStream(v->backupUrls[i], out, cb, 2 * 1024 * 1024, &berr, 0, nullptr);
        std::cout << "\n  结果: " << (bok ? "[OK]" : "[FAIL] " + berr) << "\n";
    }

    DeleteFileA(out.c_str());
    return 0;
}
