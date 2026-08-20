// ============================================================================
//  番剧多P批量下载状态完备性验证 (无头)
//  驱动与 QML GUI 相同的 BiliController:
//    解析 → enqueuePages 批量入队全部 P → 逐个手动 startDownloadTask →
//    逐 P 校验 (title / filePath 唯一 / 文件落盘 + 容器魔数 / 状态迁移)
//  用法: PgcMultiTest <ss号> [输出目录]
// ============================================================================
#define NOMINMAX
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include "glue/BiliController.hpp"
#include "glue/BiliTaskModel.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <windows.h>

static int g_pass = 0;
static int g_fail = 0;

// 与下载同源的 UTF-8 → 宽字符路径转换 (MSVC u8path 在 GBK 代码页会抛异常, 必须手工转码)
static std::filesystem::path u8p(const std::string &s) {
    if (s.empty()) return std::filesystem::path();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return std::filesystem::path();
    std::wstring ws(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &ws[0], n);
    return std::filesystem::path(ws);
}

static void check(bool ok, const std::string &name) {
    if (ok) {
        ++g_pass;
        std::cout << "  [PASS] " << name << "\n";
    } else {
        ++g_fail;
        std::cout << "  [FAIL] " << name << "\n";
    }
}

// 读取视频容器头, 校验是否含 MP4 fMP4 魔数
static bool isValidMediaContainer(const std::string &path) {
    std::ifstream f(u8p(path), std::ios::binary);
    if (!f) return false;
    char buf[64] = {0};
    f.read(buf, 64);
    std::string s(buf, 64);
    return s.find("ftyp") != std::string::npos ||
           s.find("moof") != std::string::npos ||
           s.find("mdat") != std::string::npos;
}

static int64_t sizeOf(const std::string &p) {
    std::error_code ec;
    auto sz = std::filesystem::file_size(u8p(p), ec);
    return ec ? -1 : static_cast<int64_t>(sz);
}

int main(int argc, char *argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    setvbuf(stdout, nullptr, _IONBF, 0);
    QCoreApplication app(argc, argv);

    std::string input = (argc > 1) ? argv[1] : "ss1702";
    std::string outDir = (argc > 2) ? argv[2] : "./test_pgc_multi_out";

    std::cout << "================================================================================\n";
    std::cout << "  番剧多P批量下载状态完备性验证  (目标: " << input << ")\n";
    std::cout << "================================================================================\n";

    // 清理上次运行的任务快照, 避免构造控制器时恢复残留任务干扰状态断言
    QFile::remove("./tasks.json");

    BiliController controller;
    {
        QFile f("./config.json");
        if (f.open(QIODevice::ReadOnly)) {
            QJsonObject cfg = QJsonDocument::fromJson(f.readAll()).object();
            QString sess = cfg.value("sessdata").toString();
            if (!sess.isEmpty()) controller.setSessData(sess);
            std::cout << "  [INFO] 已从 config.json 载入登录态\n";
        } else {
            std::cout << "  [WARN] 未找到 config.json, 以游客身份解析\n";
        }
        QDir().mkpath(QString::fromStdString(outDir));
        controller.setDownloadDir(QString::fromStdString(outDir));
    }

    // ---- 1. 解析番剧 ----
    std::cout << "\n[1] 番剧详情解析 (异步, 信号驱动) ...\n";
    QEventLoop loopParse;
    bool parseOk = false;
    QString parseErr;
    QObject::connect(&controller, &BiliController::parseSuccess, [&]() { parseOk = true; loopParse.quit(); });
    QObject::connect(&controller, &BiliController::parseFailed, [&](const QString &e) { parseErr = e; loopParse.quit(); });
    controller.parseVideo(QString::fromStdString(input));
    QTimer::singleShot(20000, &loopParse, &QEventLoop::quit);
    loopParse.exec();

    check(parseOk, "番剧解析成功 (parseSuccess 信号)");
    if (!parseOk) {
        std::cout << "        错误: " << parseErr.toStdString() << "\n";
        return 1;
    }
    const int pageCount = controller.pageCount();
    std::cout << "        标题: " << controller.currentTitle().toStdString() << "\n";
    std::cout << "        isBangumi: " << (controller.isBangumi() ? "true" : "false") << "\n";
    std::cout << "        分P总数: " << pageCount << "\n";
    check(controller.isBangumi() && pageCount >= 2, QString("番剧多P识别 (%1 个 P)").arg(pageCount).toStdString());

    // ---- 2. 批量入队全部 P (只入待下载, 不自动启动) ----
    std::cout << "\n[2] enqueuePages 批量入队全部 P (只入待下载, 不自动启动) ...\n";
    QVariantList allPages;
    for (int i = 0; i < pageCount; ++i) allPages.append(i);
    controller.enqueuePages(allPages, 0, 0, QString::fromStdString(outDir));
    check(controller.pendingCount() == pageCount, QString("入队 %1 条待下载任务 (pendingCount==%1)").arg(pageCount).toStdString());

    // 校验各任务 title 唯一且命名符合 番剧-P 规范
    std::vector<QString> titles;
    for (int i = 0; i < pageCount; ++i) {
        QModelIndex idx = controller.pendingModel()->index(i, 0);
        QString t = controller.pendingModel()->data(idx, TaskModel::TitleRole).toString();
        titles.push_back(t);
        std::cout << "        待下载[" << i << "] title='" << t.toStdString() << "'\n";
    }
    {
        bool uniq = true;
        for (int i = 0; i < pageCount; ++i)
            for (int j = i + 1; j < pageCount; ++j)
                if (titles[i] == titles[j]) uniq = false;
        check(uniq, "各 P 任务 title 唯一 (P1=季名, P2+=季名-P 名, 互不覆盖)");
    }

    // ---- 3. 逐个手动启动并校验状态迁移 / filePath / 文件落盘 ----
    std::cout << "\n[3] 逐个 startDownloadTask 启动, 逐 P 状态与产物校验 ...\n";
    std::vector<QString> filePaths;
    bool sawDownloading = false;
    QObject::connect(&controller, &BiliController::downloadFinished,
                     [&](bool success, const QString &msg) {
        std::cout << "        [终态] downloadFinished(" << (success ? "成功" : "失败") << ") " << msg.toStdString() << "\n";
    });

    for (int p = 0; p < pageCount; ++p) {
        // 每次启动待下载队列头部 (前一个完成后剩余任务前移, 恒为 index 0)
        controller.startDownloadTask(0);
        QEventLoop loopDl;
        QTimer poll, timeout;
        bool sawThisDl = false;
        QObject::connect(&poll, &QTimer::timeout, [&]() {
            if (controller.downloadingCount() == 1) sawThisDl = true;
            if (controller.completedCount() >= p + 1) loopDl.quit();
        });
        QObject::connect(&timeout, &QTimer::timeout, [&]() {
            std::cout << "        [超时] P[" << p << "] pending=" << controller.pendingCount()
                      << " downloading=" << controller.downloadingCount()
                      << " completed=" << controller.completedCount() << "\n";
            loopDl.quit();
        });
        poll.start(300);
        timeout.setSingleShot(true);
        timeout.start(300000); // 单 P 5 分钟兜底
        loopDl.exec();
        poll.stop();

        check(controller.completedCount() >= p + 1, QString("P[%1] 任务到达 Completed").arg(p).toStdString());
        check(sawThisDl, QString("P[%1] 状态迁移经过 Downloading").arg(p).toStdString());

        QModelIndex cidx = controller.completedModel()->index(p, 0);
        QString stt = controller.completedModel()->data(cidx, TaskModel::StatusTextRole).toString();
        QString fp = controller.completedModel()->data(cidx, TaskModel::FilePathRole).toString();
        double prog = controller.completedModel()->data(cidx, TaskModel::ProgressRole).toDouble();
        filePaths.push_back(fp);
        std::cout << "        P[" << p << "] 状态='" << stt.toStdString()
                  << "' 进度=" << prog << "% 输出=" << fp.toStdString() << "\n";
        check(stt == "下载完成" && prog >= 100.0, QString("P[%1] statusText/进度收敛").arg(p).toStdString());

        // 产物校验: 单流 MP4 → .mp4 文件; DASH → 目录 video.m4s/audio.m4s
        if (fp.endsWith(".mp4", Qt::CaseInsensitive)) {
            std::string mp4 = fp.toStdString();
            check(std::filesystem::exists(u8p(mp4)), QString("P[%1] 单流 MP4 已落盘").arg(p).toStdString());
            check(isValidMediaContainer(mp4), QString("P[%1] MP4 容器魔数校验通过 (ftyp/moof/mdat)").arg(p).toStdString());
            check(sizeOf(mp4) > 1024 * 1024, QString("P[%1] MP4 > 1MB (真实数据流)").arg(p).toStdString());
        } else {
            std::string vp = fp.toStdString() + "/video.m4s";
            std::string ap = fp.toStdString() + "/audio.m4s";
            check(std::filesystem::exists(u8p(vp)), QString("P[%1] DASH 视频轨已落盘 video.m4s").arg(p).toStdString());
            check(std::filesystem::exists(u8p(ap)), QString("P[%1] DASH 音频轨已落盘 audio.m4s").arg(p).toStdString());
        }
    }

    // ---- 4. 多 P 产物互不覆盖 ----
    {
        bool uniq = true;
        for (int i = 0; i < pageCount; ++i)
            for (int j = i + 1; j < pageCount; ++j)
                if (filePaths[i] == filePaths[j]) uniq = false;
        check(uniq, "各 P filePath 唯一 (内容互不覆盖)");
    }

    // ---- 5. 汇总 ----
    std::cout << "\n================================================================================\n";
    std::cout << "  验证结果: PASS " << g_pass << " / FAIL " << g_fail << "\n";
    std::cout << "================================================================================\n";
    return g_fail == 0 ? 0 : 1;
}
