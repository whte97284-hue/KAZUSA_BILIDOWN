#include "BiliTrackModel.hpp"
#include <QString>

static QString formatDuration(int64_t seconds) {
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

// ================= VideoTrackModel 实现 =================

VideoTrackModel::VideoTrackModel(QObject *parent) : QAbstractListModel(parent) {}

int VideoTrackModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(m_tracks.size());
}

QVariant VideoTrackModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_tracks.size())) {
        return QVariant();
    }

    const auto &t = m_tracks[index.row()];
    switch (role) {
        case QualityRole:
            return t.quality;
        case QualityDescRole:
            return QString::fromStdString(t.qualityDesc);
        case CodecIdRole:
            return t.codecId;
        case CodecNameRole:
            return QString::fromStdString(t.codecName);
        case ResolutionRole:
            return QString("%1x%2").arg(t.width).arg(t.height);
        case FpsRole:
            return t.fps;
        case BandwidthRole:
            return t.bandwidth;
        case BandwidthDescRole:
            return QString("%1 kbps").arg(t.bandwidth / 1000);
        case BaseUrlRole:
            return QString::fromStdString(t.baseUrl);
        case Qt::DisplayRole:
            return QString("%1 (%2, %3fps)").arg(QString::fromStdString(t.qualityDesc), QString::fromStdString(t.codecName)).arg(t.fps);
        default:
            return QVariant();
    }
}

QHash<int, QByteArray> VideoTrackModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[QualityRole] = "quality";
    roles[QualityDescRole] = "qualityDesc";
    roles[CodecIdRole] = "codecId";
    roles[CodecNameRole] = "codecName";
    roles[ResolutionRole] = "resolution";
    roles[FpsRole] = "fps";
    roles[BandwidthRole] = "bandwidth";
    roles[BandwidthDescRole] = "bandwidthDesc";
    roles[BaseUrlRole] = "baseUrl";
    return roles;
}

void VideoTrackModel::setTracks(const std::vector<VideoTrack> &tracks) {
    beginResetModel();
    m_tracks = tracks;
    endResetModel();
}

void VideoTrackModel::clear() {
    beginResetModel();
    m_tracks.clear();
    endResetModel();
}

// ================= VideoPageModel 实现 =================

VideoPageModel::VideoPageModel(QObject *parent) : QAbstractListModel(parent) {}

int VideoPageModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(m_pages.size());
}

QVariant VideoPageModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_pages.size())) {
        return QVariant();
    }

    const auto &p = m_pages[index.row()];
    switch (role) {
        case PageNumberRole:
            return p.page;
        case CidRole:
            return static_cast<qint64>(p.cid);
        case TitleRole:
            return QString::fromStdString(p.part);
        case DurationRole:
            return static_cast<qint64>(p.duration);
        case DurationDescRole:
            return formatDuration(p.duration);
        case FirstFrameRole:
            return QString::fromStdString(p.firstFrame);
        case Qt::DisplayRole:
            return QString("P%1 %2 (%3)").arg(p.page).arg(QString::fromStdString(p.part), formatDuration(p.duration));
        default:
            return QVariant();
    }
}

QHash<int, QByteArray> VideoPageModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[PageNumberRole] = "pageNumber";
    roles[CidRole] = "cid";
    roles[TitleRole] = "title";
    roles[DurationRole] = "duration";
    roles[DurationDescRole] = "durationDesc";
    roles[FirstFrameRole] = "firstFrame";
    return roles;
}

void VideoPageModel::setPages(const std::vector<VideoPage> &pages) {
    beginResetModel();
    m_pages = pages;
    endResetModel();
}

void VideoPageModel::clear() {
    beginResetModel();
    m_pages.clear();
    endResetModel();
}
