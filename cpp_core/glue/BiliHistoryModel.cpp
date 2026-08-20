#include "BiliHistoryModel.hpp"
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>

HistoryModel::HistoryModel(QObject *parent)
    : QAbstractListModel(parent) {
}

int HistoryModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return m_entries.size();
}

QVariant HistoryModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return QVariant();
    }

    const auto &e = m_entries.at(index.row());
    switch (role) {
        case IdRole:
            return QVariant::fromValue(e.id);
        case TitleRole:
            return e.title;
        case CoverUrlRole:
            return e.coverUrl;
        case LocalCoverRole:
            return e.localCover;
        case OwnerNameRole:
            return e.ownerName;
        case DurationDescRole:
            return e.durationDesc;
        case QualityTextRole:
            return e.qualityText;
        case SizeTextRole:
            return e.sizeText;
        case TotalBytesRole:
            return QVariant::fromValue(e.totalBytes);
        case FilePathRole:
            return e.filePath;
        case SaveDirRole:
            return e.saveDir;
        case BvidRole:
            return e.bvid;
        case FinishedAtRole:
            return QVariant::fromValue(e.finishedAt);
        case FinishedTextRole: {
            if (e.finishedAt <= 0) return QStringLiteral("-");
            QDateTime dt = QDateTime::fromMSecsSinceEpoch(e.finishedAt);
            return dt.toString(QStringLiteral("MM-dd HH:mm"));
        }
        default:
            return QVariant();
    }
}

QHash<int, QByteArray> HistoryModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[IdRole] = "id";
    roles[TitleRole] = "title";
    roles[CoverUrlRole] = "coverUrl";
    roles[LocalCoverRole] = "localCover";
    roles[OwnerNameRole] = "ownerName";
    roles[DurationDescRole] = "durationDesc";
    roles[QualityTextRole] = "qualityText";
    roles[SizeTextRole] = "sizeText";
    roles[TotalBytesRole] = "totalBytes";
    roles[FilePathRole] = "filePath";
    roles[SaveDirRole] = "saveDir";
    roles[BvidRole] = "bvid";
    roles[FinishedAtRole] = "finishedAt";
    roles[FinishedTextRole] = "finishedText";
    return roles;
}

void HistoryModel::appendEntry(const HistoryEntry &entry, const QString &savePath) {
    beginInsertRows(QModelIndex(), 0, 0);
    m_entries.prepend(entry);
    endInsertRows();

    // 封面上限：仅保留最近 200 条，超限自动裁剪最旧记录
    if (m_entries.size() > 200) {
        beginRemoveRows(QModelIndex(), 200, m_entries.size() - 1);
        m_entries.resize(200);
        endRemoveRows();
    }

    m_totalBytesDirty = true;
    emit historyUpdated();

    if (!savePath.isEmpty()) {
        saveToFile(savePath);
    }
}

bool HistoryModel::removeEntry(int row, const QString &savePath) {
    if (row < 0 || row >= m_entries.size()) {
        return false;
    }

    beginRemoveRows(QModelIndex(), row, row);
    m_entries.removeAt(row);
    endRemoveRows();

    m_totalBytesDirty = true;
    emit historyUpdated();

    if (!savePath.isEmpty()) {
        saveToFile(savePath);
    }
    return true;
}

void HistoryModel::clearAll(const QString &savePath) {
    if (m_entries.isEmpty()) return;

    beginResetModel();
    m_entries.clear();
    m_entries.squeeze(); // 清空后释放 QVector 冗余容量
    m_totalBytesDirty = true;
    endResetModel();

    emit historyUpdated();

    if (!savePath.isEmpty()) {
        saveToFile(savePath);
    }
}

void HistoryModel::loadFromFile(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isArray()) {
        return;
    }

    QJsonArray arr = doc.array();
    QVector<HistoryEntry> loaded;
    loaded.reserve(qMin(arr.size(), 200));

    for (int i = 0; i < arr.size() && loaded.size() < 200; ++i) {
        QJsonObject obj = arr[i].toObject();
        HistoryEntry e;
        e.id = obj.value("id").toVariant().toLongLong();
        e.title = obj.value("title").toString();
        e.coverUrl = obj.value("coverUrl").toString();
        e.localCover = obj.value("localCover").toString();
        e.ownerName = obj.value("ownerName").toString();
        e.durationDesc = obj.value("durationDesc").toString();
        e.qualityText = obj.value("qualityText").toString();
        e.sizeText = obj.value("sizeText").toString();
        e.totalBytes = obj.value("totalBytes").toVariant().toLongLong();
        e.filePath = obj.value("filePath").toString();
        e.saveDir = obj.value("saveDir").toString();
        e.bvid = obj.value("bvid").toString();
        e.finishedAt = obj.value("finishedAt").toVariant().toLongLong();

        // 自动将相对路径或历史记录补全为规范绝对路径
        if (!e.saveDir.isEmpty()) {
            QDir d(e.saveDir);
            if (d.exists()) {
                e.saveDir = d.absolutePath();
            }
        }
        if (e.localCover.isEmpty() && !e.saveDir.isEmpty()) {
            QDir d(e.saveDir);
            QString cov = d.absoluteFilePath(QStringLiteral("cover.jpg"));
            if (QFile::exists(cov)) {
                e.localCover = cov;
            }
        } else if (!e.localCover.isEmpty()) {
            QFileInfo fi(e.localCover);
            if (fi.exists()) {
                e.localCover = fi.absoluteFilePath();
            } else if (!e.saveDir.isEmpty()) {
                QDir d(e.saveDir);
                QString cov = d.absoluteFilePath(QStringLiteral("cover.jpg"));
                if (QFile::exists(cov)) {
                    e.localCover = cov;
                }
            }
        }

        loaded.append(e);
    }

    beginResetModel();
    m_entries = std::move(loaded);
    m_totalBytesDirty = true;
    endResetModel();

    emit historyUpdated();
}

void HistoryModel::saveToFile(const QString &filePath) const {
    QJsonArray arr;
    for (const auto &e : m_entries) {
        QJsonObject obj;
        obj["id"] = e.id;
        obj["title"] = e.title;
        obj["coverUrl"] = e.coverUrl;
        obj["localCover"] = e.localCover;
        obj["ownerName"] = e.ownerName;
        obj["durationDesc"] = e.durationDesc;
        obj["qualityText"] = e.qualityText;
        obj["sizeText"] = e.sizeText;
        obj["totalBytes"] = e.totalBytes;
        obj["filePath"] = e.filePath;
        obj["saveDir"] = e.saveDir;
        obj["bvid"] = e.bvid;
        obj["finishedAt"] = e.finishedAt;
        arr.append(obj);
    }

    QJsonDocument doc(arr);
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
    }
}

qint64 HistoryModel::totalBytes() const {
    if (m_totalBytesDirty) {
        m_cachedTotalBytes = 0;
        for (const auto &e : m_entries) {
            m_cachedTotalBytes += e.totalBytes;
        }
        m_totalBytesDirty = false;
    }
    return m_cachedTotalBytes;
}

int HistoryModel::entryCount() const {
    return m_entries.size();
}

HistoryEntry HistoryModel::getEntry(int row) const {
    if (row >= 0 && row < m_entries.size()) {
        return m_entries.at(row);
    }
    return HistoryEntry();
}
