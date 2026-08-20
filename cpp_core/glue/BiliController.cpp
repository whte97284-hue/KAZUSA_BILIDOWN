#include <QGuiApplication>
#include <QClipboard>
#define NOMINMAX
#include "BiliController.hpp"
#include "../Bento4Muxer.hpp"
#include <QMetaObject>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QStandardPaths>
#include <windows.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <objbase.h>
#include <shobjidl.h>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <exception>

// UTF-8 std::string → 宽字符路径。
// 注意: 不能用 std::filesystem::u8path (MSVC 上对 GBK 系统代码页的中文会抛
// ERROR_NO_UNICODE_TRANSLATION), 必须用 MultiByteToWideChar(CP_UTF8) 手工转码。
static std::filesystem::path u8p(const std::string &s) {
    if (s.empty()) return std::filesystem::path();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return std::filesystem::path();
    std::wstring ws(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &ws[0], n);
    return std::filesystem::path(ws);
}

static QString formatSeconds(int64_t seconds) {
    int64_t m = seconds / 60;
    int64_t s = seconds % 60;
    if (m >= 60) {
        int64_t h = m / 60;
        m = m % 60;
        return QString("%1:%2:%3")
            .arg(h, 2, 10, QChar('0'))
            .arg(m, 2, 10, QChar('0'))
            .arg(s, 2, 10, QChar('0'));
    }
    return QString("%1:%2")
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0'));
}

BiliController::BiliController(QObject *parent) : QObject(parent) {
    // 三个状态代理挂到同一个唯一数据源
    m_pendingProxy.setSourceModel(&m_taskModel);
    m_pendingProxy.setStates({ DownloadState::Pending });
    m_downloadingProxy.setSourceModel(&m_taskModel);
    m_downloadingProxy.setStates({ DownloadState::Downloading });
    m_completedProxy.setSourceModel(&m_taskModel);
    m_completedProxy.setStates({ DownloadState::Completed });

    // 模型内部计数变化转发给 QML
    connect(&m_taskModel, &TaskModel::taskCountsChanged, this, &BiliController::taskCountsChanged);

    // 历史归档模型与落盘
    connect(&m_historyModel, &HistoryModel::historyUpdated, this, &BiliController::historyChanged);
    m_historyModel.loadFromFile("./history.json");

    loadLocalConfig();

    // 启动恢复: 从 tasks.json 重建待下载队列 (下载中任务降级回待下载, 靠 .part 续传)
    restoreTasks();

    // 注入全局限速器 (多线程引擎 + 单线程下载共用同一令牌桶)
    m_multiDownloader.setRateLimiter(&m_rateLimiter);
    m_downloader.setRateLimiter(&m_rateLimiter);
    m_rateLimiter.setRate(static_cast<size_t>(m_config.maxDownloadSpeedKB) * 1024ULL);

    // 账号会话定时刷新 (对齐成熟项目): 每 30 分钟静默校验登录态,
    // 会话过期时自动 refreshToken 续期, 续期失败则降级弹出重新扫码
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(30 * 60 * 1000);
    connect(m_refreshTimer, &QTimer::timeout, this, &BiliController::refreshUserProfile);
    m_refreshTimer->start();
}

BiliController::~BiliController() {
    // 通知 worker 退出并等待其清理 (不 cancel 进行中的下载, 由 .part 续传保证可恢复)
    m_workerStop = true;
    m_queueCv.notify_all();
    if (m_workerThread.joinable()) m_workerThread.join();
    // 退出前落盘活跃任务 (待下载+下载中), 供下次启动恢复
    persistTasks();
}

QString BiliController::currentDuration() const {
    // 时长属于每个分 P (VideoPage.duration), 顶层无聚合字段
    if (m_selectedPageIndex >= 0 && m_selectedPageIndex < static_cast<int>(m_currentDetail.pages.size())) {
        return formatSeconds(m_currentDetail.pages[m_selectedPageIndex].duration);
    }
    return "00:00";
}

void BiliController::loadLocalConfig() {
    std::string configPath = "./config.json";
    if (BiliAuth::loadConfig(configPath, m_config)) {
        if (!m_config.sessdata.empty()) {
            m_parser.setSessData(m_config.sessdata);
            m_downloader.setSessData(m_config.sessdata);
            m_multiDownloader.setSessData(m_config.sessdata);
            updateUserData(m_config);
        } else {
            // 未登录: 后台静默预热二维码，0 延迟秒开
            requestQrCode();
        }
    } else {
        // 无配置: 后台静默预热二维码，0 延迟秒开
        requestQrCode();
    }
}

void BiliController::setSessData(const QString &sessData) {
    std::string sess = sessData.trimmed().toStdString();
    m_config.sessdata = sess;
    m_parser.setSessData(sess);
    m_downloader.setSessData(sess);
    m_multiDownloader.setSessData(sess);
    updateUserData(sess);
}

void BiliController::updateUserData(const std::string &sessData) {
    AppConfig cfg = m_config;
    cfg.sessdata = sessData;
    updateUserData(cfg);
}

void BiliController::updateUserData(const AppConfig &config) {
    qDebug() << "[QR-LOGIN] updateUserData(AppConfig) called, sessdata.len=" << config.sessdata.size();
    std::thread([this, config]() {
        UserProfile prof;
        std::string err;
        bool ok = m_auth.fetchUserProfile(config, prof, err);
        qDebug() << "[QR-LOGIN] fetchUserProfile returned ok=" << ok
                 << "uname=" << QString::fromStdString(prof.uname)
                 << "err=" << QString::fromStdString(err);
        QMetaObject::invokeMethod(this, [this, ok, prof, config]() {
            qDebug() << "[QR-LOGIN] GUI callback: ok=" << ok << "emitting userProfileChanged";
            if (ok) {
                m_userProfile = prof;
            } else {
                // 如果 fetchUserProfile 网络超时或临时失败，标记已有凭证登录
                m_userProfile.isLogin = !config.sessdata.empty();
                if (!config.dedeUserId.empty()) {
                    m_userProfile.mid = std::stoll(config.dedeUserId);
                    m_userProfile.uname = "UID: " + config.dedeUserId;
                } else {
                    m_userProfile.uname = "已登录用户";
                }
                m_userProfile.vipLabel = "已认证";
            }
            emit userProfileChanged();
        });
    }).detach();
}

// 账号会话定时刷新 (QTimer 每 30 分钟触发)
// 健康检查三级处理:
//   OK      → 静默更新最新资料
//   EXPIRED → 先用 refreshToken 自动续期; 续期成功则重新拉取画像, 失败则降级未登录并弹扫码
//   NETWORK / SERVER → 临时故障, 保留旧资料静默跳过, 下次周期再试
void BiliController::refreshUserProfile() {
    if (m_config.sessdata.empty()) return; // 未登录无需刷新
    bool expected = false;
    if (!m_isRefreshing.compare_exchange_strong(expected, true)) return; // 防并发重入

    AppConfig snapshot = m_config;
    std::thread([this, snapshot]() {
        UserProfile prof;
        std::string err;
        AuthError ae = m_auth.checkSession(snapshot, prof, err);

        if (ae == AuthError::OK) {
            QMetaObject::invokeMethod(this, [this, prof]() {
                m_userProfile = prof;
                emit userProfileChanged();
            });
        } else if (ae == AuthError::EXPIRED) {
            // 凭证过期: 尝试 refreshToken 静默续期
            AppConfig renewed = snapshot;
            std::string rerr;
            const bool renewedOk = m_auth.refreshSession(renewed, rerr);
            if (renewedOk) {
                QMetaObject::invokeMethod(this, [this, renewed]() {
                    m_config = renewed;
                    BiliAuth::saveConfig("./config.json", m_config);
                    m_parser.setSessData(m_config.sessdata);
                    m_downloader.setSessData(m_config.sessdata);
                    m_multiDownloader.setSessData(m_config.sessdata);
                });
                // 续期成功后立即重新拉取完整画像
                UserProfile prof2;
                std::string err2;
                if (m_auth.checkSession(renewed, prof2, err2) == AuthError::OK) {
                    QMetaObject::invokeMethod(this, [this, prof2]() {
                        m_userProfile = prof2;
                        emit userProfileChanged();
                    });
                }
            } else {
                // 续期失败: 凭证彻底失效, 降级未登录并请求重新扫码
                qDebug() << "[AUTH-REFRESH] refresh_token 失效, 需重新登录: "
                         << QString::fromStdString(rerr);
                QMetaObject::invokeMethod(this, [this]() {
                    m_userProfile = UserProfile();
                    emit userProfileChanged();
                    emit accountExpired();
                    emit requestLoginDialog();
                });
            }
        }
        // NETWORK / SERVER: 临时故障, 保留旧资料, 下次周期再试 (不打扰用户)

        QMetaObject::invokeMethod(this, [this]() {
            m_isRefreshing = false;
        });
    }).detach();
}

void BiliController::logout() {
    m_config.sessdata.clear();
    m_config.bili_jct.clear();
    m_config.dedeUserId.clear();
    m_config.refreshToken.clear();
    m_userProfile = UserProfile();
    m_parser.setSessData("");
    m_downloader.setSessData("");
    m_multiDownloader.setSessData("");
    BiliAuth::saveConfig("./config.json", m_config);
    emit userProfileChanged();

    // 登出后立即在后台静默预热新二维码，确保随时点开即扫
    requestQrCode();
}

// 1. 智能多键缓存解析视频 (原子 CAS 竞态保护 + 10分钟 TTL 避免重复请求)
void BiliController::parseVideo(const QString &rawInput) {
    if (rawInput.trimmed().isEmpty()) return;

    bool expected = false;
    if (!m_isParsing.compare_exchange_strong(expected, true)) {
        return; // 并发竞态防重入
    }

    m_parseErrorMessage.clear();
    m_lastRawInput = rawInput.trimmed();
    emit isParsingChanged();
    emit parseErrorChanged();

    std::string input = m_lastRawInput.toStdString();

    // 检查多键缓存 (TTL 10 分钟)
    {
        std::lock_guard<std::mutex> lk(m_cacheMutex);
        auto it = m_detailCacheMap.find(input);
        if (it != m_detailCacheMap.end()) {
            auto age = std::chrono::duration_cast<std::chrono::minutes>(
                std::chrono::steady_clock::now() - it->second.timestamp);
            if (age.count() < 10 && !it->second.detail.pages.empty()) {
                it->second.timestamp = std::chrono::steady_clock::now(); // LRU 命中刷新, 防止热项被驱逐
                m_isParsing = false;
                emit isParsingChanged();

                m_currentDetail = it->second.detail;
                m_selectedPageIndex = 0;
                m_pageModel.setPages(m_currentDetail.pages);
                m_trackModel.setTracks(m_currentDetail.videoTracks);
                emit videoDetailChanged();
                emit selectedPageIndexChanged();

                // 解析仅展示详情, 不自动入队 —— 由用户在详情页点击"投放"后统一生成任务卡片
                emit parseSuccess();
                return;
            }
        }
    }

    std::thread([this, input]() {
        BiliVideoDetail detail;
        std::string err;

        bool ok;
        {
            // Wbi key 热更新 + 播放地址刷新都走同一解析器, 加锁串行化保护
            std::lock_guard<std::mutex> lk(m_parserMutex);
            ok = m_parser.fetchVideoDetail(input, detail, err);
            if (ok) {
                // 解析第 1 P 的流地址
                ok = m_parser.fetchPlayUrl(detail, 0, err);
            }
        }

        QMetaObject::invokeMethod(this, [this, ok, detail, err, input]() {
            m_isParsing = false;
            emit isParsingChanged();

            if (ok) {
                m_currentDetail = detail;
                m_selectedPageIndex = 0;
                m_pageModel.setPages(detail.pages);
                m_trackModel.setTracks(detail.videoTracks);
                emit videoDetailChanged();
                emit selectedPageIndexChanged();

                // 写入多键缓存 (10 分钟 TTL)
                {
                    std::lock_guard<std::mutex> lk(m_cacheMutex);
                    m_detailCacheMap[input] = { detail, std::chrono::steady_clock::now() };
                    if (!detail.bvid.empty()) {
                        m_detailCacheMap[detail.bvid] = { detail, std::chrono::steady_clock::now() };
                    }
                    m_cachedInput = input;
                    m_cachedDetail = detail;
                    m_cachedAt = std::chrono::steady_clock::now();
                }
                pruneDetailCache(); // 有界化: 剔除过期项 + 超上限逐出最旧

                // 解析仅展示详情, 不自动入队 —— 由用户在详情页点击"投放"后统一生成任务卡片
                if (detail.isPreview) {
                    emit vipLockNotice(QStringLiteral("该番剧全集需大会员，当前仅可下载 6 分钟试看"));
                }
                emit parseSuccess();
            } else {
                m_parseErrorMessage = QString::fromStdString(err);
                emit parseErrorChanged();
                emit parseFailed(m_parseErrorMessage);
            }
        });
    }).detach();
}

// 解析缓存有界化 (LRU + TTL 双约束): 命中刷新时间戳, 写入后调用本函数
// 剔除超 10 分钟 TTL 的过期项; 仍超 32 条上限则按最旧优先驱逐, 防长期运行内存缓慢膨胀
void BiliController::pruneDetailCache() {
    std::lock_guard<std::mutex> lk(m_cacheMutex);
    constexpr size_t kCacheCap = 32;
    constexpr auto kTtl = std::chrono::minutes(10);
    const auto now = std::chrono::steady_clock::now();

    // 1. 先剔除超 TTL 的过期项
    for (auto it = m_detailCacheMap.begin(); it != m_detailCacheMap.end();) {
        if (now - it->second.timestamp > kTtl) {
            it = m_detailCacheMap.erase(it);
        } else {
            ++it;
        }
    }

    // 2. 仍超上限则逐出最旧项, 直到回归上限内
    while (m_detailCacheMap.size() > kCacheCap) {
        auto oldest = m_detailCacheMap.begin();
        for (auto it = m_detailCacheMap.begin(); it != m_detailCacheMap.end(); ++it) {
            if (it->second.timestamp < oldest->second.timestamp) oldest = it;
        }
        m_detailCacheMap.erase(oldest);
    }
}

// 详细下载区：全部 P 批量入队
void BiliController::enqueueAllPages(int quality, int codecId, const QString &customSaveDir) {
    if (m_currentDetail.pages.empty()) return;
    QVariantList list;
    for (int i = 0; i < static_cast<int>(m_currentDetail.pages.size()); ++i) {
        list.append(i);
    }
    enqueuePages(list, quality, codecId, customSaveDir);
}

// 详细下载区：指定选中索引批量入队
void BiliController::enqueuePages(const QVariantList &pageIndices, int quality, int codecId, const QString &customSaveDir) {
    if (m_currentDetail.pages.empty() || pageIndices.isEmpty()) return;

    QString saveDir = customSaveDir.isEmpty() ? QString::fromStdString(m_config.downloadPath) : customSaveDir;
    if (saveDir.isEmpty()) saveDir = "./downloads";

    int q = quality > 0 ? quality : defaultQuality();
    int c = codecId > 0 ? codecId : defaultCodec();

    for (const auto &var : pageIndices) {
        int pageIdx = var.toInt();
        if (pageIdx < 0 || pageIdx >= static_cast<int>(m_currentDetail.pages.size())) continue;

        DownloadTask t;
        t.rawInput = m_lastRawInput.isEmpty() ? QString::fromStdString(m_currentDetail.bvid) : m_lastRawInput;
        t.bvid = QString::fromStdString(m_currentDetail.bvid);
        t.ownerName = QString::fromStdString(m_currentDetail.ownerName);
        t.pageIndex = pageIdx;
        t.totalPages = static_cast<int>(m_currentDetail.pages.size());
        t.saveDir = saveDir;
        t.qualityId = q;
        t.codecId = c;

        // 任务标题生成 (规范化)
        const auto &page = m_currentDetail.pages[pageIdx];
        if (m_currentDetail.isBangumi) {
            t.title = QString("%1 - %2").arg(QString::fromStdString(m_currentDetail.title), QString::fromStdString(page.part));
        } else if (m_currentDetail.pages.size() > 1) {
            t.title = QString("%1 - P%2 %3").arg(QString::fromStdString(m_currentDetail.title)).arg(pageIdx + 1, 2, 10, QChar('0')).arg(QString::fromStdString(page.part));
        } else {
            t.title = QString::fromStdString(m_currentDetail.title);
        }

        t.coverUrl = page.firstFrame.empty() ? QString::fromStdString(m_currentDetail.coverUrl) : QString::fromStdString(page.firstFrame);
        t.durationDesc = formatSeconds(page.duration);
        t.statusText = "就绪";

        // 填充画质/音质下拉选项 (UGC 自动入队与番剧批量入队共用)
        populateTaskOptions(t, m_currentDetail);

        // 用番剧页下拉的选择覆盖默认选项: 找到 (画质, 编码) 匹配项, 否则保留默认最高清
        for (int i = 0; i < t.qualityIds.size(); ++i) {
            if (t.qualityIds[i] == q && t.codecIds[i] == c) {
                t.selectedQualityIndex = i;
                break;
            }
        }
        t.qualityId = valueAt(t.qualityIds, t.selectedQualityIndex);
        t.codecId = valueAt(t.codecIds, t.selectedQualityIndex);

        // 先加入任务模型拿到真实 id 并显示在"待下载"列表;
        // 注意: 只入列表, 不自动启动后台队列 —— 由用户到下载页点击卡片上的"下载"再真正开始
        t.id = m_taskModel.addTask(t);
    }
}

// 详细下载区：强制刷新当前视频缓存并重新解析
void BiliController::refreshDetail() {
    if (m_lastRawInput.isEmpty()) return;
    std::string input = m_lastRawInput.toStdString();
    {
        std::lock_guard<std::mutex> lk(m_cacheMutex);
        m_detailCacheMap.erase(input);
        if (!m_currentDetail.bvid.empty()) {
            m_detailCacheMap.erase(m_currentDetail.bvid);
        }
        m_cachedInput.clear();
    }
    parseVideo(m_lastRawInput);
}

// 详细下载区：返回/清空当前解析详情
void BiliController::clearDetail() {
    m_currentDetail = BiliVideoDetail{};
    m_pageModel.clear();
    m_trackModel.clear();
    m_selectedPageIndex = 0;
    emit videoDetailChanged();
    emit selectedPageIndexChanged();
}

// 2. 切换选集 / 分 P
void BiliController::selectPage(int pageIndex) {
    if (pageIndex < 0 || pageIndex >= static_cast<int>(m_currentDetail.pages.size())) {
        return;
    }

    m_selectedPageIndex = pageIndex;
    emit selectedPageIndexChanged();

    std::thread([this, pageIndex]() {
        BiliVideoDetail copyDetail = m_currentDetail;
        std::string err;
        bool ok;
        {
            // 与 worker 线程串行化解析器访问, 保护 Wbi key 热更新
            std::lock_guard<std::mutex> lk(m_parserMutex);
            ok = m_parser.fetchPlayUrl(copyDetail, pageIndex, err);
        }

        QMetaObject::invokeMethod(this, [this, ok, copyDetail, pageIndex, err]() {
            if (ok) {
                m_currentDetail.videoTracks = copyDetail.videoTracks;
                m_currentDetail.audioTracks = copyDetail.audioTracks;
                m_currentDetail.singleStreamMp4 = copyDetail.singleStreamMp4;
                m_trackModel.setTracks(copyDetail.videoTracks);
                emit videoDetailChanged();
            } else {
                emit parseFailed(QString::fromStdString(err));
            }
        });
    }).detach();
}

// 3. 启动全流程异步下载
void BiliController::startDownload(int quality, int codecId, const QString &customSaveDir) {
    if (m_isDownloading || m_currentDetail.pages.empty()) return;

    m_isDownloading = true;
    m_downloadPercent = 0.0;
    m_downloadSpeedText = "0 MB/s";
    m_downloadStatusText = "正在准备下载任务...";
    emit isDownloadingChanged();
    emit downloadProgressUpdated();

    QString saveDir = customSaveDir;
    if (saveDir.isEmpty()) {
        saveDir = QString::fromStdString(m_config.downloadPath);
    }
    if (saveDir.isEmpty()) {
        saveDir = "./downloads";
    }

    QDir dir(saveDir);
    if (!dir.exists()) dir.mkpath(".");

    BiliVideoDetail detail = m_currentDetail;
    int pageIdx = m_selectedPageIndex;
    std::string outDir = saveDir.toStdString();

    std::thread([this, detail, pageIdx, outDir, quality, codecId]() {
        auto videoProg = [this](int64_t dl, int64_t total, double speed, double pct) {
            QMetaObject::invokeMethod(this, [this, dl, total, speed, pct]() {
                m_downloadPercent = pct * 0.8; // 视频占 80% 进度
                double speedMb = speed / (1024.0 * 1024.0);
                m_downloadSpeedText = QString("%1 MB/s").arg(speedMb, 0, 'f', 2);
                m_downloadStatusText = QString("正在下载高清视频流 (%1 MB / %2 MB)...")
                    .arg(dl / (1024.0 * 1024.0), 0, 'f', 1)
                    .arg(total / (1024.0 * 1024.0), 0, 'f', 1);
                emit downloadProgressUpdated();
            });
        };

        auto audioProg = [this](int64_t dl, int64_t total, double speed, double pct) {
            QMetaObject::invokeMethod(this, [this, dl, total, speed, pct]() {
                m_downloadPercent = 80.0 + pct * 0.2; // 音频占后 20% 进度
                double speedMb = speed / (1024.0 * 1024.0);
                m_downloadSpeedText = QString("%1 MB/s").arg(speedMb, 0, 'f', 2);
                m_downloadStatusText = QString("正在下载高清音频轨 (%1 MB / %2 MB)...")
                    .arg(dl / (1024.0 * 1024.0), 0, 'f', 1)
                    .arg(total / (1024.0 * 1024.0), 0, 'f', 1);
                emit downloadProgressUpdated();
            });
        };

        std::string err;
        bool ok = m_downloader.downloadEpisodePackage(detail, pageIdx, outDir, quality, codecId, m_config.downloadSubRes, videoProg, audioProg, &err);

        QMetaObject::invokeMethod(this, [this, ok, err]() {
            m_isDownloading = false;
            m_downloadPercent = ok ? 100.0 : 0.0;
            m_downloadStatusText = ok ? "下载已完成！" : QString("下载失败: %1").arg(QString::fromStdString(err));
            emit isDownloadingChanged();
            emit downloadProgressUpdated();
            if (ok) playCompletionSound(); // 完成提示音 (后台线程, 不阻塞 GUI)
            emit downloadFinished(ok, m_downloadStatusText);
        });
    }).detach();
}

// 4. 二维码生成 (竞态保护与状态机)
void BiliController::requestQrCode() {
    bool expected = false;
    if (!m_isRequestingQr.compare_exchange_strong(expected, true)) {
        return; // 防并发重入
    }

    m_qrState = 1; // Generating
    m_qrStatusText = "正在生成二维码...";
    m_qrCodeKey.clear();
    m_qrCodeUrl.clear();
    emit qrStatusChanged();

    std::thread([this]() {
        QrCodeInfo qr;
        std::string err;
        bool ok = m_auth.generateQrCode(qr, err);

        QMetaObject::invokeMethod(this, [this, ok, qr, err]() {
            m_isRequestingQr = false;
            if (ok) {
                m_qrCodeKey = QString::fromStdString(qr.qrcodeKey);
                m_qrCodeUrl = QString::fromStdString(qr.url);
                m_qrState = 2; // WaitingScan
                m_qrStatusText = "请使用 哔哩哔哩 客户端扫码";
                emit qrCodeGenerated();
                emit qrStatusChanged();
            } else {
                m_qrState = 6; // Error
                m_qrStatusText = QString("生成失败: %1").arg(QString::fromStdString(err));
                emit qrStatusChanged();
            }
        });
    }).detach();
}

// 5. 轮询二维码扫码状态 (竞态保护与状态流转)
void BiliController::pollQrStatus() {
    if (m_qrCodeKey.isEmpty() || (m_qrState != 2 && m_qrState != 3)) {
        return;
    }

    bool expected = false;
    if (!m_isPollingQr.compare_exchange_strong(expected, true)) {
        return; // 上一次轮询尚未完成，避免网络慢时并发堆积
    }

    std::string key = m_qrCodeKey.toStdString();
    std::thread([this, key]() {
        AppConfig cfg;
        std::string msg;
        QrPollStatus status = m_auth.pollQrCode(key, cfg, msg);

        QMetaObject::invokeMethod(this, [this, status, cfg, msg]() {
            m_isPollingQr = false;
            if (status == QrPollStatus::SUCCESS) {
                m_qrState = 4; // Success
                m_qrStatusText = "登录成功！";
                m_config.sessdata = cfg.sessdata;
                m_config.bili_jct = cfg.bili_jct;
                m_config.dedeUserId = cfg.dedeUserId;
                m_config.refreshToken = cfg.refreshToken; // 凭证续期密钥, 必须持久化
                qDebug() << "[QR-LOGIN] pollQrStatus SUCCESS in GUI thread, sessdata.len=" << m_config.sessdata.size();
                BiliAuth::saveConfig("./config.json", m_config);
                m_parser.setSessData(m_config.sessdata);
                m_downloader.setSessData(m_config.sessdata);
                m_multiDownloader.setSessData(m_config.sessdata);
                updateUserData(m_config);
                emit qrStatusChanged();
                emit qrLoginSuccess();
            } else if (status == QrPollStatus::WAIT_SCAN) {
                m_qrState = 2; // WaitingScan
                m_qrStatusText = "请使用 哔哩哔哩 客户端扫码";
                emit qrStatusChanged();
            } else if (status == QrPollStatus::WAIT_CONFIRM) {
                m_qrState = 3; // WaitingConfirm
                m_qrStatusText = "已扫码，请在手机上确认登录";
                emit qrStatusChanged();
            } else if (status == QrPollStatus::EXPIRED) {
                m_qrState = 5; // Expired
                m_qrStatusText = "二维码已过期，点击刷新";
                emit qrStatusChanged();
            }
        });
    }).detach();
}

// ============================================================================
//  三态任务队列: 待下载 → 下载中 → 已完成
//  设计要点 (对齐成熟下载器):
//   - 唯一数据源 TaskModel 仅 GUI 线程读写; worker 只通过 queued invokeMethod 改模型
//   - 队列为 FIFO, 单 worker 线程顺序消费, 任务入队/出队均持锁
//   - 下载前重取播放地址 (URL 120 分钟过期 + Wbi key 热更新)
//   - 主 CDN 失败自动切换备份 CDN; .part 文件支持断点续传
//   - 取消标记可中断阻塞下载
// ============================================================================

void BiliController::populateTaskOptions(DownloadTask &t, const BiliVideoDetail &detail) {
    // 画质选项: 按 (quality, codec) 去重 → 清晰度降序 + 编码偏好排序
    struct QualityOpt {
        int quality = 0;
        int codec = 0;
        int bandwidth = 0;
        QString desc;
    };
    std::vector<QualityOpt> opts;
    for (const auto &v : detail.videoTracks) {
        bool dup = false;
        for (const auto &o : opts) {
            if (o.quality == v.quality && o.codec == v.codecId) { dup = true; break; }
        }
        if (dup) continue;
        opts.push_back({ v.quality, v.codecId, v.bandwidth,
                         QString::fromStdString(v.qualityDesc) + " | " + QString::fromStdString(v.codecName) });
    }
    // 展示排序: 清晰度降序 + 编码偏好 (AV1 新一代开源编码 > HEVC > AVC 兼容性兜底)
    auto codecPriority = [](int c) { if (c == 13) return 0; if (c == 12) return 1; return 2; };
    std::sort(opts.begin(), opts.end(), [&](const QualityOpt &a, const QualityOpt &b) {
        if (a.quality != b.quality) return a.quality > b.quality;
        return codecPriority(a.codec) < codecPriority(b.codec);
    });

    t.qualityOptions.clear();
    t.qualityIds.clear();
    t.codecIds.clear();
    for (const auto &o : opts) {
        t.qualityOptions << o.desc;
        t.qualityIds.push_back(o.quality);
        t.codecIds.push_back(o.codec);
    }

    // 音质选项
    t.audioOptions.clear();
    t.audioIds.clear();
    for (const auto &a : detail.audioTracks) {
        t.audioOptions << QString::fromStdString(a.qualityName);
        t.audioIds.push_back(a.id);
    }

    // 默认选中: 匹配配置的默认清晰度 + 编码; 否则取最高清
    int defQuality = m_config.defaultQuality > 0 ? m_config.defaultQuality : 120;
    int defCodec = m_config.defaultCodec > 0 ? m_config.defaultCodec : 12;
    int vidIdx = 0;
    for (int i = 0; i < t.qualityIds.size(); ++i) {
        if (t.qualityIds[i] == defQuality && t.codecIds[i] == defCodec) { vidIdx = i; break; }
    }
    t.selectedQualityIndex = vidIdx;
    t.selectedAudioIndex = 0;

    // 解析最终选择的底层 id
    t.qualityId = valueAt(t.qualityIds, t.selectedQualityIndex);
    t.codecId = valueAt(t.codecIds, t.selectedQualityIndex);
    t.audioId = valueAt(t.audioIds, t.selectedAudioIndex);

    // 估算体积: 码率 × 时长 / 8
    int64_t dur = 0;
    if (t.pageIndex >= 0 && t.pageIndex < static_cast<int>(detail.pages.size())) {
        dur = detail.pages[t.pageIndex].duration;
    }
    int64_t videoBytes = 0, audioBytes = 0;
    if (!opts.empty()) videoBytes = static_cast<int64_t>(opts[vidIdx].bandwidth) * dur / 8;
    if (!detail.audioTracks.empty()) audioBytes = static_cast<int64_t>(detail.audioTracks[0].bandwidth) * dur / 8;
    t.videoSizeText = humanSize(videoBytes);
    t.audioSizeText = humanSize(audioBytes);
}

int64_t BiliController::pendingRowToTaskId(int row) const {
    QModelIndex src = m_pendingProxy.mapToSource(m_pendingProxy.index(row, 0));
    if (!src.isValid()) return 0;
    return src.data(TaskModel::TaskIdRole).toLongLong();
}

int64_t BiliController::completedRowToTaskId(int row) const {
    QModelIndex src = m_completedProxy.mapToSource(m_completedProxy.index(row, 0));
    if (!src.isValid()) return 0;
    return src.data(TaskModel::TaskIdRole).toLongLong();
}

void BiliController::enqueuePending(int64_t id, DownloadMode mode) {
    DownloadTask snapshot;
    if (!m_taskModel.taskById(id, snapshot)) return;

    snapshot.mode = mode;
    // 把当前下拉选择解析为具体 id (跨线程快照)
    snapshot.qualityId = valueAt(snapshot.qualityIds, snapshot.selectedQualityIndex);
    snapshot.codecId = valueAt(snapshot.codecIds, snapshot.selectedQualityIndex);
    snapshot.audioId = valueAt(snapshot.audioIds, snapshot.selectedAudioIndex);
    if (snapshot.saveDir.isEmpty()) snapshot.saveDir = QString::fromStdString(m_config.downloadPath);
    enqueueTask(snapshot);
}

void BiliController::enqueueTask(const DownloadTask &snapshot) {
    {
        std::lock_guard<std::mutex> lk(m_queueMutex);
        if (m_queuedIds.count(snapshot.id)) return; // 已在队列中, 去重
        m_queuedIds.insert(snapshot.id);
        m_queue.push_back(snapshot);
    }
    m_queueCv.notify_one();
    ensureWorker();
}

void BiliController::removeQueuedTask(int64_t id) {
    std::lock_guard<std::mutex> lk(m_queueMutex);
    m_queuedIds.erase(id);
    m_queue.erase(std::remove_if(m_queue.begin(), m_queue.end(),
                                 [id](const DownloadTask &t) { return t.id == id; }),
                  m_queue.end());
}

// ================= 任务持久化与恢复 =================

void BiliController::persistTasks() {
    // 仅 GUI 线程调用; 完成态任务由 history.json 归档, 不重复落盘
    saveTasksToFile("./tasks.json", m_taskModel.allTasks(), m_taskModel.nextId());
}

void BiliController::restoreTasks() {
    std::vector<DownloadTask> tasks;
    int64_t nextId = 1;
    if (!loadTasksFromFile("./tasks.json", tasks, nextId)) return;

    // 重启后 worker 已退出: 活跃任务统一回到"待下载" (下载中 → 降级),
    // 配合 .part 断点续传实现"恢复后继续下载"; 完成态兜底降级防脏数据
    for (auto &t : tasks) {
        if (t.state == DownloadState::Downloading) {
            t.state = DownloadState::Pending;
            t.speedText.clear();
            t.statusText = "上次未完成，可继续下载";
        } else if (t.state != DownloadState::Pending) {
            t.state = DownloadState::Pending;
            t.progress = 0.0;
            t.speedText.clear();
            t.statusText = "等待下载";
        } else if (t.statusText.isEmpty()) {
            t.statusText = "等待下载";
        }
    }
    m_taskModel.replaceAllTasks(tasks, nextId);
    std::cout << "[任务恢复] 已从 tasks.json 恢复 " << tasks.size() << " 条待下载任务\n";
}

// ================= 下载完成提示音 (WinMM MCI, 后台线程) =================

void BiliController::playCompletionSound() {
    if (!m_config.completionSoundEnabled) return;
    // 防叠加: 上一段还没播完就跳过 (完成提示音频率低, 丢新不丢旧)
    if (m_soundPlaying->exchange(true)) return;

    // 解析实际音频路径: 用户自定义路径优先, 空则回退到内嵌默认音效
    std::string path = m_config.completionSoundPath;
    if (path.empty()) {
        // 内嵌默认音效 (qrc): MCI 只认真实文件路径, 先落地到临时目录再播放
        QFile res(QStringLiteral(":/sounds/completion.mp3"));
        const QString tmp = QDir::tempPath() + QStringLiteral("/BiliCommander_completion.mp3");
        QFile out(tmp);
        if (res.open(QIODevice::ReadOnly) && out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            out.write(res.readAll());
            out.close();
            res.close();
            path = tmp.toStdString();
        } else {
            m_soundPlaying->store(false); // 资源读取失败则静默跳过
            return;
        }
    }

    const std::wstring wpath = u8p(path).wstring();
    // 按扩展名选设备类型: wav 走 waveaudio, 其余 (mp3/m4a/aac/flac) 走 mpegvideo
    const bool isWav = path.size() > 4 &&
        (path.compare(path.size() - 4, 4, ".wav") == 0 ||
         path.compare(path.size() - 4, 4, ".WAV") == 0);
    const std::wstring devType = isWav ? L"waveaudio" : L"mpegvideo";
    // shared_ptr 拷贝进线程: 控制器析构后标志仍存活, 无 use-after-free
    const auto busy = m_soundPlaying;
    std::thread([wpath, devType, busy]() {
        mciSendStringW(L"close snd", NULL, 0, NULL); // 清残留别名 (无则报错无害)
        const std::wstring open = L"open \"" + wpath + L"\" type " + devType + L" alias snd";
        if (mciSendStringW(open.c_str(), NULL, 0, NULL) == 0) {
            mciSendStringW(L"play snd wait", NULL, 0, NULL); // 同步播完再关
            mciSendStringW(L"close snd", NULL, 0, NULL);
        }
        busy->store(false);
    }).detach();
}

void BiliController::previewCompletionSound() {
    playCompletionSound();
}

// ================= 跨页导航点击音 (拍子木 hyoshigi) =================
// 低延迟方案: 首次点击提取内嵌音效并预打开 MCI 设备, 之后每次只发
// "play clk from 0" 重播, 天然防爆音 (连续点击即打断重头播放)。
// 仅 GUI 线程调用, 普通 bool 标志即可, 无需加锁。
void BiliController::playClickSound() {
    if (m_clickSoundReady) {
        mciSendStringW(L"play clk from 0", NULL, 0, NULL);
        return;
    }
    // 首次: 提取内嵌音效到临时目录 (MCI 只认真实文件路径)
    QFile res(QStringLiteral(":/sounds/click.mp3"));
    const QString tmp = QDir::tempPath() + QStringLiteral("/BiliCommander_click.mp3");
    QFile out(tmp);
    if (!res.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return; // 资源读取失败则静默跳过
    }
    out.write(res.readAll());
    out.close();
    res.close();

    mciSendStringW(L"close clk", NULL, 0, NULL); // 清残留别名 (无则报错无害)
    const std::wstring open = L"open \"" + u8p(tmp.toStdString()).wstring() + L"\" type mpegvideo alias clk";
    if (mciSendStringW(open.c_str(), NULL, 0, NULL) == 0) {
        m_clickSoundReady = true;
        // 拍子木偏响, 默认压到 30% 音量 (0~1000, 设备保持, 每次重播均生效)
        mciSendStringW(L"setaudio clk volume to 300", NULL, 0, NULL);
        mciSendStringW(L"play clk from 0", NULL, 0, NULL);
    }
}

void BiliController::chooseCompletionSound() {
    // 原生文件对话框 (IFileDialog): 不引入 QtWidgets 依赖, 保持 QML 原生架构
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool coInited = SUCCEEDED(hrCo);
    std::wstring picked;
    {
        IFileDialog *dlg = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
        if (SUCCEEDED(hr)) {
            COMDLG_FILTERSPEC filters[] = {
                { L"音频文件", L"*.mp3;*.wav;*.wma;*.m4a;*.aac;*.flac" },
                { L"所有文件", L"*.*" }
            };
            dlg->SetFileTypes(2, filters);
            dlg->SetTitle(L"选择下载完成提示音");
            if (SUCCEEDED(dlg->Show(nullptr))) {
                IShellItem *item = nullptr;
                if (SUCCEEDED(dlg->GetResult(&item))) {
                    PWSTR path = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                        picked = path;
                        CoTaskMemFree(path);
                    }
                    item->Release();
                }
            }
            dlg->Release();
        }
    }
    if (coInited) CoUninitialize();
    if (picked.empty()) return;
    setCompletionSoundPath(QString::fromStdWString(picked));
}

void BiliController::ensureWorker() {
    bool expected = false;
    if (m_queueRunning.compare_exchange_strong(expected, true)) {
        m_workerThread = std::thread([this]() { queueLoop(); });
    }
}

void BiliController::queueLoop() {
    for (;;) {
        DownloadTask task;
        {
            std::unique_lock<std::mutex> lk(m_queueMutex);
            m_queueCv.wait(lk, [this]() { return m_workerStop || !m_queue.empty(); });
            if (m_workerStop) {
                // 退出: 残留队列任务保持 Pending (下次运行可重新入队)
                m_queue.clear();
                m_queuedIds.clear();
                break;
            }
            task = m_queue.front();
            m_queue.pop_front();
            m_queuedIds.erase(task.id);
        }
        executeTask(task);
    }
    m_queueRunning = false;
}

void BiliController::executeTask(const DownloadTask &snapshot) {
    const int64_t id = snapshot.id;
    auto cancel = std::make_shared<std::atomic<bool>>(false);

    // 登记为当前活跃任务 (供"清空下载中"/退出时取消)
    {
        std::lock_guard<std::mutex> lk(m_activeMutex);
        m_activeTask = { id, cancel };
    }

    // 状态 → 下载中 (GUI 线程)
    QMetaObject::invokeMethod(this, [this, id]() {
        m_taskModel.setState(id, DownloadState::Downloading);
        m_taskModel.updateTask(id, [](DownloadTask &t) {
            t.progress = 0.0;
            t.speedText.clear();
            t.statusText = "准备中...";
        });
        m_isDownloading = true;
        emit isDownloadingChanged();
    });

    std::string error;
    bool ok = false;
    try {
        ok = runDownload(snapshot, cancel.get(), &error);
    } catch (const std::exception &e) {
        // worker 线程内未捕获异常会直接 fail-fast 干掉整个进程 (0xC0000409), 必须拦截
        error = std::string("内部异常: ") + e.what();
        std::cout << "[worker 异常] " << error << "\n";
    } catch (...) {
        error = "内部未知异常";
        std::cout << "[worker 未知异常]\n";
    }

    {
        std::lock_guard<std::mutex> lk(m_activeMutex);
        m_activeTask = ActiveTask();
    }

    const bool wasCancelled = cancel->load();

    // 终态收敛 (GUI 线程)
    QMetaObject::invokeMethod(this, [this, id, ok, wasCancelled, error]() {
        if (wasCancelled) {
            // 用户取消 → 回到待下载, 便于重新发起 (配合 .part 续传)
            m_taskModel.updateTask(id, [](DownloadTask &t) {
                t.progress = 0.0;
                t.speedText.clear();
                t.statusText = "已取消，可重新下载";
            });
            m_taskModel.setState(id, DownloadState::Pending);
        } else if (ok) {
            m_taskModel.updateTask(id, [](DownloadTask &t) {
                t.progress = 100.0;
                t.speedText.clear();
                // finishNote 非空则覆盖默认文案 (如"混流失败，已保留原文件"降级提示)
                t.statusText = !t.finishNote.isEmpty() ? t.finishNote : QStringLiteral("下载完成");
                t.finishNote.clear();
            });
            m_taskModel.setState(id, DownloadState::Completed);

            // 成功任务自动写入历史时光隧道
            DownloadTask taskSnapshot;
            if (m_taskModel.taskById(id, taskSnapshot)) {
                HistoryEntry he;
                he.id = taskSnapshot.id;
                he.title = taskSnapshot.title;
                he.coverUrl = taskSnapshot.coverUrl;
                he.ownerName = taskSnapshot.ownerName;
                he.durationDesc = taskSnapshot.durationDesc;
                if (!taskSnapshot.qualityOptions.isEmpty() &&
                    taskSnapshot.selectedQualityIndex >= 0 &&
                    taskSnapshot.selectedQualityIndex < taskSnapshot.qualityOptions.size()) {
                    he.qualityText = taskSnapshot.qualityOptions.at(taskSnapshot.selectedQualityIndex);
                }
                he.sizeText = taskSnapshot.videoSizeText;

                // 规范化绝对路径 (避免 ./ 等相对路径导致 QML Image URL 无法解析)
                QString rawDir = taskSnapshot.filePath.isEmpty()
                    ? (taskSnapshot.saveDir + "/" + QString::fromStdString(sanitizeFileName(taskSnapshot.title.toStdString())))
                    : taskSnapshot.filePath;
                QDir dirObj(rawDir);
                he.saveDir = dirObj.absolutePath();
                he.bvid = taskSnapshot.bvid;
                he.finishedAt = QDateTime::currentMSecsSinceEpoch();

                // 优先检查本地 cover.jpg 绝对路径
                QString localCoverPath = dirObj.absoluteFilePath(QStringLiteral("cover.jpg"));
                if (QFile::exists(localCoverPath)) {
                    he.localCover = localCoverPath;
                } else {
                    he.localCover = "";
                }

                // 优先检查主产物 mp4
                QString mp4Path = dirObj.absoluteFilePath(QString::fromStdString(sanitizeFileName(taskSnapshot.title.toStdString())) + ".mp4");
                if (QFile::exists(mp4Path)) {
                    he.filePath = mp4Path;
                    he.totalBytes = QFileInfo(mp4Path).size();
                } else {
                    he.filePath = he.saveDir;
                    qint64 total = 0;
                    for (const auto &fi : dirObj.entryInfoList(QDir::Files)) {
                        total += fi.size();
                    }
                    he.totalBytes = total;
                }
                if (he.sizeText.isEmpty() || he.sizeText == "-") {
                    he.sizeText = humanSize(he.totalBytes);
                }

                m_historyModel.appendEntry(he, "./history.json");
            }
        } else {
            // 失败 → 停留在下载中 Tab 展示错误, 允许用户重试/清空
            m_taskModel.updateTask(id, [&error](DownloadTask &t) {
                t.progress = 0.0;
                t.speedText.clear();
                t.statusText = QString::fromStdString(error);
            });
        }
        m_isDownloading = false;
        emit isDownloadingChanged();
        emit taskCountsChanged();
        // 终态落盘: 完成态由 history.json 归档不再入 tasks.json, 待下载/下载中继续保留
        persistTasks();
        // 完成提示音 (后台线程 MCI 播放, 不阻塞 GUI)
        if (ok && !wasCancelled) playCompletionSound();
        // 终态广播: 成功/失败都通知前端 (含无头测试), 便于及时收敛不干等超时
        emit downloadFinished(ok && !wasCancelled, wasCancelled
            ? "任务已取消" : QString::fromStdString(error));
    });
}

bool BiliController::runDownload(const DownloadTask &task, std::atomic<bool> *cancel, std::string *error) {
    std::string dir = task.saveDir.toStdString();
    if (dir.empty()) dir = m_config.downloadPath;
    if (dir.empty()) dir = "./downloads";
    if (CreateDirectoryW(u8p(dir).c_str(), NULL) == 0 && GetLastError() != ERROR_ALREADY_EXISTS) {
        *error = "创建下载目录失败";
        return false;
    }

    // 1. 元数据 + 播放地址:
    //    播放直链 120 分钟过期, 下载前必须刷新; Wbi key 热更新已内建在解析器
    //    若 10 分钟内刚解析过同一链接, 直接复用缓存的 DASH 直链, 避免瞬间二次 Wbi 请求触发风控
    BiliVideoDetail detail;
    {
        std::lock_guard<std::mutex> lk(m_parserMutex);
        if (task.rawInput.isEmpty()) {
            *error = "任务缺少原始链接";
            return false;
        }
        const std::string raw = task.rawInput.toStdString();
        bool cacheHit = false;
        {
            std::lock_guard<std::mutex> clk(m_cacheMutex);
            auto age = std::chrono::duration_cast<std::chrono::minutes>(
                std::chrono::steady_clock::now() - m_cachedAt);
            if (m_cachedInput == raw && age.count() < 10 && !m_cachedDetail.pages.empty()) {
                detail = m_cachedDetail;
                cacheHit = true;
            }
        }
        if (!cacheHit) {
            std::string perr;
            if (!m_parser.fetchVideoDetail(raw, detail, perr)) {
                *error = "任务解析失败: " + perr;
                return false;
            }
            if (!m_parser.fetchPlayUrl(detail, task.pageIndex, perr)) {
                *error = "获取播放地址失败: " + perr;
                return false;
            }
            {
                std::lock_guard<std::mutex> clk(m_cacheMutex);
                m_cachedInput = raw;
                m_cachedDetail = detail;
                m_cachedAt = std::chrono::steady_clock::now();
            }
        } else {
            // 元数据可复用, 但取流必须按目标 P 刷新:
            // 缓存内的播放地址固定是"上次解析/下载的 P"(通常是第 0 P),
            // 批量多 P 场景直接复用会下错集 —— 把第 0 P 的流存成第 N P 的文件。
            // Wbi 密钥已热缓存, 此处仅一次签名直链请求, 成本可忽略。
            std::string perr;
            if (!m_parser.fetchPlayUrl(detail, task.pageIndex, perr)) {
                *error = "获取播放地址失败: " + perr;
                return false;
            }
        }
    }

    if (cancel->load()) { *error = "已取消"; return false; }

    switch (task.mode) {
        case DownloadMode::Full:      return downloadFullTo(task, detail, dir, cancel, error);
        case DownloadMode::AudioOnly: return downloadAudioTo(task, detail, dir, cancel, error);
        case DownloadMode::CoverOnly: return downloadCoverTo(task, detail, dir, error);
    }
    *error = "未知任务类型";
    return false;
}

bool BiliController::downloadFullTo(const DownloadTask &task, const BiliVideoDetail &detail,
                                    const std::string &dir, std::atomic<bool> *cancel, std::string *error) {
    const int64_t id = task.id;
    std::string folder = dir + "/" + sanitizeFileName(task.title.toStdString());
    if (CreateDirectoryW(u8p(folder).c_str(), NULL) == 0 && GetLastError() != ERROR_ALREADY_EXISTS) {
        *error = "创建视频目录失败 (path=" + folder + ", err=" + std::to_string(GetLastError()) + ")";
        return false;
    }

    // 封面 (尽力而为, 失败不阻塞主流程)
    if (!detail.coverUrl.empty()) {
        std::string cerr;
        m_downloader.downloadCover(detail.coverUrl, folder + "/cover.jpg", cerr);
    }

    // 弹幕 (尽力而为)
    if (task.pageIndex >= 0 && task.pageIndex < static_cast<int>(detail.pages.size())) {
        std::vector<DanmakuEntry> dms;
        std::string derr;
        m_downloader.downloadDanmaku(detail.pages[task.pageIndex].cid, folder + "/danmaku.xml", dms, derr);
    }

    // 视频轨 (进度权重 80%)
    const VideoTrack *v = pickVideoTrack(detail, task.qualityId, task.codecId);
    if (!v || v->baseUrl.empty()) {
        *error = "未找到可下载的视频轨道";
        return false;
    }

    if (v && task.qualityId > 0 && v->quality != task.qualityId) {
        // 请求画质被静默降级 → 一次警告 (该账号缺该画质权限, 通常为大会员专享画质)
        QMetaObject::invokeMethod(this, [this, v]() {
            emit vipLockNotice(QStringLiteral("该分辨率需大会员，已自动降至 %1")
                               .arg(QString::fromStdString(v->qualityDesc)));
        });
    }

    // ===== 单流 MP4 (durl): 音视频内嵌同一文件, 直接下载为最终 .mp4, 跳过音频与混流 =====
    if (detail.singleStreamMp4) {
        const std::string outMp4 = folder + "/" + sanitizeFileName(task.title.toStdString()) + ".mp4";
        auto mp4Cb = [this, id](int64_t dl, int64_t total, double speed, double pct) {
            QMetaObject::invokeMethod(this, [this, id, dl, total, speed, pct]() {
                m_taskModel.updateTask(id, [&](DownloadTask &t) {
                    t.progress = pct;
                    t.speedText = speedString(speed);
                    t.statusText = QString("单流 MP4 %1 / %2").arg(humanSize(dl), humanSize(total));
                });
            });
        };
        if (!downloadWithFallback(videoUrls(*v), outMp4, cancel, mp4Cb, error)) {
            return false;
        }
        QMetaObject::invokeMethod(this, [this, id, outMp4]() {
            m_taskModel.updateTask(id, [&](DownloadTask &t) {
                t.progress = 100.0;
                t.statusText = "下载完成";
                t.filePath = QString::fromStdString(outMp4);
            });
        });
        return true;
    }

    auto videoCb = [this, id](int64_t dl, int64_t total, double speed, double pct) {
        QMetaObject::invokeMethod(this, [this, id, dl, total, speed, pct]() {
            m_taskModel.updateTask(id, [&](DownloadTask &t) {
                t.progress = pct * 0.8;
                t.speedText = speedString(speed);
                t.statusText = QString("视频流 %1 / %2").arg(humanSize(dl), humanSize(total));
            });
        });
    };
    if (!downloadWithFallback(videoUrls(*v), folder + "/video.m4s", cancel, videoCb, error)) {
        return false;
    }

    // 音频轨 (进度权重后 20%)
    const AudioTrack *a = pickAudioTrack(detail, task.audioId);
    if (!a || a->baseUrl.empty()) {
        *error = "未找到可下载的音频轨道";
        return false;
    }
    auto audioCb = [this, id](int64_t dl, int64_t total, double speed, double pct) {
        QMetaObject::invokeMethod(this, [this, id, dl, total, speed, pct]() {
            m_taskModel.updateTask(id, [&](DownloadTask &t) {
                t.progress = 80.0 + pct * 0.2;
                t.speedText = speedString(speed);
                t.statusText = QString("音频轨 %1 / %2").arg(humanSize(dl), humanSize(total));
            });
        });
    };
    if (!downloadWithFallback(audioUrls(*a), folder + "/audio.m4s", cancel, audioCb, error)) {
        return false;
    }

    // ================= 混流: video.m4s + audio.m4s → 单文件 MP4 =================
    // Bento4 内嵌静态库进程内无损 remux (worker 线程执行, 不碰 GUI 线程)
    // 失败降级: 保留 m4s 原文件, 任务仍进 Completed (通过 finishNote 覆盖完成文案)
    if (m_config.muxEnabled) {
        const std::string videoPath = folder + "/video.m4s";
        const std::string audioPath = folder + "/audio.m4s";
        const std::string outMp4 = folder + "/" + sanitizeFileName(task.title.toStdString()) + ".mp4";

        QMetaObject::invokeMethod(this, [this, id]() {
            m_taskModel.updateTask(id, [&](DownloadTask &t) {
                t.progress = 99.5;
                t.statusText = "正在合成 MP4...";
            });
        });

        std::string merr;
        const bool muxOk = Bento4Muxer::muxToMp4(videoPath, audioPath, outMp4, merr);
        if (muxOk) {
            // 混流成功后总是清理 m4s 源文件, 只保留最终 MP4 (删除失败仅记录, 不影响任务结果)
            if (!DeleteFileW(u8p(videoPath).c_str()) || !DeleteFileW(u8p(audioPath).c_str())) {
                std::cout << "[混流] 源 m4s 清理失败 (文件可能被占用), 已保留原文件\n";
            }
            QMetaObject::invokeMethod(this, [this, id]() {
                m_taskModel.updateTask(id, [&](DownloadTask &t) {
                    t.progress = 100.0;
                    t.statusText = "混流完成";
                });
            });
        } else {
            // 降级: m4s 保留, 任务照常 Completed, 但明确提示未混流
            std::cout << "[混流失败-降级] " << merr << "\n";
            QMetaObject::invokeMethod(this, [this, id, merr]() {
                m_taskModel.updateTask(id, [&](DownloadTask &t) {
                    t.progress = 100.0;
                    t.statusText = "混流失败，已保留音视频原文件";
                    t.finishNote = QString::fromStdString("混流失败: " + merr + "（已保留 m4s 原文件）");
                });
            });
        }
    }

    QMetaObject::invokeMethod(this, [this, id, folder]() {
        m_taskModel.updateTask(id, [&](DownloadTask &t) { t.filePath = QString::fromStdString(folder); });
    });
    return true;
}

bool BiliController::downloadAudioTo(const DownloadTask &task, const BiliVideoDetail &detail,
                                     const std::string &dir, std::atomic<bool> *cancel, std::string *error) {
    const int64_t id = task.id;
    const AudioTrack *a = pickAudioTrack(detail, task.audioId);
    if (!a || a->baseUrl.empty()) {
        *error = "该视频没有可下载的音频轨道";
        return false;
    }
    std::string folder = dir + "/" + sanitizeFileName(task.title.toStdString());
    if (CreateDirectoryW(u8p(folder).c_str(), NULL) == 0 && GetLastError() != ERROR_ALREADY_EXISTS) {
        *error = "创建视频目录失败 (path=" + folder + ", err=" + std::to_string(GetLastError()) + ")";
        return false;
    }
    auto cb = [this, id](int64_t dl, int64_t total, double speed, double pct) {
        QMetaObject::invokeMethod(this, [this, id, dl, total, speed, pct]() {
            m_taskModel.updateTask(id, [&](DownloadTask &t) {
                t.progress = pct;
                t.speedText = speedString(speed);
                t.statusText = QString("音频 %1 / %2").arg(humanSize(dl), humanSize(total));
            });
        });
    };
    if (!downloadWithFallback(audioUrls(*a), folder + "/audio.m4s", cancel, cb, error)) return false;

    QMetaObject::invokeMethod(this, [this, id, folder]() {
        m_taskModel.updateTask(id, [&](DownloadTask &t) { t.filePath = QString::fromStdString(folder); });
    });
    return true;
}

bool BiliController::downloadCoverTo(const DownloadTask &task, const BiliVideoDetail &detail,
                                     const std::string &dir, std::string *error) {
    const int64_t id = task.id;
    if (detail.coverUrl.empty()) {
        *error = "该视频没有封面";
        return false;
    }
    std::string folder = dir + "/" + sanitizeFileName(task.title.toStdString());
    if (CreateDirectoryW(u8p(folder).c_str(), NULL) == 0 && GetLastError() != ERROR_ALREADY_EXISTS) {
        *error = "创建视频目录失败 (path=" + folder + ", err=" + std::to_string(GetLastError()) + ")";
        return false;
    }
    if (!m_downloader.downloadCover(detail.coverUrl, folder + "/cover.jpg", *error)) return false;

    QMetaObject::invokeMethod(this, [this, id, folder]() {
        m_taskModel.updateTask(id, [&](DownloadTask &t) { t.filePath = QString::fromStdString(folder); });
    });
    return true;
}

bool BiliController::downloadWithFallback(const std::vector<std::string> &urls,
                                          const std::string &destFilePath,
                                          std::atomic<bool> *cancel,
                                          const ProgressCallback &cb,
                                          std::string *error) {
    if (urls.empty()) {
        *error = "没有可用的下载地址";
        return false;
    }

    // 优先采用多线程分片并发下载引擎 (当 maxDownloadThreads > 1 且未取消)
    if (m_config.maxDownloadThreads > 1 && (!cancel || !cancel->load())) {
        std::vector<std::string> backupUrls;
        for (size_t k = 1; k < urls.size(); ++k) {
            backupUrls.push_back(urls[k]);
        }
        m_multiDownloader.setSessData(m_config.sessdata);
        if (m_multiDownloader.downloadStreamParallel(urls[0], backupUrls, destFilePath,
                                                     m_config.maxDownloadThreads, cb, error, cancel)) {
            return true;
        }
        if (cancel && cancel->load()) {
            if (error) *error = "已取消";
            return false;
        }
        // 多线程引擎失败时，自动平滑退化为单线程重试链路
    }

    for (size_t i = 0; i < urls.size(); ++i) {
        if (cancel->load()) { *error = "已取消"; return false; }

        std::string partPath = destFilePath + ".part";

        // 读取 .part 当前大小 → 断点续传偏移
        auto partSize = [&]() -> int64_t {
            HANDLE h = CreateFileW(u8p(partPath).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h == INVALID_HANDLE_VALUE) return 0;
            LARGE_INTEGER sz;
            BOOL ok = GetFileSizeEx(h, &sz);
            CloseHandle(h);
            return ok ? sz.QuadPart : 0;
        };

        // 同一 CDN 至多尝试 3 次 (瞬时网络闪断可自愈), 退避 300ms → 1s
        int64_t resumeOffset = partSize();
        std::string perr;
        bool ok = false;
        for (int attempt = 0; attempt < 3; ++attempt) {
            if (cancel->load()) break;
            if (attempt > 0) Sleep(attempt == 1 ? 300 : 1000); // 指数退避
            perr.clear();
            if (m_downloader.downloadStream(urls[i], partPath, cb, 0, &perr, resumeOffset, cancel)) {
                ok = true;
                break;
            }
            if (cancel->load()) break;
            // 每次失败后重新读取续传偏移 (.part 可能已部分增长)
            resumeOffset = partSize();
        }

        if (ok) {
            // 下载完成: 原子改名为最终文件
            MoveFileExW(u8p(partPath).c_str(), u8p(destFilePath).c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
            return true;
        }

        if (cancel->load()) { *error = "已取消"; return false; }
        *error = "CDN#" + std::to_string(i + 1) + " 下载失败: " + perr;
        // 继续尝试备份 CDN
    }
    return false;
}

const VideoTrack* BiliController::pickVideoTrack(const BiliVideoDetail &detail, int qualityId, int codecId) const {
    if (detail.videoTracks.empty()) return nullptr;

    // 0. 精确命中: 画质 + 编码同时匹配 (用户显式选择优先)
    if (qualityId > 0 && codecId > 0) {
        for (const auto &v : detail.videoTracks) {
            if (v.quality == qualityId && v.codecId == codecId) return &v;
        }
    }
    // 1. 同画质回退: 保留画质, 编码降级 (如该画质无 HEVC, 取 AV1/AVC 同规格)
    if (qualityId > 0) {
        for (const auto &v : detail.videoTracks) {
            if (v.quality == qualityId) return &v;
        }
    }
    // 2. 画质降级: 不超过目标画质中取最高档 (宁降画质也不越级拉高码率触发风控)
    if (qualityId > 0) {
        const VideoTrack *best = nullptr;
        for (const auto &v : detail.videoTracks) {
            if (v.quality <= qualityId &&
                (!best || v.quality > best->quality ||
                 (v.quality == best->quality && v.bandwidth > best->bandwidth))) {
                best = &v;
            }
        }
        if (best) return best;
    }
    // 3. 最终兜底: 全局最高码率轨道
    const VideoTrack *maxBw = nullptr;
    for (const auto &v : detail.videoTracks) {
        if (!maxBw || v.bandwidth > maxBw->bandwidth) maxBw = &v;
    }
    return maxBw;
}

const AudioTrack* BiliController::pickAudioTrack(const BiliVideoDetail &detail, int audioId) const {
    const AudioTrack *fallback = nullptr;
    for (const auto &a : detail.audioTracks) {
        if (!fallback) fallback = &a;
        if (audioId > 0 && a.id != audioId) continue;
        return &a;
    }
    return fallback;
}

std::vector<std::string> BiliController::videoUrls(const VideoTrack &v) {
    std::vector<std::string> urls;
    if (!v.baseUrl.empty()) urls.push_back(v.baseUrl);
    urls.insert(urls.end(), v.backupUrls.begin(), v.backupUrls.end());
    return urls;
}

std::vector<std::string> BiliController::audioUrls(const AudioTrack &a) {
    std::vector<std::string> urls;
    if (!a.baseUrl.empty()) urls.push_back(a.baseUrl);
    urls.insert(urls.end(), a.backupUrls.begin(), a.backupUrls.end());
    return urls;
}

int BiliController::valueAt(const QVector<int> &vec, int index) {
    return (index >= 0 && index < vec.size()) ? vec[index] : 0;
}

QString BiliController::humanSize(int64_t bytes) {
    if (bytes <= 0) return "0 B";
    if (bytes < 1024) return QString("%1 B").arg(bytes);
    double kb = bytes / 1024.0;
    if (kb < 1024) return QString("%1 KB").arg(kb, 0, 'f', 1);
    double mb = kb / 1024.0;
    if (mb < 1024) return QString("%1 MB").arg(mb, 0, 'f', 2);
    return QString("%1 GB").arg(mb / 1024.0, 0, 'f', 2);
}

QString BiliController::speedString(double bps) {
    if (bps <= 0) return "0 B/s";
    if (bps < 1024) return QString("%1 B/s").arg(bps, 0, 'f', 0);
    if (bps < 1024.0 * 1024) return QString("%1 KB/s").arg(bps / 1024.0, 0, 'f', 1);
    if (bps < 1024.0 * 1024 * 1024) return QString("%1 MB/s").arg(bps / (1024.0 * 1024), 0, 'f', 2);
    return QString("%1 GB/s").arg(bps / (1024.0 * 1024 * 1024), 0, 'f', 2);
}

std::string BiliController::sanitizeFileName(const std::string &name) {
    // 1. 过滤 Windows 非法字符 + 控制字符 (0x00 ~ 0x1F)
    std::string out;
    out.reserve(name.size());
    for (unsigned char c : name) {
        if (c < 32 || c == '<' || c == '>' || c == ':' || c == '"' ||
            c == '/' || c == '\\' || c == '|' || c == '?' || c == '*') {
            out += '_';
        } else {
            out += c;
        }
    }

    // 2. 移除首尾空格 / 尾部句点 (CreateFileW 会静默丢弃尾部点或报非法路径)
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    if (out.empty()) return "未命名视频";

    // 3. Windows 保留设备名 (CON/PRN/AUX/NUL/COM1-9/LPT1-9) 前置下划线规避
    static const char *reservedNames[] = {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
    };
    {
        std::string upper = out;
        std::transform(upper.begin(), upper.end(), upper.begin(), [](char ch) {
            return static_cast<char>(::toupper(static_cast<unsigned char>(ch)));
        });
        for (const char *r : reservedNames) {
            if (upper == r) { out = "_" + out; break; }
        }
    }

    // 4. UTF-8 安全截断: 80 字节以内 (Windows 路径长度保护)。
    //    必须检查"第一个被砍掉的字节" out[cut] 而非 out[cut-1]:
    //    若 out[cut] 是续字节(0x80~0xBF), 说明 cut 落在多字节字符中间, 继续回退,
    //    否则会劈开 3 字节汉字/4 字节 Emoji, 转码后产出 U+FFFD 乱码目录名。
    if (out.size() > 80) {
        size_t cut = 80;
        while (cut > 0 && cut < out.size()) {
            unsigned char c = static_cast<unsigned char>(out[cut]);
            if ((c & 0xC0) != 0x80) break; // ASCII 或首字节 → 安全边界
            --cut;
        }
        out = out.substr(0, cut);
        while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    }
    return out.empty() ? "未命名视频" : out;
}

// ==================== Q_INVOKABLE 实现 ====================

void BiliController::setDownloadDir(const QString &dir) {
    QString d = dir.trimmed();
    if (d.isEmpty()) return;
    m_config.downloadPath = d.toStdString();
    BiliAuth::saveConfig("./config.json", m_config);
    emit downloadDirChanged();
}

void BiliController::chooseDownloadDir() {
    // 交给 QML FolderDialog 选目录, 选中后回调 setDownloadDir
    emit requestFolderPicker();
}

void BiliController::openFolder(const QString &path) {
    QString p = path;
    if (p.isEmpty()) p = QString::fromStdString(m_config.downloadPath);
    if (p.isEmpty()) return;

    // 相对路径统一解析为绝对路径 (基于进程 CWD):
    // filePath 可能存的是 "./downloads/xxx" 这类相对路径, 若直接扔给
    // QFileInfo::isFile() 会随 CWD 漂移判定失败, 导致定位按钮"无反应"。
    QFileInfo fi(p);
    if (!fi.isAbsolute())
        fi.setFile(QDir::current(), p);

    if (fi.isFile()) {
        // 文件 (单流 MP4 最终产物 / DASH 等): 资源管理器"定位并选中"该文件
        const std::wstring cmd = L"/select,\"" +
            QDir::toNativeSeparators(fi.absoluteFilePath()).toStdWString() + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", cmd.c_str(), nullptr, SW_SHOWNORMAL);
        return;
    }
    QDir d(fi.absoluteFilePath());
    if (!d.exists()) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(d.absolutePath()));
}

void BiliController::openFile(const QString &filePath) {
    if (filePath.isEmpty()) return;
    // 与 openFolder 相同: 相对路径先规范化为绝对路径, 避免 CWD 漂移导致判定失败
    QFileInfo fi(filePath);
    if (!fi.isAbsolute())
        fi.setFile(QDir::current(), filePath);
    if (fi.isDir()) {
        // 目录: 尝试播放目录内的媒体文件, 否则打开目录
        const QStringList media = { "video.m4s", "audio.m4s", "cover.jpg" };
        for (const QString &m : media) {
            QString p = filePath + "/" + m;
            if (QFileInfo::exists(p)) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(p));
                return;
            }
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
        return;
    }
    if (fi.exists()) QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
}

void BiliController::startDownloadTask(int index) {
    int64_t id = pendingRowToTaskId(index);
    if (id > 0) enqueuePending(id, DownloadMode::Full);
}

void BiliController::downloadAudioOnly(int index) {
    int64_t id = pendingRowToTaskId(index);
    if (id > 0) enqueuePending(id, DownloadMode::AudioOnly);
}

void BiliController::downloadCoverOnly(int index) {
    int64_t id = pendingRowToTaskId(index);
    if (id > 0) enqueuePending(id, DownloadMode::CoverOnly);
}

void BiliController::removePendingTask(int index) {
    int64_t id = pendingRowToTaskId(index);
    if (id > 0) {
        removeQueuedTask(id);
        m_taskModel.removeTask(id);
        persistTasks();
    }
}

void BiliController::removeCompletedTask(int index) {
    int64_t id = completedRowToTaskId(index);
    if (id > 0) {
        m_taskModel.removeTask(id);
        persistTasks();
    }
}

void BiliController::copyToClipboard(const QString &text) {
    QClipboard *cb = QGuiApplication::clipboard();
    if (cb) {
        cb->setText(text);
    }
}

void BiliController::setTaskSelection(int index, int qualityIdx, int audioIdx) {
    int64_t id = pendingRowToTaskId(index);
    if (id <= 0) return;
    m_taskModel.updateTask(id, [qualityIdx, audioIdx](DownloadTask &t) {
        t.selectedQualityIndex = qualityIdx;
        t.selectedAudioIndex = audioIdx;
    });
}

void BiliController::startDownloadAll() {
    std::vector<int64_t> ids = m_taskModel.taskIdsByState(DownloadState::Pending);
    for (int64_t id : ids) enqueuePending(id, DownloadMode::Full);
}

void BiliController::clearTab(int tabIndex) {
    switch (tabIndex) {
        case 0: { // 清空待下载
            std::vector<int64_t> ids = m_taskModel.taskIdsByState(DownloadState::Pending);
            for (int64_t id : ids) {
                removeQueuedTask(id);
                m_taskModel.removeTask(id);
            }
            persistTasks();
            break;
        }
        case 1: { // 取消当前下载 (回到待下载)
            std::shared_ptr<std::atomic<bool>> cancel;
            {
                std::lock_guard<std::mutex> lk(m_activeMutex);
                if (m_activeTask.cancel) cancel = m_activeTask.cancel;
            }
            if (cancel) cancel->store(true);
            break;
        }
        case 2: // 清空已下载记录
            m_taskModel.clearState(DownloadState::Completed);
            persistTasks();
            break;
    }
}

// ==================== 历史归档时光隧道方法 ====================

QString BiliController::historyTotalSize() const {
    return humanSize(m_historyModel.totalBytes());
}

void BiliController::openHistoryFile(int row) {
    HistoryEntry e = m_historyModel.getEntry(row);
    if (!e.filePath.isEmpty()) {
        openFile(e.filePath);
    } else if (!e.saveDir.isEmpty()) {
        openFolder(e.saveDir);
    }
}

void BiliController::openHistoryFolder(int row) {
    HistoryEntry e = m_historyModel.getEntry(row);
    if (!e.saveDir.isEmpty()) {
        openFolder(e.saveDir);
    }
}

void BiliController::removeHistory(int row) {
    m_historyModel.removeEntry(row, "./history.json");
}

void BiliController::clearHistory() {
    m_historyModel.clearAll("./history.json");
}

