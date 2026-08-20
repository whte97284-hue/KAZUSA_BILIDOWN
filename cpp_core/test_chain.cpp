// ============================================================================
//  BiliCommander 三态队列下载链路端到端验证 (无头)
//  驱动与 QML GUI 完全相同的 BiliController 代码路径:
//    解析 → 生成待下载任务 → 入队 → 真实下载(.part+CDN容灾) → 完整性校验
//  用法: BiliChainTest <BV号> [输出目录]
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
#include <QTextStream>
#include "glue/BiliController.hpp"
#include "glue/BiliTaskModel.hpp"
#include <iostream>
#include <fstream>
#include <cstdio>
#include <filesystem>
#include <algorithm>
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

int main(int argc, char *argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    setvbuf(stdout, nullptr, _IONBF, 0); // stdout 无缓冲, 实时看到进度
    QCoreApplication app(argc, argv);

    std::string bv = (argc > 1) ? argv[1] : "BV1sqbmzeEzR";
    std::string outDir = (argc > 2) ? argv[2] : "./test_chain_out";

    std::cout << "================================================================================\n";
    std::cout << "  BiliCommander 三态队列下载链路端到端验证  (目标: " << bv << ")\n";
    std::cout << "================================================================================\n";

    // ---- 0. 任务持久化离线回归 (不联网): JSON 往返一致性 ----
    std::cout << "\n[0] 任务持久化 JSON 往返 ...\n";
    const QString tasksTmp = "./test_tasks_roundtrip.json";
    {
        TaskModel m;
        DownloadTask a;
        a.title = QStringLiteral("往返测试·中文标题");
        a.bvid = "BV1xx411c7mD";
        a.state = DownloadState::Pending;
        a.mode = DownloadMode::Full;
        a.qualityOptions = { "1080P 高清 | avc1", "720P 高清 | avc1" };
        a.qualityIds = { 80, 64 };
        a.codecIds = { 7, 7 };
        a.audioOptions = { "192K | mp4a", "64K | mp4a" };
        a.audioIds = { 30280, 30216 };
        a.selectedQualityIndex = 1;
        a.selectedAudioIndex = 1;
        a.pageIndex = 0;
        a.totalPages = 3;
        a.videoSizeText = "41.33 MB";
        a.saveDir = QStringLiteral("./downloads/测试目录");
        a.statusText = "等待下载";
        int64_t id1 = m.addTask(a);

        DownloadTask b;
        b.title = "续传任务";
        b.state = DownloadState::Downloading;
        b.progress = 55.5;
        b.speedText = "1.2 MB/s";
        b.statusText = "下载中";
        b.rawInput = "BV1xx411c7mD";
        b.pageIndex = 2;
        b.totalPages = 5;
        int64_t id2 = m.addTask(b);

        saveTasksToFile(tasksTmp, m.allTasks(), m.nextId());
        std::vector<DownloadTask> loaded;
        int64_t nextId = 0;
        bool ok = loadTasksFromFile(tasksTmp, loaded, nextId);
        check(ok, "save/load 往返成功");
        check(nextId == std::max(id1, id2) + 1, "nextId 持久化正确");
        check(loaded.size() == 2, "恢复 2 条任务");
        if (ok && loaded.size() == 2) {
            check(loaded[0].title == QStringLiteral("往返测试·中文标题"), "中文标题往返一致");
            check(loaded[0].selectedQualityIndex == 1, "画质选择往返一致");
            check(loaded[0].qualityIds == a.qualityIds, "qualityIds 往返一致");
            check(loaded[0].audioIds == a.audioIds, "audioIds 往返一致");
            check(loaded[0].saveDir == QStringLiteral("./downloads/测试目录"), "saveDir 往返一致");
            check(static_cast<int>(loaded[1].state) == static_cast<int>(DownloadState::Downloading), "下载中状态往返一致");
            check(loaded[1].progress > 55.4 && loaded[1].progress < 55.6, "进度往返一致");
        }
        QFile::remove(tasksTmp);
    }

    // ---- 0.1 恢复语义: 构造控制器时从 tasks.json 重建队列, 下载中降级回待下载 ----
    std::cout << "\n[0.1] 启动恢复语义 (下载中 → 待下载, 靠 .part 续传) ...\n";
    {
        // 先清理旧快照, 再写入一条"下载中"任务, 验证控制器构造时正确降级
        QFile::remove("./tasks.json");
        TaskModel m;
        DownloadTask t;
        t.title = "重启续传任务";
        t.state = DownloadState::Downloading;
        t.progress = 42.0;
        t.statusText = "下载中";
        m.addTask(t);
        saveTasksToFile("./tasks.json", m.allTasks(), m.nextId());
    }

    // ---- 0.2 从 config.json 读取登录态 ----
    BiliController controller;
    check(controller.pendingCount() == 1, "重启后恢复 1 条待下载任务");
    check(controller.downloadingCount() == 0, "下载中任务已降级回待下载");
    controller.clearTab(0); // 清理恢复任务, 保持后续解析断言基线 (pendingCount 归零)
    check(controller.pendingCount() == 0, "清理恢复任务后待下载归零");
    {
        QFile f("./config.json");
        if (f.open(QIODevice::ReadOnly)) {
            QJsonObject cfg = QJsonDocument::fromJson(f.readAll()).object();
            QString sess = cfg.value("sessdata").toString();
            if (!sess.isEmpty()) controller.setSessData(sess);
            std::cout << "  [INFO] 已从 config.json 载入登录态\n";
        } else {
            std::cout << "  [WARN] 未找到 config.json, 以游客身份解析 (仅低画质可用)\n";
        }
        QDir().mkpath(QString::fromStdString(outDir));
        controller.setDownloadDir(QString::fromStdString(outDir));
    }

    // ---- 1. 解析 ----
    std::cout << "\n[1] 视频解析 (异步, 信号驱动) ...\n";
    QEventLoop loopParse;
    bool parseOk = false;
    QString parseErr;
    QObject::connect(&controller, &BiliController::parseSuccess, [&]() {
        parseOk = true;
        loopParse.quit();
    });
    QObject::connect(&controller, &BiliController::parseFailed, [&](const QString &e) {
        parseErr = e;
        loopParse.quit();
    });
    controller.parseVideo(QString::fromStdString(bv));
    QTimer::singleShot(20000, &loopParse, &QEventLoop::quit);
    loopParse.exec();

    check(parseOk, "解析成功 (parseSuccess 信号)");
    if (!parseOk) {
        std::cout << "        错误: " << parseErr.toStdString() << "\n";
        return 1;
    }
    std::cout << "        标题: " << controller.currentTitle().toStdString() << "\n";
    std::cout << "        时长: " << controller.currentDuration().toStdString() << "\n";
    check(controller.isParsing() == false, "解析结束 isParsing 复位为 false");

    // 解析只展示详情, 不自动入队; 模拟用户在详情页点击"投放"手动入队第 1 P
    // (UGC 与番剧统一走同一入队链路, 与 QML 详情页按钮行为一致)
    QVariantList indices;
    indices.append(0);
    controller.enqueuePages(indices, 0, 0, QString::fromStdString(outDir));
    check(controller.pendingCount() == 1, "任务已入队 1 条 (pendingCount==1)");

    // ---- 2. 任务队列入队 ----
    std::cout << "\n[2] 任务队列入队 (FIFO + 单 worker) ...\n";
    int pendingRows = controller.pendingModel()->rowCount();
    check(pendingRows == 1, QString("待下载列表可见 1 条任务 (%1 行)").arg(pendingRows).toStdString());

    // 读待下载任务详情
    QModelIndex idx = controller.pendingModel()->index(0, 0);
    QString title = controller.pendingModel()->data(idx, TaskModel::TitleRole).toString();
    QStringList qopts = controller.pendingModel()->data(idx, TaskModel::QualityOptionsRole).toStringList();
    std::cout << "        任务标题: " << title.toStdString() << "\n";
    std::cout << "        可选画质数: " << qopts.size() << "\n";
    for (int i = 0; i < qopts.size(); ++i)
        std::cout << "          [" << i << "] " << qopts[i].toStdString() << "\n";
    check(!qopts.isEmpty(), "画质下拉选项已生成");

    // 无论 UGC 还是番剧, 入队后都停在待下载, 由测试手动启动 (模拟用户到下载页点击下载)
    // 选最低画质 (最后一个选项, 最小体积, 快速验证; 番剧单流仅 1 个选项也适用)
    int lowQ = qopts.size() - 1;
    controller.setTaskSelection(0, lowQ, 0);
    std::cout << "        选择画质: " << qopts[lowQ].toStdString() << "\n";

    // 诊断: 确认入队接口生效
    controller.startDownloadTask(0);
    QTimer::singleShot(1500, [&]() {
        QModelIndex di = controller.pendingModel()->index(0, 0);
        int st = controller.pendingModel()->data(di, TaskModel::StateRole).toInt();
        QString stt = controller.pendingModel()->data(di, TaskModel::StatusTextRole).toString();
        std::cout << "        [诊断] startDownloadTask 后 1.5s: pending=" << controller.pendingCount()
                  << " downloading=" << controller.downloadingCount()
                  << " completed=" << controller.completedCount()
                  << " | state=" << st << " status='" << stt.toStdString() << "'\n";
    });

    // ---- 3. 启动下载并监控状态迁移 ----
    std::cout << "\n[3] 启动下载, 监控 Pending→Downloading→Completed ...\n";
    QEventLoop loopDl;
    bool sawDownloading = false;
    QString dlFinishMsg;
    bool dlFinishedSignaled = false;
    QTimer pollTimer;
    QTimer timeoutTimer;
    QObject::connect(&pollTimer, &QTimer::timeout, [&]() {
        if (controller.downloadingCount() == 1) sawDownloading = true;
        if (controller.completedCount() == 1) loopDl.quit();
    });
    pollTimer.start(300);

    // 终态信号: 失败立即收敛, 不用干等 180s; 成功由 completedCount 收敛
    QObject::connect(&controller, &BiliController::downloadFinished, [&](bool success, const QString &msg) {
        dlFinishedSignaled = true;
        dlFinishMsg = msg;
        std::cout << "        [终态] downloadFinished(" << (success ? "成功" : "失败")
                  << ") 消息: " << msg.toStdString() << "\n";
        if (!success) loopDl.quit();
    });

    QObject::connect(&timeoutTimer, &QTimer::timeout, [&]() {
        std::cout << "        [超时] 当前状态: pending=" << controller.pendingCount()
                  << " downloading=" << controller.downloadingCount()
                  << " completed=" << controller.completedCount() << "\n";
        loopDl.quit();
    });
    timeoutTimer.setSingleShot(true);
    timeoutTimer.start(180000); // 180s 兜底 (正常由终态信号提前退出)

    loopDl.exec();
    pollTimer.stop();

    check(controller.completedCount() == 1, "任务最终到达 Completed (completedCount==1)");
    check(sawDownloading, "状态迁移经过 Downloading (downloadingCount 出现过 1)");
    check(controller.pendingCount() == 0, "任务已移出待下载 (pendingCount==0)");

    // 读取完成状态
    QModelIndex cidx = controller.completedModel()->index(0, 0);
    QString statusText = controller.completedModel()->data(cidx, TaskModel::StatusTextRole).toString();
    QString filePath = controller.completedModel()->data(cidx, TaskModel::FilePathRole).toString();
    double progress = controller.completedModel()->data(cidx, TaskModel::ProgressRole).toDouble();
    std::cout << "        状态: " << statusText.toStdString() << "\n";
    std::cout << "        进度: " << progress << "%\n";
    std::cout << "        输出: " << filePath.toStdString() << "\n";
    check(statusText == "下载完成", "statusText 收敛为 '下载完成'");
    check(progress >= 100.0, "进度收敛为 100%");

    // ---- 4. 文件落盘与完整性校验 ----
    std::cout << "\n[4] 文件落盘与完整性校验 ...\n";
    auto sizeOf = [](const std::string &p) {
        std::error_code ec;
        auto sz = std::filesystem::file_size(u8p(p), ec);
        return ec ? -1 : static_cast<int64_t>(sz);
    };

    // 单流 MP4 (番剧试看): filePath 指向最终 .mp4 文件 (无独立音轨);
    // DASH: filePath 指向分P目录. 混流开启且成功时, 目录内只留最终 .mp4 (m4s 已清理);
    //       混流关闭/降级时, 目录内为 video.m4s + audio.m4s.
    const bool singleFile = filePath.endsWith(".mp4", Qt::CaseInsensitive);
    std::string coverPath, dmPath;

    // 若 DASH 目录内存在最终 .mp4 (混流产物), 统一按单文件产物校验
    std::string finalMp4;
    if (!singleFile) {
        QDir pdir(filePath);
        QStringList mp4s = pdir.entryList(QStringList() << "*.mp4", QDir::Files);
        if (!mp4s.isEmpty()) finalMp4 = pdir.filePath(mp4s.first()).toStdString();
    }

    const bool singleFileOrMuxed = singleFile || !finalMp4.empty();
    if (singleFileOrMuxed) {
        const std::string mp4 = singleFile ? filePath.toStdString() : finalMp4;
        check(std::filesystem::exists(u8p(mp4)), "最终 MP4 已落盘 (单流/混流产物)");
        check(isValidMediaContainer(mp4), "MP4 容器头校验通过 (ftyp/moof/mdat)");
        int64_t mSize = sizeOf(mp4);
        std::cout << "        MP4 大小: " << (mSize / 1024.0 / 1024.0) << " MB\n";
        check(mSize > 1024 * 1024, "MP4 > 1MB (真实数据流, 非 mock)");
        check(!std::filesystem::exists(u8p(mp4 + ".part")), "无 .part 残留 (原子改名成功)");
        // 封面/弹幕位于 mp4 所在目录
        auto pp = u8p(mp4).parent_path().u8string();
        coverPath = pp + "/cover.jpg";
        dmPath = pp + "/danmaku.xml";
    } else {
        std::string vpath = filePath.toStdString() + "/video.m4s";
        std::string apath = filePath.toStdString() + "/audio.m4s";
        coverPath = filePath.toStdString() + "/cover.jpg";
        dmPath = filePath.toStdString() + "/danmaku.xml";

        // 用与下载完全同源的宽字符路径转换链校验, 避免 Qt/标准库编码视角不一致
        check(std::filesystem::exists(u8p(vpath)), "视频轨已落盘 video.m4s");
        check(std::filesystem::exists(u8p(apath)), "音频轨已落盘 audio.m4s");
        int64_t vSize = sizeOf(vpath), aSize = sizeOf(apath);
        std::cout << "        视频轨大小: " << (vSize / 1024.0 / 1024.0) << " MB\n";
        std::cout << "        音频轨大小: " << (aSize / 1024.0 / 1024.0) << " MB\n";
        check(vSize > 1024 * 1024, "视频轨 > 1MB (真实数据流, 非 mock)");
        check(aSize > 100 * 1024, "音频轨 > 100KB (真实数据流, 非 mock)");
        check(isValidMediaContainer(vpath), "视频容器头校验通过 (ftyp/moof/mdat)");
        // 检查 .part 残留 (原子改名应已清理)
        check(!std::filesystem::exists(u8p(vpath + ".part")) &&
              !std::filesystem::exists(u8p(apath + ".part")), "无 .part 残留 (原子改名成功)");
    }
    check(std::filesystem::exists(u8p(coverPath)), "封面已落盘 cover.jpg");
    check(std::filesystem::exists(u8p(dmPath)), "弹幕已落盘 danmaku.xml");

    // ---- 5. 汇总 ----
    std::cout << "\n================================================================================\n";
    std::cout << "  验证结果: PASS " << g_pass << " / FAIL " << g_fail << "\n";
    std::cout << "================================================================================\n";
    return g_fail == 0 ? 0 : 1;
}
