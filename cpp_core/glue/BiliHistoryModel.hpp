#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>
#include <cstdint>
#include <QDateTime>

// 单条历史记录（值类型，供 HistoryModel 安全快照与持久化）
struct HistoryEntry {
    int64_t id = 0;            // 自增主键 / 任务ID
    QString title;             // 视频/番剧标题
    QString coverUrl;          // 远程封面 URL（回退用）
    QString localCover;        // 本地 cover.jpg 绝对路径（优先加载）
    QString ownerName;         // UP主
    QString durationDesc;      // 时长 "24:00"
    QString qualityText;       // 画质/编码 "1080P | hvc1"
    QString sizeText;          // 展示用大小 "60 MB"
    qint64 totalBytes = 0;     // 总字节数（顶部统计求和）
    QString filePath;          // 主产物路径（output.mp4，混流后 或 视频目录/文件）
    QString saveDir;           // 任务存储目录
    QString bvid;              // 供未来"重新下载"扩展
    qint64 finishedAt = 0;     // 完成时间戳（ms）
};

class HistoryModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum HistoryRoles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        CoverUrlRole,
        LocalCoverRole,
        OwnerNameRole,
        DurationDescRole,
        QualityTextRole,
        SizeTextRole,
        TotalBytesRole,
        FilePathRole,
        SaveDirRole,
        BvidRole,
        FinishedAtRole,
        FinishedTextRole
    };

    explicit HistoryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // 添加记录 (最新插入在表头最前，自动裁切至 200 条，并即时落盘)
    void appendEntry(const HistoryEntry &entry, const QString &savePath = QString());

    // 删除单条记录 (仅删记录，绝不触碰磁盘真实文件)
    bool removeEntry(int row, const QString &savePath = QString());

    // 清空记录 (仅清空记录，绝不触碰磁盘真实文件)
    void clearAll(const QString &savePath = QString());

    // 持久化加载与落盘
    void loadFromFile(const QString &filePath);
    void saveToFile(const QString &filePath) const;

    qint64 totalBytes() const;
    int entryCount() const;
    HistoryEntry getEntry(int row) const;

signals:
    void historyUpdated();

private:
    QVector<HistoryEntry> m_entries;
    mutable qint64 m_cachedTotalBytes = 0;
    mutable bool m_totalBytesDirty = true;
};
