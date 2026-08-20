#pragma once

#include <QAbstractListModel>
#include <QSortFilterProxyModel>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QVector>
#include <cstdint>
#include <functional>
#include <vector>

// 任务三态 (与 QML 下载页三个 Tab 一一对应)
enum class DownloadState : int {
    Pending = 0,      // 待下载 (排队中)
    Downloading = 1,  // 下载中 (含失败，展示错误信息)
    Completed = 2     // 已完成
};

// 任务类型: 全媒体包 / 仅音频 / 仅封面
enum class DownloadMode : int {
    Full = 0,      // 封面 + 弹幕 + 视频轨 + 音频轨
    AudioOnly = 1, // 仅音频轨
    CoverOnly = 2  // 仅封面
};

// 单个下载任务 (全部为值类型字段，可跨线程安全快照)
struct DownloadTask {
    int64_t id = 0;
    DownloadState state = DownloadState::Pending;
    DownloadMode mode = DownloadMode::Full;

    // 解析元数据 (展示用)
    QString rawInput;      // 原始输入 (BV 号/链接)，下载前用于刷新播放地址
    QString bvid;
    QString title;
    QString coverUrl;
    QString ownerName;
    QString durationDesc;
    int pageIndex = 0;
    int totalPages = 1;

    // 画质/音质下拉选项 (展示文本 + 对应底层 id)
    QStringList qualityOptions;
    QVector<int> qualityIds;
    QVector<int> codecIds;
    QStringList audioOptions;
    QVector<int> audioIds;
    int selectedQualityIndex = 0;
    int selectedAudioIndex = 0;

    // 启动任务时解析出的最终选择 (跨线程快照)
    int qualityId = 0;
    int codecId = 0;
    int audioId = 0;

    // 估算大小
    QString videoSizeText;
    QString audioSizeText;

    // 运行时状态
    double progress = 0.0;   // 0-100
    QString speedText;
    QString statusText;
    QString filePath;        // 输出目录 (完成后)

    // 完成备注 (worker 线程在终态前通过 invokeMethod 写入, 覆盖默认"下载完成"文案)
    QString finishNote;

    QString saveDir;         // 任务发起时的存储目录快照
};

// 三态任务模型 (唯一数据源，仅供 GUI 线程读写；工作线程只消费快照)
class TaskModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum TaskRoles {
        TaskIdRole = Qt::UserRole + 1,
        TitleRole,
        CoverUrlRole,
        OwnerNameRole,
        DurationDescRole,
        VideoSizeTextRole,
        AudioSizeTextRole,
        QualityOptionsRole,
        AudioOptionsRole,
        SelectedQualityIndexRole,
        SelectedAudioIndexRole,
        StateRole,
        ProgressRole,
        SpeedTextRole,
        StatusTextRole,
        FilePathRole
    };

    explicit TaskModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // 增删改查 (仅 GUI 线程)
    int64_t addTask(const DownloadTask &task);
    bool taskById(int64_t id, DownloadTask &out) const;
    DownloadTask taskAtRow(int row) const;
    int64_t taskIdAtRow(int row) const;
    bool updateTask(int64_t id, const std::function<void(DownloadTask &)> &mutator);
    bool setState(int64_t id, DownloadState state);
    bool removeTask(int64_t id);
    void clearState(DownloadState state);

    int stateCount(DownloadState state) const;
    std::vector<int64_t> taskIdsByState(DownloadState state) const;

    // ================= 任务持久化支撑 (启动恢复 / 退出落盘) =================
    std::vector<DownloadTask> allTasks() const;          // 全量快照
    int64_t nextId() const;                              // 当前自增 ID (持久化防碰撞)
    void replaceAllTasks(const std::vector<DownloadTask> &tasks, int64_t nextId); // 整表重建

signals:
    void taskStateChanged(int64_t id, int oldState, int newState);
    void taskCountsChanged();

private:
    int findRowById(int64_t id) const;

    std::vector<DownloadTask> m_tasks;
    int64_t m_nextId = 1;
};

// ================= 任务 JSON 序列化 (与 HistoryModel 持久化同风格) =================
// 存储活跃任务 (待下载 + 下载中)，已完成任务由历史归档 history.json 负责。
QJsonObject downloadTaskToJson(const DownloadTask &t);
DownloadTask downloadTaskFromJson(const QJsonObject &o);
void saveTasksToFile(const QString &filePath, const std::vector<DownloadTask> &tasks, int64_t nextId);
bool loadTasksFromFile(const QString &filePath, std::vector<DownloadTask> &tasks, int64_t &nextId);

// 按任务状态过滤的代理模型 (QML 三个 Tab 各挂一个)
class TaskStateProxy : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit TaskStateProxy(QObject *parent = nullptr);
    TaskStateProxy(std::vector<DownloadState> states, QObject *parent = nullptr);

    void setStates(std::vector<DownloadState> states);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    std::vector<DownloadState> m_states;
};
