#include "Bento4Muxer.hpp"
#include <iostream>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <vector>

namespace fs = std::filesystem;

struct SamplePair {
    std::string name;
    std::string videoM4s;
    std::string audioM4s;
};

int main() {
    std::cout << "======================================================" << std::endl;
    std::cout << "      Bento4 无损混流引擎 (Bento4Muxer) 全套验证测试    " << std::endl;
    std::cout << "======================================================" << std::endl;

    // 寻找真实 B 站下载样本
    std::vector<SamplePair> samplePairs;
    std::string testDir = "./test_chain_out";

    if (fs::exists(testDir)) {
        for (const auto &entry : fs::directory_iterator(testDir)) {
            if (entry.is_directory()) {
                std::string v = (entry.path() / "video.m4s").u8string();
                std::string a = (entry.path() / "audio.m4s").u8string();
                if (fs::exists(fs::u8path(v)) && fs::exists(fs::u8path(a))) {
                    SamplePair sp;
                    sp.name = entry.path().filename().u8string();
                    sp.videoM4s = v;
                    sp.audioM4s = a;
                    samplePairs.push_back(sp);
                }
            }
        }
    }

    if (samplePairs.empty()) {
        std::cerr << "[ERROR] 未找到任何现成的测试用例 m4s 文件！" << std::endl;
        return 1;
    }

    std::cout << "[INFO] 发现 " << samplePairs.size() << " 组真实 B 站 DASH 流测试样本:\n" << std::endl;
    for (size_t i = 0; i < samplePairs.size(); ++i) {
        auto vSize = fs::file_size(fs::u8path(samplePairs[i].videoM4s));
        auto aSize = fs::file_size(fs::u8path(samplePairs[i].audioM4s));
        std::cout << "  [" << (i + 1) << "] " << samplePairs[i].name << "\n"
                  << "      视频: " << std::fixed << std::setprecision(2) << (vSize / (1024.0 * 1024.0)) << " MB\n"
                  << "      音频: " << std::fixed << std::setprecision(2) << (aSize / (1024.0 * 1024.0)) << " MB" << std::endl;
    }

    int testIndex = 1;
    std::string error;

    // ----------------------------------------------------
    // 测试 1 & 2: 真实样本无损混流与性能测试
    // ----------------------------------------------------
    for (const auto &sp : samplePairs) {
        std::cout << "\n------------------------------------------------------" << std::endl;
        std::cout << "[TEST " << testIndex++ << "] 真实样本合成: " << sp.name << std::endl;
        std::cout << "------------------------------------------------------" << std::endl;

        std::string outMp4 = "./test_mux_out/output_sample_" + std::to_string(testIndex - 1) + ".mp4";
        auto t1 = std::chrono::high_resolution_clock::now();
        bool ok = Bento4Muxer::muxToMp4(sp.videoM4s, sp.audioM4s, outMp4, error);
        auto t2 = std::chrono::high_resolution_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();

        if (!ok) {
            std::cerr << "[FAIL] 合成失败: " << error << std::endl;
            return 1;
        }

        auto vSize = fs::file_size(fs::u8path(sp.videoM4s));
        auto aSize = fs::file_size(fs::u8path(sp.audioM4s));
        auto outSize = fs::file_size(fs::u8path(outMp4));
        double inTotal = (double)(vSize + aSize);
        double diffRatio = std::abs((double)outSize - inTotal) / inTotal * 100.0;

        std::cout << "[PASS] 合成成功! 耗时: " << ms << " ms (" << std::fixed << std::setprecision(3) << (ms / 1000.0) << " 秒)" << std::endl;
        std::cout << "  输入总大小: " << std::setprecision(2) << (inTotal / (1024.0 * 1024.0)) << " MB" << std::endl;
        std::cout << "  产物 MP4:   " << std::setprecision(2) << (outSize / (1024.0 * 1024.0)) << " MB" << std::endl;
        std::cout << "  封装开销:   " << std::setprecision(3) << diffRatio << " % (无损Remux标准要求 < 0.5%)" << std::endl;

        // 产物魔数与合法性验证
        if (!Bento4Muxer::validateMp4File(outMp4, error)) {
            std::cerr << "[FAIL] 产物 MP4 格式与魔数校验失败: " << error << std::endl;
            return 1;
        }
        std::cout << "  [PASS] 产物 MP4 容器魔数校验通过" << std::endl;
    }

    // ----------------------------------------------------
    // 测试 3: 中文路径与深层目录创建测试
    // ----------------------------------------------------
    std::cout << "\n------------------------------------------------------" << std::endl;
    std::cout << "[TEST " << testIndex++ << "] UTF-8 中文路径与深层子目录自动创建测试" << std::endl;
    std::cout << "------------------------------------------------------" << std::endl;

    std::string chinesePath = "./test_mux_out/【超清4K·中文字符测试目录】/多级子目录/2026年热歌榜TOP100_中文产物.mp4";
    auto t1 = std::chrono::high_resolution_clock::now();
    bool ok = Bento4Muxer::muxToMp4(samplePairs[0].videoM4s, samplePairs[0].audioM4s, chinesePath, error);
    auto t2 = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();

    if (!ok) {
        std::cerr << "[FAIL] 中文路径合成失败: " << error << std::endl;
        return 1;
    }
    if (!fs::exists(fs::u8path(chinesePath))) {
        std::cerr << "[FAIL] 中文路径文件未在磁盘上找到！" << std::endl;
        return 1;
    }
    std::cout << "[PASS] 中文路径与深层目录写入成功! 耗时: " << ms << " ms" << std::endl;

    // ----------------------------------------------------
    // 测试 4: 异常与容错降级保护测试
    // ----------------------------------------------------
    std::cout << "\n------------------------------------------------------" << std::endl;
    std::cout << "[TEST " << testIndex++ << "] 异常输入与容错降级保护测试" << std::endl;
    std::cout << "------------------------------------------------------" << std::endl;

    ok = Bento4Muxer::muxToMp4("non_existent_path.m4s", samplePairs[0].audioM4s, "./test_mux_out/should_not_exist.mp4", error);
    if (ok) {
        std::cerr << "[FAIL] 不存在的视频源居然返回了成功！" << std::endl;
        return 1;
    }
    std::cout << "[PASS] 捕获预期错误 (不存在的文件): " << error << std::endl;

    // 写入一个损坏的伪文件测试魔数检测
    std::string fakeFile = "./test_mux_out/fake_corrupted.m4s";
    {
        std::ofstream f(fakeFile, std::ios::binary);
        f << "This is not an MP4 container file, just dummy text!";
    }
    ok = Bento4Muxer::muxToMp4(fakeFile, samplePairs[0].audioM4s, "./test_mux_out/should_not_exist2.mp4", error);
    if (ok) {
        std::cerr << "[FAIL] 损坏的伪媒体文件居然返回了成功！" << std::endl;
        return 1;
    }
    std::cout << "[PASS] 捕获预期错误 (非法 MP4 容器魔数): " << error << std::endl;

    std::cout << "\n======================================================" << std::endl;
    std::cout << "  🎉 ALL TESTS PASSED! Bento4 混流引擎全面验证通过！  " << std::endl;
    std::cout << "======================================================" << std::endl;
    return 0;
}
