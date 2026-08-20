#include "MultiThreadDownloader.hpp"
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QAbstractItemModel>
#include <QTimer>
#include <memory>
#include <thread>
#include <atomic>
#include <deque>
#include <set>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include "../BiliParser.hpp"
#include "../BiliDownloader.hpp"
#include "../BiliAuth.hpp"
#include "BiliTrackModel.hpp"
#include "BiliTaskModel.hpp"
#include "BiliHistoryModel.hpp"

class BiliController : public QObject {
    Q_OBJECT

    // ================= 1. 用户账号信息与登录状态 =================
    Q_PROPERTY(bool isLogin READ isLogin NOTIFY userProfileChanged)
    Q_PROPERTY(QString userName READ userName NOTIFY userProfileChanged)
    Q_PROPERTY(QString userAvatar READ userAvatar NOTIFY userProfileChanged)
    Q_PROPERTY(QString vipLabel READ vipLabel NOTIFY userProfileChanged)
    Q_PROPERTY(int userLevel READ userLevel NOTIFY userProfileChanged)
    Q_PROPERTY(double userCoins READ userCoins NOTIFY userProfileChanged)
    Q_PROPERTY(qint64 userMid READ userMid NOTIFY userProfileChanged)
    Q_PROPERTY(QString userMidStr READ userMidStr NOTIFY userProfileChanged)

    // ================= 2. 视频解析与元数据状态 =================
    Q_PROPERTY(bool isParsing READ isParsing NOTIFY isParsingChanged)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY videoDetailChanged)
    Q_PROPERTY(QString currentCover READ currentCover NOTIFY videoDetailChanged)
    Q_PROPERTY(QString currentOwner READ currentOwner NOTIFY videoDetailChanged)
    Q_PROPERTY(QString currentDuration READ currentDuration NOTIFY videoDetailChanged)
    Q_PROPERTY(bool isBangumi READ isBangumi NOTIFY videoDetailChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY videoDetailChanged)
    Q_PROPERTY(QString currentDesc READ currentDesc NOTIFY videoDetailChanged)
    Q_PROPERTY(double currentScore READ currentScore NOTIFY videoDetailChanged)
    Q_PROPERTY(QString currentSeasonInfo READ currentSeasonInfo NOTIFY videoDetailChanged)
    Q_PROPERTY(QString currentViews READ currentViews NOTIFY videoDetailChanged)
    Q_PROPERTY(bool currentIsPreview READ currentIsPreview NOTIFY videoDetailChanged)
    Q_PROPERTY(int selectedPageIndex READ selectedPageIndex NOTIFY selectedPageIndexChanged)
    Q_PROPERTY(QString parseErrorMessage READ parseErrorMessage NOTIFY parseErrorChanged)

    // ================= 3. 数据模型 (直接提供给 QML 列表与药丸按钮绑定) =================
    Q_PROPERTY(VideoTrackModel* trackModel READ trackModel CONSTANT)
    Q_PROPERTY(VideoPageModel* pageModel READ pageModel CONSTANT)

    // ================= 4. 下载进度与实时状态 =================
    Q_PROPERTY(bool isDownloading READ isDownloading NOTIFY isDownloadingChanged)
    Q_PROPERTY(double downloadPercent READ downloadPercent NOTIFY downloadProgressUpdated)
    Q_PROPERTY(QString downloadSpeedText READ downloadSpeedText NOTIFY downloadProgressUpdated)
    Q_PROPERTY(QString downloadStatusText READ downloadStatusText NOTIFY downloadProgressUpdated)

    // ================= 5. 二维码扫码登录状态 =================
    Q_PROPERTY(QString qrCodeUrl READ qrCodeUrl NOTIFY qrCodeGenerated)
    Q_PROPERTY(QString qrStatusText READ qrStatusText NOTIFY qrStatusChanged)
    Q_PROPERTY(int qrState READ qrState NOTIFY qrStatusChanged)

    // ================= 6. 三态下载任务队列 =================
    Q_PROPERTY(QAbstractItemModel* pendingModel READ pendingModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel* downloadingModel READ downloadingModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel* completedModel READ completedModel CONSTANT)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY taskCountsChanged)
    Q_PROPERTY(int downloadingCount READ downloadingCount NOTIFY taskCountsChanged)
    Q_PROPERTY(int completedCount READ completedCount NOTIFY taskCountsChanged)
    Q_PROPERTY(QString downloadDir READ downloadDir NOTIFY downloadDirChanged)
    Q_PROPERTY(int defaultQuality READ defaultQuality NOTIFY preferencesChanged)
    Q_PROPERTY(int defaultCodec READ defaultCodec NOTIFY preferencesChanged)
    Q_PROPERTY(bool muxEnabled READ muxEnabled NOTIFY preferencesChanged)
    Q_PROPERTY(bool downloadSubRes READ downloadSubRes NOTIFY preferencesChanged)
    Q_PROPERTY(int downloadLimitKB READ downloadLimitKB NOTIFY preferencesChanged)
    
    // ================= 7. 历史归档时光隧道 =================
    Q_PROPERTY(QAbstractItemModel* historyModel READ historyModel CONSTANT)
    Q_PROPERTY(int historyCount READ historyCount NOTIFY historyChanged)
    Q_PROPERTY(QString historyTotalSize READ historyTotalSize NOTIFY historyChanged)

public:
    explicit BiliController(QObject *parent = nullptr);
    ~BiliController();

    // 属性读取函数
    QAbstractItemModel* historyModel() { return &m_historyModel; }
    int historyCount() const { return m_historyModel.entryCount(); }
    QString historyTotalSize() const;
    bool isLogin() const { return m_userProfile.isLogin; }
    QString userName() const { return QString::fromStdString(m_userProfile.uname); }
    QString userAvatar() const { return QString::fromStdString(m_userProfile.face); }
    QString vipLabel() const { return QString::fromStdString(m_userProfile.vipLabel); }
    int userLevel() const { return m_userProfile.level; }
    double userCoins() const { return m_userProfile.money; }
    qint64 userMid() const { return m_userProfile.mid; }
    QString userMidStr() const { return QString::number(m_userProfile.mid); }

    bool isParsing() const { return m_isParsing; }
    QString currentTitle() const { return QString::fromStdString(m_currentDetail.title); }
    QString currentCover() const { return QString::fromStdString(m_currentDetail.coverUrl); }
    QString currentOwner() const { return QString::fromStdString(m_currentDetail.ownerName); }
    QString currentDuration() const;
    bool isBangumi() const { return m_currentDetail.isBangumi; }
    int pageCount() const { return static_cast<int>(m_currentDetail.pages.size()); }
    QString currentDesc() const { return QString::fromStdString(m_currentDetail.desc); }
    double currentScore() const { return m_currentDetail.score; }
    QString currentSeasonInfo() const { return QString::fromStdString(m_currentDetail.seasonInfo); }
    QString currentViews() const { return QString::fromStdString(m_currentDetail.views); }
    bool currentIsPreview() const { return m_currentDetail.isPreview; }
    int selectedPageIndex() const { return m_selectedPageIndex; }
    QString parseErrorMessage() const { return m_parseErrorMessage; }

    VideoTrackModel* trackModel() { return &m_trackModel; }
    VideoPageModel* pageModel() { return &m_pageModel; }

    bool isDownloading() const { return m_isDownloading; }
    double downloadPercent() const { return m_downloadPercent; }
    QString downloadSpeedText() const { return m_downloadSpeedText; }
    QString downloadStatusText() const { return m_downloadStatusText; }

    QString qrCodeUrl() const { return m_qrCodeUrl; }
    QString qrStatusText() const { return m_qrStatusText; }
    int qrState() const { return m_qrState; }

    QAbstractItemModel* pendingModel() { return &m_pendingProxy; }
    QAbstractItemModel* downloadingModel() { return &m_downloadingProxy; }
    QAbstractItemModel* completedModel() { return &m_completedProxy; }
    int pendingCount() const { return m_taskModel.stateCount(DownloadState::Pending); }
    int downloadingCount() const { return m_taskModel.stateCount(DownloadState::Downloading); }
    int completedCount() const { return m_taskModel.stateCount(DownloadState::Completed); }
    QString downloadDir() const { return QString::fromStdString(m_config.downloadPath); }
    int defaultQuality() const { return m_config.defaultQuality > 0 ? m_config.defaultQuality : 120; }
    int defaultCodec() const { return m_config.defaultCodec > 0 ? m_config.defaultCodec : 12; }
    bool muxEnabled() const { return m_config.muxEnabled; }
    bool downloadSubRes() const { return m_config.downloadSubRes; }
    QString themeColor() const { return QString::fromStdString(m_config.themeColor); }
    Q_INVOKABLE void setDefaultQuality(int q) {
        m_config.defaultQuality = q;
        BiliAuth::saveConfig("./config.json", m_config);
        emit preferencesChanged();
    }
    Q_INVOKABLE void setDefaultCodec(int c) {
        m_config.defaultCodec = c;
        BiliAuth::saveConfig("./config.json", m_config);
        emit preferencesChanged();
    }
    Q_INVOKABLE void setMuxEnabled(bool on) {
        m_config.muxEnabled = on;
        BiliAuth::saveConfig("./config.json", m_config);
        emit preferencesChanged();
    }
    Q_INVOKABLE void setDownloadSubRes(bool on) {
        m_config.downloadSubRes = on;
        BiliAuth::saveConfig("./config.json", m_config);
        emit preferencesChanged();
    }
    Q_INVOKABLE void setPrimaryColor(const QString &c) {
        m_config.themeColor = c.toStdString();
        BiliAuth::saveConfig("./config.json", m_config);
    }

    // ================= 下载限速 (KB/s, 0=不限速) =================
    int downloadLimitKB() const { return m_config.maxDownloadSpeedKB; }
    Q_INVOKABLE void setDownloadLimitKB(int kb) {
        if (kb < 0) kb = 0;
        m_config.maxDownloadSpeedKB = kb;
        m_rateLimiter.setRate(static_cast<size_t>(kb) * 1024ULL); // 运行时立即生效
        BiliAuth::saveConfig("./config.json", m_config);
        emit preferencesChanged();
    }

    // ================= 下载完成提示音 (用户自定义本地音频文件) =================
    // 受版权音频不入库: 由用户自备 mp3/wav 并配置路径, 空路径 = 静默
    Q_PROPERTY(bool completionSoundEnabled READ completionSoundEnabled WRITE setCompletionSoundEnabled NOTIFY preferencesChanged)
    Q_PROPERTY(QString completionSoundPath READ completionSoundPath NOTIFY preferencesChanged)
    bool completionSoundEnabled() const { return m_config.completionSoundEnabled; }
    QString completionSoundPath() const { return QString::fromStdString(m_config.completionSoundPath); }
    Q_INVOKABLE void setCompletionSoundEnabled(bool on) {
        m_config.completionSoundEnabled = on;
        BiliAuth::saveConfig("./config.json", m_config);
        emit preferencesChanged();
    }
    Q_INVOKABLE void setCompletionSoundPath(const QString &p) {
        m_config.completionSoundPath = p.toStdString();
        BiliAuth::saveConfig("./config.json", m_config);
        emit preferencesChanged();
    }
    Q_INVOKABLE void chooseCompletionSound();    // 原生文件对话框选择音频
    Q_INVOKABLE void previewCompletionSound();   // 试听当前提示音
    Q_INVOKABLE void playClickSound();           // 跨页导航点击音 (拍子木 hyoshigi)

    // ================= 供 QML 前端直接调用的异步方法 (Q_INVOKABLE) =================
    
    // 1. 解析视频 (支持 BV号, AV号, 番剧 ep/ss, 完整链接, b23.tv 短链)
    Q_INVOKABLE void parseVideo(const QString &rawInput);

    // 2. 切换当前分 P / 选集
    Q_INVOKABLE void selectPage(int pageIndex);

    // 3. 启动全流程下载 (指定清晰度 ID 与编码 ID)
    Q_INVOKABLE void startDownload(int quality, int codecId, const QString &customSaveDir = "");

    // 4. 生成登录二维码
    Q_INVOKABLE void requestQrCode();

    // 5. 轮询二维码扫码状态
    Q_INVOKABLE void pollQrStatus();

    // 6. 登出账号
    Q_INVOKABLE void logout();

    // 7. 手动载入或保存 SESSDATA 配置
    Q_INVOKABLE void setSessData(const QString &sessData);
    Q_INVOKABLE void loadLocalConfig();

    // ================= 三态任务队列接口 (QML 下载页) =================

    // 8. 设置存储目录 / 弹出系统目录选择器
    Q_INVOKABLE void setDownloadDir(const QString &dir);
    Q_INVOKABLE void chooseDownloadDir();

    // 9. 打开目录 / 打开文件 (系统资源管理器)
    Q_INVOKABLE void openFolder(const QString &path = QString());
    Q_INVOKABLE void openFile(const QString &filePath);

    // 10. 启动待下载列表中某个任务 (index 为待下载列表行号)
    Q_INVOKABLE void startDownloadTask(int index);
    Q_INVOKABLE void downloadAudioOnly(int index);
    Q_INVOKABLE void downloadCoverOnly(int index);
    Q_INVOKABLE void removePendingTask(int index);
    Q_INVOKABLE void removeCompletedTask(int index);
    Q_INVOKABLE void copyToClipboard(const QString &text);
    Q_INVOKABLE void setTaskSelection(int index, int qualityIdx, int audioIdx);
    Q_INVOKABLE void startDownloadAll();
    Q_INVOKABLE void clearTab(int tabIndex);

    // ================= 番剧 / 多 P 详细下载区专用方法 =================
    Q_INVOKABLE void enqueueAllPages(int quality = 0, int codecId = 0, const QString &customSaveDir = "");
    Q_INVOKABLE void enqueuePages(const QVariantList &pageIndices, int quality = 0, int codecId = 0, const QString &customSaveDir = "");
    Q_INVOKABLE void refreshDetail();
    Q_INVOKABLE void clearDetail();

    // ================= 历史归档时光隧道方法 =================
    Q_INVOKABLE void openHistoryFile(int row);
    Q_INVOKABLE void openHistoryFolder(int row);
    Q_INVOKABLE void removeHistory(int row);
    Q_INVOKABLE void clearHistory();

    Q_INVOKABLE void showLoginDialog() {
        emit requestLoginDialog();
    }

signals:
    void userProfileChanged();
    void isParsingChanged();
    void videoDetailChanged();
    void selectedPageIndexChanged();
    void parseErrorChanged();
    void isDownloadingChanged();
    void downloadProgressUpdated();
    void qrCodeGenerated();
    void qrStatusChanged();
    void requestLoginDialog();

    // 历史归档
    void historyChanged();

    // 事件通知信号
    void parseSuccess();
    void parseFailed(const QString &error);
    void downloadFinished(bool success, const QString &message);
    void vipLockNotice(const QString &text);
    void qrLoginSuccess();
    // 会话凭证彻底失效 (refreshToken 续期失败) → 前端弹出重新扫码登录
    void accountExpired();

    // 三态任务队列
    void taskCountsChanged();
    void downloadDirChanged();
    void preferencesChanged();
    // 请求前端弹出系统目录选择器 (QML FolderDialog 响应后回调 setDownloadDir)
    void requestFolderPicker();

private:
    BiliParser m_parser;
    BiliDownloader m_downloader;
    MultiThreadDownloader m_multiDownloader;
    BiliAuth m_auth;

    TokenBucketLimiter m_rateLimiter; // 全局限速器 (0=不限速), 注入多线程与单线程下载器

    AppConfig m_config;
    UserProfile m_userProfile;
    BiliVideoDetail m_currentDetail;

    VideoTrackModel m_trackModel;
    VideoPageModel m_pageModel;

    // ================= 三态任务模型与代理 =================
    TaskModel m_taskModel;
    TaskStateProxy m_pendingProxy;      // Pending
    TaskStateProxy m_downloadingProxy;  // Downloading
    TaskStateProxy m_completedProxy;    // Completed

    // ================= 历史归档时光隧道模型 =================
    HistoryModel m_historyModel;

    // ================= 下载队列 (FIFO + 单工作者线程) =================
    std::deque<DownloadTask> m_queue;
    std::set<int64_t> m_queuedIds;      // 已入队任务 id (去重, 仅 GUI 线程访问)
    std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    std::thread m_workerThread;
    std::atomic<bool> m_workerStop{false};
    std::atomic<bool> m_queueRunning{false};
    std::mutex m_parserMutex;           // 串行化 parser 并发访问 (Wbi key 热更新保护)

    // ================= 最近解析结果缓存 (下载前复用, 避免瞬间二次 Wbi 触发风控) =================
    // 解析成功时缓存 (原始输入 → 完整 detail 含 DASH 直链), 10 分钟内下载直接复用
    struct DetailCacheEntry {
        BiliVideoDetail detail;
        std::chrono::steady_clock::time_point timestamp;
    };
    std::unordered_map<std::string, DetailCacheEntry> m_detailCacheMap;
    QString m_lastRawInput;
    std::string m_cachedInput;
    BiliVideoDetail m_cachedDetail;
    std::chrono::steady_clock::time_point m_cachedAt;
    std::mutex m_cacheMutex;

    // 当前活跃任务取消标记 (供"清空下载中"/退出时中止阻塞下载)
    struct ActiveTask {
        int64_t id = 0;
        std::shared_ptr<std::atomic<bool>> cancel;
    };
    std::mutex m_activeMutex;
    ActiveTask m_activeTask;

    std::atomic<bool> m_isParsing{false};
    std::atomic<bool> m_isDownloading{false};

    int m_selectedPageIndex = 0;
    QString m_parseErrorMessage;

    double m_downloadPercent = 0.0;
    QString m_downloadSpeedText = "0 MB/s";
    QString m_downloadStatusText = "准备中";

    QString m_qrCodeUrl;
    QString m_qrCodeKey;
    QString m_qrStatusText;
    int m_qrState = 0; // 0=Idle, 1=Generating, 2=WaitingScan, 3=WaitingConfirm, 4=Success, 5=Expired, 6=Error
    std::atomic<bool> m_isRequestingQr{false};
    std::atomic<bool> m_isPollingQr{false};

    // ================= 账号会话定时刷新 (对齐成熟项目) =================
    QTimer *m_refreshTimer = nullptr; // 每 30 分钟静默校验登录态, 防会话静默过期
    std::atomic<bool> m_isRefreshing{false}; // 防并发重入 (刷新链路内包含续期+二次拉取)

    void updateUserData(const std::string &sessData);
    void updateUserData(const AppConfig &config);
    void refreshUserProfile();

    // ================= 三态任务队列内部实现 =================
    // 填充任务画质/音质下拉选项与默认选中 (番剧批量入队共用)
    void populateTaskOptions(DownloadTask &t, const BiliVideoDetail &detail);
    int64_t pendingRowToTaskId(int row) const;
    int64_t completedRowToTaskId(int row) const;
    void enqueuePending(int64_t id, DownloadMode mode);
    void enqueueTask(const DownloadTask &snapshot);
    void removeQueuedTask(int64_t id);
    void ensureWorker();
    void queueLoop();
    void executeTask(const DownloadTask &task);
    bool runDownload(const DownloadTask &task, std::atomic<bool> *cancel, std::string *error);
    bool downloadFullTo(const DownloadTask &task, const BiliVideoDetail &detail,
                        const std::string &dir, std::atomic<bool> *cancel, std::string *error);
    bool downloadAudioTo(const DownloadTask &task, const BiliVideoDetail &detail,
                         const std::string &dir, std::atomic<bool> *cancel, std::string *error);
    bool downloadCoverTo(const DownloadTask &task, const BiliVideoDetail &detail,
                         const std::string &dir, std::string *error);
    // 主 CDN + 备份 CDN 逐条尝试, 支持 .part 断点续传与取消
    bool downloadWithFallback(const std::vector<std::string> &urls,
                              const std::string &destFilePath,
                              std::atomic<bool> *cancel,
                              const ProgressCallback &cb,
                              std::string *error);
    const VideoTrack* pickVideoTrack(const BiliVideoDetail &detail, int qualityId, int codecId) const;
    const AudioTrack* pickAudioTrack(const BiliVideoDetail &detail, int audioId) const;
    static std::vector<std::string> videoUrls(const VideoTrack &v);
    static std::vector<std::string> audioUrls(const AudioTrack &a);
    static int valueAt(const QVector<int> &vec, int index);
    static QString humanSize(int64_t bytes);
    static QString speedString(double bps);
    static std::string sanitizeFileName(const std::string &name);

    // 解析缓存有界化: 剔除超 TTL 项 + 超上限按最旧优先驱逐, 防长期运行内存缓慢膨胀
    void pruneDetailCache();

    // ================= 任务持久化与恢复 =================
    // 活跃任务 (待下载+下载中) 落盘 tasks.json, 完成态由 history.json 归档
    void persistTasks();
    // 启动时重建任务队列: 下载中降级回待下载, 配合 .part 断点续传实现"恢复后继续下载"
    void restoreTasks();

    // ================= 下载完成提示音 =================
    // 基于 WinMM MCI 后台线程播放本地音频, 不依赖 Qt Multimedia
    void playCompletionSound();
    // 防叠加标志 (shared_ptr 让后台线程生命周期自含, 控制器析构后仍安全)
    std::shared_ptr<std::atomic<bool>> m_soundPlaying{std::make_shared<std::atomic<bool>>(false)};

    // ================= 跨页导航点击音 (拍子木 hyoshigi) =================
    // MCI 设备预打开标志: 首次点击提取内嵌音效并打开, 之后零延迟重播
    bool m_clickSoundReady = false;
};
